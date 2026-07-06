#include "FileBrowserPanel.h"
#include "resource/GameResources.h"
#include "ui/QtDialogs.h"
#include "ui/Settings.h"
#include "ui/dialogs/ProEditorDialog.h"
#include "ui/theme/ThemeManager.h"
#include "ui/UIConstants.h"
#include "reader/pro/ProReader.h"
#include "reader/ReaderFactory.h"
#include "format/pro/Pro.h"
#include "format/msg/Msg.h"
#include "ui/IconHelper.h"
#include "ui/GameEnums.h"

#include <QFileInfo>
#include <QDir>
#include <QHeaderView>
#include <QSplitter>
#include <QGroupBox>
#include <QScrollBar>
#include <QApplication>
#include <QStyle>
#include <QMessageBox>
#include <QFile>
#include <QProgressBar>
#include <QThread>
#include <QMetaObject>
#include <QRegularExpression>
#include <chrono>
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <util/ProHelper.h>

namespace geck {

FileBrowserProxyModel::FileBrowserProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setRecursiveFilteringEnabled(true);
}

bool FileBrowserProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    if (filterRegularExpression().pattern().isEmpty()) {
        return true;
    }

    QModelIndex sourceModel = this->sourceModel()->index(sourceRow, 0, sourceParent);
    if (!sourceModel.isValid()) {
        return false;
    }

    QString fileName = sourceModel.data(Qt::DisplayRole).toString();

    QModelIndex proNameIndex = this->sourceModel()->index(sourceRow, 4, sourceParent);
    QString proName = proNameIndex.isValid() ? proNameIndex.data(Qt::DisplayRole).toString() : QString();

    QModelIndex sourceIndex = this->sourceModel()->index(sourceRow, 2, sourceParent);
    QString sourceName = sourceIndex.isValid() ? sourceIndex.data(Qt::DisplayRole).toString() : QString();

    bool fileNameMatches = fileName.contains(filterRegularExpression());
    bool proNameMatches = !proName.isEmpty() && proName.contains(filterRegularExpression());
    bool sourceMatches = !sourceName.isEmpty() && sourceName.contains(filterRegularExpression());

    return fileNameMatches || proNameMatches || sourceMatches;
}

bool FileBrowserProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    if (!left.isValid() || !right.isValid()) {
        return QSortFilterProxyModel::lessThan(left, right);
    }

    QModelIndex leftTypeIndex = left.sibling(left.row(), 1);
    QModelIndex rightTypeIndex = right.sibling(right.row(), 1);

    QString leftType = leftTypeIndex.data(Qt::DisplayRole).toString();
    QString rightType = rightTypeIndex.data(Qt::DisplayRole).toString();

    bool leftIsDir = (leftType == "Directory");
    bool rightIsDir = (rightType == "Directory");

    if (leftIsDir && !rightIsDir) {
        return true;
    }
    if (!leftIsDir && rightIsDir) {
        return false;
    }

    return QSortFilterProxyModel::lessThan(left, right);
}

namespace {

    // Display path relative to the native mount that contains it (VFS paths may carry the
    // mount prefix and doubled leading slashes); falls back to stripping leading slashes.
    QString normalizeForDisplay(const QString& fullPath, const std::vector<std::filesystem::path>& nativeDirectories) {
        std::string fullPathStr = fullPath.toStdString();

        for (const auto& nativeDir : nativeDirectories) {
            std::string nativeDirStr = nativeDir.string();

            if (!nativeDirStr.empty() && nativeDirStr.back() != '/') {
                nativeDirStr += '/';
            }

            size_t pos = fullPathStr.find(nativeDirStr);
            if (pos != std::string::npos) {
                std::string relativePath = fullPathStr.substr(pos + nativeDirStr.length());
                return QString::fromStdString(relativePath);
            }
        }

        QString result = fullPath;
        while (result.startsWith('/')) {
            result = result.mid(1);
        }

        return result;
    }

} // namespace

FileLoaderWorker::FileLoaderWorker(std::shared_ptr<resource::GameResources> resources, QObject* parent)
    : QObject(parent)
    , _resources(std::move(resources)) {
}

QString FileLoaderWorker::resolveProName(const std::string& vfsPath) const {
    try {
        std::string path = vfsPath;
        if (!path.empty() && path.front() == '/') {
            path = path.substr(1);
        }

        const auto* pro = _resources->repository().load<Pro>(path);
        if (!pro) {
            return QStringLiteral("Failed to load");
        }

        const Msg* msgFile = nullptr;
        try {
            msgFile = ProHelper::msgFile(*_resources, pro->type());
        } catch (const std::exception&) {
            msgFile = nullptr; // repository().load throws when the msg file isn't mounted
        }
        if (!msgFile) {
            return QStringLiteral("MSG not found");
        }

        // Read via getMessages().find(): Msg::message() is a map operator[] that INSERTS a
        // blank entry on a missing id, mutating the shared cached Msg — unacceptable from a
        // worker thread (and a silent leak on the UI thread too).
        const auto messageId = static_cast<int>(pro->header.message_id);
        const auto& messages = msgFile->getMessages();
        const auto it = messages.find(messageId);
        if (it == messages.end() || it->second.text.empty()) {
            return QStringLiteral("No name (ID: %1)").arg(messageId);
        }
        return QString::fromStdString(it->second.text);
    } catch (const std::exception& e) {
        spdlog::debug("FileLoaderWorker: failed to resolve PRO name for '{}': {}", vfsPath, e.what());
        return QStringLiteral("Error: %1").arg(e.what());
    }
}

void FileLoaderWorker::loadFiles() {
    const auto workStart = std::chrono::steady_clock::now();
    try {
        spdlog::debug("FileLoaderWorker: Starting background file loading...");
        Q_EMIT loadingProgress(0, 100, "Initializing file system...");

        spdlog::debug("FileLoaderWorker: Listing mounted files...");
        auto filePaths = _resources->files().list();
        std::vector<std::string> allFiles;
        allFiles.reserve(filePaths.size());
        for (const auto& path : filePaths) {
            allFiles.push_back(path.generic_string());
        }
        spdlog::debug("FileLoaderWorker: Got {} files from data paths", allFiles.size());

        if (allFiles.empty()) {
            spdlog::warn("FileLoaderWorker: Data filesystem returned no files - data may not be loaded yet");
            Q_EMIT loadingError("No files found - data paths may not be loaded yet");
            Q_EMIT loadingComplete();
            return;
        }

        Q_EMIT loadingProgress(50, 100, "Processing file list...");

        if (_shouldStop.load()) {
            return;
        }

        std::unordered_set<std::string> fileTypes;
        std::vector<FileBrowserEntry> entries;
        entries.reserve(allFiles.size());
        int processed = 0;
        const int totalFiles = static_cast<int>(allFiles.size());

        for (const auto& file : allFiles) {
            if (_shouldStop.load()) {
                return;
            }

            FileBrowserEntry entry;
            entry.path = QString::fromStdString(file);
            entry.normalizedPath = normalizeForDisplay(entry.path, _nativeDirectories);

            QString suffix = QFileInfo(entry.path).suffix().toLower();
            if (!suffix.isEmpty()) {
                entry.extension = "." + suffix;
                fileTypes.insert(entry.extension.toStdString());
            }

            if (auto source = _resources->files().sourceInfo(file)) {
                entry.source = QString::fromStdString(source->displayLabel);
            } else {
                entry.source = QStringLiteral("Unknown");
            }

            entries.push_back(std::move(entry));

            if (++processed % 1000 == 0) {
                int progressPercent = 50 + (processed * 50) / totalFiles;
                Q_EMIT loadingProgress(progressPercent, 100,
                    QString("Indexing files... %1/%2").arg(processed).arg(totalFiles));
            }
        }

        Q_EMIT loadingProgress(100, 100, "File loading completed");
        spdlog::debug("FileLoaderWorker: Emitting fileTypesExtracted with {} types", fileTypes.size());
        Q_EMIT fileTypesExtracted(fileTypes);
        spdlog::info("FileLoaderWorker: listed and indexed {} files in {}ms", entries.size(),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - workStart).count());
        Q_EMIT filesLoaded(entries);

        spdlog::debug("FileLoaderWorker: Loaded {} files with {} file types",
            allFiles.size(), fileTypes.size());

        // Second pass: PRO display names. Parsing every proto (plus its msg file) is the one
        // expensive per-file job — seconds on a cold cache — so it runs after the tree data is
        // already delivered and trickles in as batched column updates.
        QHash<QString, QString> names;
        for (const auto& entry : entries) {
            if (_shouldStop.load()) {
                return;
            }
            if (entry.extension != QLatin1String(".pro")) {
                continue;
            }
            names.insert(entry.path, resolveProName(entry.path.toStdString()));
            if (names.size() >= 500) {
                Q_EMIT proNamesResolved(names);
                names.clear();
            }
        }
        if (!names.isEmpty()) {
            Q_EMIT proNamesResolved(names);
        }

        Q_EMIT loadingComplete();

    } catch (const std::exception& e) {
        spdlog::error("FileLoaderWorker: Error loading files: {}", e.what());
        Q_EMIT loadingError(QString::fromStdString(e.what()));
        Q_EMIT loadingComplete();
    }
}

FileTreeItem::FileTreeItem(const QString& name, ItemType type)
    : QStandardItem(name)
    , _type(type) {
    setEditable(false);

    if (type == Directory) {
        setIcon(createIcon(":/icons/filetypes/folder.svg"));
    } else {
        setIcon(getFileIcon(name));
    }
}

QIcon FileTreeItem::getFileIcon(const QString& fileName) {
    QFileInfo fileInfo(fileName);
    QString suffix = fileInfo.suffix().toLower();

    static const QMap<QString, QString> iconMap = {
        { "map", ":/icons/filetypes/map.svg" },
        { "frm", ":/icons/filetypes/image.svg" },
        { "pro", ":/icons/filetypes/proto.svg" },
        { "msg", ":/icons/filetypes/message.svg" },
        { "dat", ":/icons/filetypes/data.svg" },
        { "lst", ":/icons/filetypes/list.svg" },
        { "int", ":/icons/filetypes/script.svg" },
        { "ssl", ":/icons/filetypes/script.svg" },
        { "luau", ":/icons/filetypes/luau.svg" },
        { "lua", ":/icons/filetypes/luau.svg" },
        { "txt", ":/icons/filetypes/text.svg" },
        { "cfg", ":/icons/filetypes/text.svg" },
        { "gam", ":/icons/filetypes/text.svg" },
        { "ini", ":/icons/filetypes/text.svg" },
        { "pal", ":/icons/filetypes/palette.svg" }
    };

    if (iconMap.contains(suffix)) {
        return createIcon(iconMap[suffix]);
    }

    return createIcon(":/icons/filetypes/default.svg");
}

bool FileBrowserPanel::isTextFile(const QString& filePath) const {
    QString suffix = QFileInfo(filePath).suffix().toLower();
    return game::enums::textFileExtensions().contains(suffix);
}

FileBrowserPanel::FileBrowserPanel(std::shared_ptr<resource::GameResources> resources, std::shared_ptr<Settings> settings, QWidget* parent)
    : QWidget(parent)
    , _mainLayout(nullptr)
    , _filterLayout(nullptr)
    , _searchLineEdit(nullptr)
    , _fileTypeComboBox(nullptr)
    , _refreshButton(nullptr)
    , _treeView(nullptr)
    , _treeModel(nullptr)
    , _statusLabel(nullptr)
    , _progressBar(nullptr)
    , _loaderThread(nullptr)
    , _loaderWorker(nullptr)
    , _currentSearchFilter("")
    , _currentFileTypeFilter("All Files")
    , _chunkTimer(new QTimer(this))
    , _searchTimer(new QTimer(this))
    , _isLoading(false)
    , _resourcesShared(std::move(resources))
    , _settings(std::move(settings)) {

    setupUI();

    _chunkTimer->setSingleShot(true);
    _chunkTimer->setInterval(CHUNK_DELAY_MS);
    connect(_chunkTimer, &QTimer::timeout, this, &FileBrowserPanel::processNextChunk);

    _searchTimer->setSingleShot(true);
    _searchTimer->setInterval(300);
    connect(_searchTimer, &QTimer::timeout, this, &FileBrowserPanel::performDebouncedSearch);

    _statusLabel->setText("Ready - Click Refresh to load files");
}

void FileBrowserPanel::stopAndDestroyLoader() noexcept {
    if (!_loaderThread) {
        return;
    }

    if (_loaderWorker) {
        // loadFiles() polls _shouldStop, so cancelling it lets the worker's loop exit and the
        // thread's event loop quit — wait() then returns promptly. Disconnect first so a late
        // queued signal can't reach a half-torn-down panel.
        _loaderWorker->_shouldStop.store(true);
        disconnect(_loaderWorker, nullptr, this, nullptr);
    }
    disconnect(_loaderThread, nullptr, this, nullptr);

    if (_loaderThread->isRunning()) {
        _loaderThread->quit();
        if (!_loaderThread->wait(3000)) {
            // Only reachable if the worker is stuck in the one step it cannot poll _shouldStop
            // from — the initial VFS files().list(). Force-stop as a last resort (there is no
            // other cancellation path there) so the worker can be freed without a data race.
            spdlog::warn("FileBrowserPanel: loader thread didn't stop within 3s, forcing termination");
            _loaderThread->terminate();
            _loaderThread->wait();
        }
    }

    // The thread is stopped now, so the worker is no longer running and is deleted directly:
    // deleteLater() would never run here, because the worker lives in a thread whose event loop
    // has already exited (and it is not parented to the QThread). delete nullptr is a safe no-op.
    delete _loaderWorker;
    _loaderWorker = nullptr;
    delete _loaderThread;
    _loaderThread = nullptr;
}

FileBrowserPanel::~FileBrowserPanel() {
    spdlog::debug("FileBrowserPanel: Destructor called, cleaning up threads...");

    if (_loaderWorker) {
        _loaderWorker->_shouldStop.store(true);
    }

    if (_chunkTimer) {
        _chunkTimer->stop();
    }

    if (_searchTimer) {
        _searchTimer->stop();
    }

    stopAndDestroyLoader();

    spdlog::debug("FileBrowserPanel: Destructor completed");
}

void FileBrowserPanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN);
    _mainLayout->setSpacing(ui::constants::SPACING_TIGHT);

    setupFilterControls();
    setupTreeView();
    setupStatusBar();

    setLayout(_mainLayout);
}

void FileBrowserPanel::setupFilterControls() {
    QGroupBox* filterGroup = new QGroupBox("Filters", this);
    QVBoxLayout* filterGroupLayout = new QVBoxLayout(filterGroup);

    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:", this));

    _searchLineEdit = new QLineEdit(this);
    _searchLineEdit->setPlaceholderText("Type to search in all files instantly...");
    _searchLineEdit->setClearButtonEnabled(true);
    connect(_searchLineEdit, &QLineEdit::textChanged, this, &FileBrowserPanel::onSearchTextChanged);
    searchLayout->addWidget(_searchLineEdit);

    filterGroupLayout->addLayout(searchLayout);

    QHBoxLayout* typeLayout = new QHBoxLayout();
    typeLayout->addWidget(new QLabel("Type:", this));

    _fileTypeComboBox = new QComboBox(this);
    _fileTypeComboBox->addItem("All Files");
    connect(_fileTypeComboBox, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
        this, &FileBrowserPanel::onFileTypeChanged);
    typeLayout->addWidget(_fileTypeComboBox, 1);

    _refreshButton = new QPushButton("Refresh", this);
    _refreshButton->setIcon(createIcon(":/icons/ui/refresh.svg"));
    connect(_refreshButton, &QPushButton::clicked, this, &FileBrowserPanel::onRefreshClicked);
    typeLayout->addWidget(_refreshButton);

    filterGroupLayout->addLayout(typeLayout);

    _mainLayout->addWidget(filterGroup);
}

void FileBrowserPanel::setupTreeView() {
    _treeView = new QTreeView(this);
    _treeModel = new QStandardItemModel(this);
    _proxyModel = new FileBrowserProxyModel(this);

    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Source" << "Path" << "PRO Name");
    _proxyModel->setSourceModel(_treeModel);

    _treeView->setModel(_proxyModel);
    _treeView->setAlternatingRowColors(true);
    _treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    _treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    _treeView->setSortingEnabled(true);
    _treeView->sortByColumn(0, Qt::AscendingOrder);

    _treeView->setUniformRowHeights(true);
    _treeView->setAnimated(false);

    QHeaderView* header = _treeView->header();
    header->setStretchLastSection(true);                       // Let the last visible column stretch
    header->setSectionResizeMode(0, QHeaderView::Interactive); // Name column user-resizable
    header->resizeSection(0, 300);                             // Start with wider default for long filenames
    header->resizeSection(1, 80);                              // Type column fixed width
    header->resizeSection(2, 110);                             // Source column reasonable default
    header->setSectionResizeMode(2, QHeaderView::Interactive); // Source column user-resizable
    header->setSectionResizeMode(3, QHeaderView::Interactive); // Path column user-resizable when visible
    header->setSectionResizeMode(4, QHeaderView::Interactive); // PRO Name column user-resizable

    applyDefaultColumnVisibility();

    _treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    setupHeaderContextMenu();

    connect(_treeView, &QTreeView::clicked, this, &FileBrowserPanel::onTreeItemClicked);
    connect(_treeView, &QTreeView::doubleClicked, this, &FileBrowserPanel::onTreeItemDoubleClicked);
    connect(_treeView, &QTreeView::customContextMenuRequested, this, &FileBrowserPanel::onCustomContextMenuRequested);

    _mainLayout->addWidget(_treeView, 1);
}

void FileBrowserPanel::setupStatusBar() {
    QHBoxLayout* statusLayout = new QHBoxLayout();

    _statusLabel = new QLabel("Ready", this);
    _statusLabel->setStyleSheet(ui::theme::styles::smallLabel());
    statusLayout->addWidget(_statusLabel, 1);

    _progressBar = new QProgressBar(this);
    _progressBar->setMinimumWidth(120);
    _progressBar->setTextVisible(true);
    _progressBar->setStyleSheet(ui::theme::styles::progressBarStyle());
    _progressBar->setVisible(false);
    statusLayout->addWidget(_progressBar);

    _mainLayout->addLayout(statusLayout);
}

void FileBrowserPanel::loadFiles() {
    if (_isLoading) {
        stopLoading();
    }

    _isLoading = true;
    _allEntries.clear();
    _resolvedProNames.clear();
    _proNameItems.clear();
    _fileTypes.clear();
    _pendingEntries.clear();
    _currentChunkIndex = 0;

    _savedExpandedPaths = saveExpandedPaths();

    _treeModel->clear();
    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Source" << "Path" << "PRO Name");
    applyDefaultColumnVisibility();

    _progressBar->setVisible(true);
    _progressBar->setRange(0, 100);
    _progressBar->setValue(0);
    _statusLabel->setText("Starting file loading...");

    stopAndDestroyLoader();

    _loaderThread = new QThread(this);
    _loaderWorker = new FileLoaderWorker(_resourcesShared);
    _loaderWorker->setNativeDirectories(getNativeDirectoryPaths());
    _loaderWorker->moveToThread(_loaderThread);

    spdlog::debug("FileBrowserPanel: Connecting worker signals...");
    connect(_loaderThread, &QThread::started, _loaderWorker, &FileLoaderWorker::loadFiles);
    connect(_loaderWorker, &FileLoaderWorker::filesLoaded, this, &FileBrowserPanel::onFilesLoaded, Qt::QueuedConnection);
    connect(_loaderWorker, &FileLoaderWorker::proNamesResolved, this, &FileBrowserPanel::onProNamesResolved, Qt::QueuedConnection);
    connect(_loaderWorker, &FileLoaderWorker::fileTypesExtracted, this, &FileBrowserPanel::onFileTypesExtracted, Qt::QueuedConnection);
    connect(_loaderWorker, &FileLoaderWorker::loadingProgress, this, &FileBrowserPanel::onLoadingProgress, Qt::QueuedConnection);
    connect(_loaderWorker, &FileLoaderWorker::loadingError, this, &FileBrowserPanel::onLoadingError, Qt::QueuedConnection);
    connect(_loaderWorker, &FileLoaderWorker::loadingComplete, _loaderThread, &QThread::quit, Qt::QueuedConnection);

    spdlog::debug("FileBrowserPanel: Worker signals connected");
    spdlog::debug("FileBrowserPanel: Starting background file loading...");
    _loaderThread->start();
}

void FileBrowserPanel::stopLoading() {
    spdlog::debug("FileBrowserPanel: Stopping loading operations...");

    _isLoading = false;

    if (_loaderWorker) {
        _loaderWorker->_shouldStop.store(true);
    }

    if (_loaderThread && _loaderThread->isRunning()) {
        _loaderThread->quit();
    }

    if (_chunkTimer) {
        _chunkTimer->stop();
    }

    if (_searchTimer) {
        _searchTimer->stop();
    }

    if (_progressBar) {
        _progressBar->setVisible(false);
    }

    if (_statusLabel) {
        _statusLabel->setText("Loading stopped");
    }
}

void FileBrowserPanel::updateFileTypeComboBox() {
    QString currentSelection = _fileTypeComboBox->currentText();

    _fileTypeComboBox->clear();
    _fileTypeComboBox->addItem("All Files");

    std::vector<std::string> sortedTypes(_fileTypes.begin(), _fileTypes.end());
    std::sort(sortedTypes.begin(), sortedTypes.end());

    for (const auto& type : sortedTypes) {
        _fileTypeComboBox->addItem(QString::fromStdString(type));
    }

    int index = _fileTypeComboBox->findText(currentSelection);
    if (index >= 0) {
        _fileTypeComboBox->setCurrentIndex(index);
    }
}

void FileBrowserPanel::insertFileRow(FileTreeItem* rootItem, const FileBrowserEntry& entry) {
    QStringList pathComponents = entry.normalizedPath.split('/', Qt::SkipEmptyParts);
    if (pathComponents.isEmpty())
        return;

    FileTreeItem* currentParent = rootItem;
    for (int i = 0; i < pathComponents.size() - 1; ++i) {
        currentParent = findOrCreateDirectory(currentParent, pathComponents[i]);
    }

    FileTreeItem* fileItem = new FileTreeItem(pathComponents.last(), FileTreeItem::File);
    fileItem->setFilePath(entry.path);

    QStandardItem* typeItem = new QStandardItem(entry.extension);
    typeItem->setEditable(false);

    QStandardItem* sourceItem = new QStandardItem(entry.source);
    sourceItem->setEditable(false);

    QStandardItem* pathItem = new QStandardItem(entry.normalizedPath);
    pathItem->setEditable(false);

    QStandardItem* proNameItem = new QStandardItem();
    proNameItem->setEditable(false);
    if (entry.extension == QLatin1String(".pro")) {
        proNameItem->setText(_resolvedProNames.value(entry.path));
        _proNameItems.insert(entry.path, proNameItem);
    }

    currentParent->appendRow(QList<QStandardItem*>() << fileItem << typeItem << sourceItem << pathItem << proNameItem);
}

void FileBrowserPanel::onProNamesResolved(const QHash<QString, QString>& namesByPath) {
    for (auto it = namesByPath.constBegin(); it != namesByPath.constEnd(); ++it) {
        _resolvedProNames.insert(it.key(), it.value());
        if (QStandardItem* item = _proNameItems.value(it.key())) {
            item->setText(it.value());
        }
    }
}

void FileBrowserPanel::buildFileTree(const std::vector<FileBrowserEntry>& entries) {
    _proxyModel->setDynamicSortFilter(false); // bulk build: sort once at the end
    _proNameItems.clear();                    // the model clear below deletes the indexed items
    _treeModel->clear();
    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Source" << "Path" << "PRO Name");

    applyDefaultColumnVisibility();

    FileTreeItem* rootItem = static_cast<FileTreeItem*>(_treeModel->invisibleRootItem());

    for (const auto& entry : entries) {
        if (!_currentSearchFilter.isEmpty() && !entry.path.contains(_currentSearchFilter, Qt::CaseInsensitive)) {
            continue;
        }

        if (_currentFileTypeFilter != "All Files" && entry.extension != _currentFileTypeFilter) {
            continue;
        }

        insertFileRow(rootItem, entry);
    }

    for (int i = 0; i < _treeModel->rowCount(); ++i) {
        QModelIndex index = _treeModel->index(i, 0);
        _treeView->expand(index);
    }

    _proxyModel->setSourceModel(_treeModel);
    _proxyModel->sort(0, Qt::AscendingOrder);
    resizeNameColumnToContent();
}

FileTreeItem* FileBrowserPanel::findOrCreateDirectory(FileTreeItem* parent, const QString& dirName) {
    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* child = parent->child(i, 0);
        FileTreeItem* treeItem = static_cast<FileTreeItem*>(child);

        if (treeItem && treeItem->getType() == FileTreeItem::Directory && treeItem->text() == dirName) {
            return treeItem;
        }
    }

    FileTreeItem* dirItem = new FileTreeItem(dirName, FileTreeItem::Directory);
    QStandardItem* typeItem = new QStandardItem("Directory");
    typeItem->setEditable(false);
    QStandardItem* sourceItem = new QStandardItem("");
    sourceItem->setEditable(false);
    QStandardItem* pathItem = new QStandardItem("");
    pathItem->setEditable(false);
    QStandardItem* proNameItem = new QStandardItem("");
    proNameItem->setEditable(false);

    parent->appendRow(QList<QStandardItem*>() << dirItem << typeItem << sourceItem << pathItem << proNameItem);
    return dirItem;
}

QString FileBrowserPanel::getFileExtension(const QString& filePath) const {
    QFileInfo fileInfo(filePath);
    QString suffix = fileInfo.suffix().toLower();
    return suffix.isEmpty() ? QString() : "." + suffix;
}

QString FileBrowserPanel::getFileIcon(const QString& extension) const {
    return extension;
}

void FileBrowserPanel::updateFileCount() {
    int totalFiles = static_cast<int>(_allEntries.size());

    // With no filter active every file is visible; skip the recursive proxy walk, which is
    // not cheap over tens of thousands of rows.
    if (_currentSearchFilter.isEmpty() && _currentFileTypeFilter == "All Files") {
        _statusLabel->setText(QString("%1 files loaded").arg(totalFiles));
        return;
    }

    int visibleFiles = 0;

    std::function<void(const QModelIndex&)> countVisibleFiles = [&](const QModelIndex& parent) {
        int rowCount = _proxyModel->rowCount(parent);
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex proxyIndex = _proxyModel->index(i, 0, parent);
            if (proxyIndex.isValid()) {
                QModelIndex sourceIndex = _proxyModel->mapToSource(proxyIndex);
                QStandardItem* item = _treeModel->itemFromIndex(sourceIndex);
                if (item) {
                    FileTreeItem* treeItem = static_cast<FileTreeItem*>(item);
                    if (treeItem->getType() == FileTreeItem::File) {
                        visibleFiles++;
                    }
                }
                countVisibleFiles(proxyIndex);
            }
        }
    };

    countVisibleFiles(QModelIndex());

    if (!_currentSearchFilter.isEmpty() || _currentFileTypeFilter != "All Files") {
        _statusLabel->setText(QString("Showing %1 of %2 files")
                .arg(visibleFiles)
                .arg(totalFiles));
    } else {
        _statusLabel->setText(QString("%1 files loaded").arg(totalFiles));
    }
}

void FileBrowserPanel::refreshFileList() {
    spdlog::debug("FileBrowserPanel: Refreshing file list");
    loadFiles();
}

void FileBrowserPanel::setSearchFilter(const QString& filter) {
    _currentSearchFilter = filter;
    updateFileDisplay();
}

void FileBrowserPanel::setFileTypeFilter(const QString& fileType) {
    _currentFileTypeFilter = fileType;
    updateFileDisplay();
}

void FileBrowserPanel::onSearchTextChanged(const QString& text) {
    _currentSearchFilter = text;

    _searchTimer->stop();
    _searchTimer->start();
}

void FileBrowserPanel::performDebouncedSearch() {
    if (!_currentSearchFilter.isEmpty()) {
        QString escapedText = QRegularExpression::escape(_currentSearchFilter);
        QRegularExpression regex(escapedText, QRegularExpression::CaseInsensitiveOption);
        _proxyModel->setFilterRegularExpression(regex);
        expandFilteredItems();
    } else {
        _proxyModel->setFilterRegularExpression(QRegularExpression(""));
        _treeView->collapseAll();
        for (int i = 0; i < _proxyModel->rowCount(); ++i) {
            _treeView->expand(_proxyModel->index(i, 0));
        }
    }

    updateFileCount();
}

void FileBrowserPanel::onFileTypeChanged(const QString& fileType) {
    setFileTypeFilter(fileType);
}

void FileBrowserPanel::onRefreshClicked() {
    refreshFileList();
}

void FileBrowserPanel::onTreeItemClicked(const QModelIndex& index) {
    if (!index.isValid())
        return;

    QModelIndex sourceIndex = _proxyModel->mapToSource(index);
    QStandardItem* item = _treeModel->itemFromIndex(sourceIndex);
    FileTreeItem* treeItem = static_cast<FileTreeItem*>(item);

    if (treeItem && treeItem->getType() == FileTreeItem::File) {
        QString filePath = treeItem->getFilePath();
        spdlog::debug("FileBrowserPanel: File selected: {}", filePath.toStdString());
        Q_EMIT fileSelected(filePath);
    }
}

void FileBrowserPanel::onTreeItemDoubleClicked(const QModelIndex& index) {
    if (!index.isValid())
        return;

    if (_isLoading) {
        QMessageBox::information(this, "Loading in Progress",
            "Files are still being loaded. Please wait for loading to complete before opening files.");
        return;
    }

    QModelIndex sourceIndex = _proxyModel->mapToSource(index);

    // Get the FileTreeItem from column 0 regardless of which column was clicked
    QModelIndex fileItemIndex = sourceIndex.sibling(sourceIndex.row(), 0);
    QStandardItem* item = _treeModel->itemFromIndex(fileItemIndex);
    FileTreeItem* treeItem = static_cast<FileTreeItem*>(item);

    if (treeItem && treeItem->getType() == FileTreeItem::File) {
        QString filePath = treeItem->getFilePath();
        spdlog::debug("FileBrowserPanel: File double-clicked with path: '{}'", filePath.toStdString());

        if (filePath.endsWith(".pro", Qt::CaseInsensitive)) {
            spdlog::debug("FileBrowserPanel: Opening PRO editor for double-clicked file: {}", filePath.toStdString());
            openProEditor(filePath);
            return;
        }

        Q_EMIT fileDoubleClicked(filePath);
    }
}

void FileBrowserPanel::updateFileDisplay() {
    if (_isLoading) {
        return;
    }
    buildFileTree(_allEntries);
    updateFileCount();
}

void FileBrowserPanel::onFilesLoaded(const std::vector<FileBrowserEntry>& entries) {
    spdlog::debug("FileBrowserPanel::onFilesLoaded called with {} entries", entries.size());
    _allEntries = entries;

    buildFileTreeProgressive(_allEntries);
}

void FileBrowserPanel::onFileTypesExtracted(const std::unordered_set<std::string>& fileTypes) {
    spdlog::debug("FileBrowserPanel::onFileTypesExtracted called with {} types", fileTypes.size());
    _fileTypes = fileTypes;

    updateFileTypeComboBox();

    spdlog::debug("FileBrowserPanel: Extracted {} file types", fileTypes.size());
}

void FileBrowserPanel::onLoadingProgress(int current, int total, const QString& status) {
    if (total > 0) {
        _progressBar->setRange(0, total);
        _progressBar->setValue(current);
    }
    _statusLabel->setText(status);
}

void FileBrowserPanel::onLoadingError(const QString& error) {
    spdlog::error("FileBrowserPanel: Loading error: {}", error.toStdString());
    _statusLabel->setText("Error: " + error);
    _progressBar->setVisible(false);
    _isLoading = false;
}

void FileBrowserPanel::buildFileTreeProgressive(const std::vector<FileBrowserEntry>& entries) {
    std::vector<FileBrowserEntry> filteredEntries;
    for (const auto& entry : entries) {
        if (_currentFileTypeFilter != "All Files" && entry.extension != _currentFileTypeFilter) {
            continue;
        }
        filteredEntries.push_back(entry);
    }

    spdlog::debug("FileBrowserPanel: Starting progressive build of {} filtered files", filteredEntries.size());
    startProgressiveTreeBuild(std::move(filteredEntries));
}

void FileBrowserPanel::startProgressiveTreeBuild(std::vector<FileBrowserEntry> filteredEntries) {
    _populationStart = std::chrono::steady_clock::now();
    _pendingEntries = std::move(filteredEntries);
    _currentChunkIndex = 0;

    // Detach the model from the proxy for the bulk build: attached, every appendRow pays
    // proxy mapping (and, once a previous population has sorted the proxy, a sorted insert
    // with lessThan sibling lookups). Detached, rows cost only the QStandardItem work and
    // the proxy maps everything once on re-attach at completion.
    _proxyModel->setSourceModel(nullptr);

    _proNameItems.clear(); // the model clear below deletes the indexed items
    _treeModel->clear();
    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Source" << "Path" << "PRO Name");
    applyDefaultColumnVisibility();

    _chunkTimer->stop(); // a pending re-queue from a previous population must not double-pump
    _progressBar->setRange(0, static_cast<int>(_pendingEntries.size()));
    _progressBar->setValue(0);
    _statusLabel->setText("Building file tree...");

    processNextChunk();
}

void FileBrowserPanel::processNextChunk() {
    if (_currentChunkIndex >= _pendingEntries.size()) {
        _isLoading = false;
        _progressBar->setVisible(false);

        _proxyModel->setSourceModel(_treeModel);
        _proxyModel->sort(0, Qt::AscendingOrder);

        if (!_savedExpandedPaths.isEmpty()) {
            restoreExpandedPaths(_savedExpandedPaths);
            _savedExpandedPaths.clear();
        }

        resizeNameColumnToContent();
        updateFileCount();

        spdlog::info("FileBrowserPanel: built tree with {} rows in {}ms", _pendingEntries.size(),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _populationStart).count());
        return;
    }

    FileTreeItem* rootItem = static_cast<FileTreeItem*>(_treeModel->invisibleRootItem());

    // No nested processEvents here: a chunk is small (CHUNK_SIZE rows) and the queued
    // re-invoke below already yields to the real event loop between chunks. Re-entrant
    // event pumping from inside the chunk let other timers (notably the SFML render
    // loop) run long work re-entrantly, starving modal progress dialogs — a map load's
    // progress bar sat frozen until the load finished. It also let a settings change
    // delete or restart this panel mid-chunk; with the pump gone, nothing can touch the
    // population state while a chunk runs.
    const auto chunkStart = std::chrono::steady_clock::now();
    size_t endIndex = std::min(_currentChunkIndex + CHUNK_SIZE, _pendingEntries.size());
    for (size_t i = _currentChunkIndex; i < endIndex; ++i) {
        insertFileRow(rootItem, _pendingEntries[i]);
    }
    // A chunk is pure in-memory work and should take low milliseconds; if one is slow the
    // whole "Building tree" phase crawls, so make the culprit visible in the Log panel.
    const auto chunkMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - chunkStart).count();
    if (chunkMs > 250) {
        spdlog::warn("FileBrowserPanel: tree-build chunk {}..{} took {}ms", _currentChunkIndex, endIndex, chunkMs);
    }

    _currentChunkIndex = endIndex;
    _progressBar->setValue(static_cast<int>(_currentChunkIndex));
    _statusLabel->setText(QString("Building tree... %1/%2 files")
            .arg(_currentChunkIndex)
            .arg(_pendingEntries.size()));

    // Re-queue through the single-shot timer, NOT a queued invocation: a self-re-posting
    // event chain keeps the event loop from ever reaching its about-to-idle phase, which
    // on macOS is when widget paints flush — the progress label updated every chunk but
    // painted nothing until the build finished. A 1ms timer lets the loop sleep briefly
    // between chunks, so progress (and the rest of the UI) actually renders.
    _chunkTimer->start();
}

void FileBrowserPanel::onCustomContextMenuRequested(const QPoint& pos) {
    QModelIndex index = _treeView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    QModelIndex sourceIndex = _proxyModel->mapToSource(index);
    QModelIndex nameIndex = sourceIndex.sibling(sourceIndex.row(), 0);
    FileTreeItem* item = static_cast<FileTreeItem*>(_treeModel->itemFromIndex(nameIndex));
    if (!item) {
        return;
    }

    if (item->getType() != FileTreeItem::File) {
        return;
    }

    QMenu contextMenu(this);
    QString filePath = item->getFilePath();

    QAction* openAction = nullptr;
    QAction* editProAction = nullptr;
    QAction* executeScriptAction = nullptr;

    if (filePath.endsWith(".map", Qt::CaseInsensitive)) {
        openAction = contextMenu.addAction("Open Map");
        openAction->setIcon(createIcon(":/icons/filetypes/map.svg"));
    } else if (filePath.endsWith(".pro", Qt::CaseInsensitive)) {
        editProAction = contextMenu.addAction("Edit PRO");
        editProAction->setIcon(createIcon(":/icons/actions/settings.svg"));
        contextMenu.addSeparator();
        openAction = contextMenu.addAction("Open");
        openAction->setIcon(createIcon(":/icons/actions/open.svg"));
#ifdef GECK_SCRIPTING_ENABLED
    } else if (filePath.endsWith(".luau", Qt::CaseInsensitive) || filePath.endsWith(".lua", Qt::CaseInsensitive)) {
        executeScriptAction = contextMenu.addAction("Execute script");
        executeScriptAction->setIcon(createIcon(":/icons/filetypes/luau.svg"));
        contextMenu.addSeparator();
        openAction = contextMenu.addAction("Open with System Editor");
        openAction->setIcon(createIcon(":/icons/filetypes/text.svg"));
#endif
    } else if (isTextFile(filePath)) {
        openAction = contextMenu.addAction("Open with System Editor");
        openAction->setIcon(createIcon(":/icons/filetypes/text.svg"));
    } else {
        openAction = contextMenu.addAction("Open");
        openAction->setIcon(createIcon(":/icons/actions/open.svg"));
    }

    QAction* exportAction = contextMenu.addAction("Export");
    exportAction->setIcon(createIcon(":/icons/actions/save.svg"));

    QAction* selectedAction = contextMenu.exec(_treeView->viewport()->mapToGlobal(pos));
    if (!selectedAction) {
        return; // menu dismissed — guard the nullptr action comparisons below
    }

    if (selectedAction == openAction) {
        spdlog::debug("FileBrowserPanel: Open action triggered for: {}", filePath.toStdString());
        Q_EMIT fileDoubleClicked(filePath);
    } else if (selectedAction == executeScriptAction) {
        spdlog::debug("FileBrowserPanel: Execute script action triggered for: {}", filePath.toStdString());
        executeScript(filePath);
    } else if (selectedAction == editProAction) {
        spdlog::debug("FileBrowserPanel: Edit PRO action triggered for: {}", filePath.toStdString());
        openProEditor(filePath);
    } else if (selectedAction == exportAction) {
        spdlog::debug("FileBrowserPanel: Export action triggered for: {}", filePath.toStdString());
        exportFile(filePath);
    }
}

void FileBrowserPanel::exportFile(const QString& filePath) {
    try {
        _statusLabel->setText(QString("Exporting %1...").arg(filePath));

        QFileInfo fileInfo(filePath);
        QString defaultFileName = fileInfo.fileName();

        QString filter = QString("*%1;;All Files (*.*)").arg(fileInfo.suffix().isEmpty() ? "" : "." + fileInfo.suffix());
        QString saveFilePath = QtDialogs::saveFile(this, "Export File", filter);

        if (saveFilePath.isEmpty()) {
            _statusLabel->setText("Export cancelled");
            return;
        }

        QFileInfo saveInfo(saveFilePath);
        if (saveInfo.suffix().isEmpty() && !fileInfo.suffix().isEmpty()) {
            saveFilePath += "." + fileInfo.suffix();
        }

        auto buffer = _resourcesShared->files().readRawBytes(filePath.toStdString());
        if (!buffer) {
            QMessageBox::critical(this, "Export Error",
                QString("Failed to open file from data paths: %1").arg(filePath));
            _statusLabel->setText("Export failed: Could not open source file");
            return;
        }

        QFile outputFile(saveFilePath);
        if (!outputFile.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, "Export Error",
                QString("Failed to create output file: %1\n%2").arg(saveFilePath).arg(outputFile.errorString()));
            _statusLabel->setText("Export failed: Could not create output file");
            return;
        }

        qint64 bytesWritten = outputFile.write(reinterpret_cast<const char*>(buffer->data()), static_cast<qint64>(buffer->size()));
        outputFile.close();

        if (bytesWritten != static_cast<qint64>(buffer->size())) {
            QMessageBox::warning(this, "Export Warning",
                QString("File was partially written: %1 of %2 bytes").arg(bytesWritten).arg(buffer->size()));
        }

        _statusLabel->setText(QString("Exported %1 (%2 bytes)").arg(fileInfo.fileName()).arg(bytesWritten));
        spdlog::debug("FileBrowserPanel: Exported {} to {} ({} bytes)",
            filePath.toStdString(), saveFilePath.toStdString(), bytesWritten);

        Q_EMIT fileExportRequested(filePath);

    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export Error",
            QString("An error occurred during export: %1").arg(e.what()));
        _statusLabel->setText("Export failed");
        spdlog::error("FileBrowserPanel: Export failed for {}: {}", filePath.toStdString(), e.what());
    }
}

void FileBrowserPanel::executeScript(const QString& filePath) {
    auto buffer = _resourcesShared->files().readRawBytes(filePath.toStdString());
    if (!buffer) {
        QMessageBox::critical(this, "Execute Script",
            QString("Failed to read script from data paths: %1").arg(filePath));
        return;
    }

    const QString source = QString::fromUtf8(reinterpret_cast<const char*>(buffer->data()),
        static_cast<qsizetype>(buffer->size()));
    spdlog::debug("FileBrowserPanel: Loading script into the console: {}", filePath.toStdString());
    Q_EMIT executeScriptRequested(source);
}

void FileBrowserPanel::resizeNameColumnToContent() {
    if (!_treeView || !_treeModel) {
        return;
    }

    _treeView->resizeColumnToContents(0);

    QHeaderView* header = _treeView->header();
    if (header->sectionSize(0) > 500) {
        header->resizeSection(0, 500);
    }
}

QSet<QString> FileBrowserPanel::saveExpandedPaths() const {
    QSet<QString> expanded;
    if (!_treeView || !_proxyModel || !_treeModel) {
        return expanded;
    }

    std::function<void(const QModelIndex&, const QString&)> collect =
        [&](const QModelIndex& proxyParent, const QString& parentPath) {
            for (int i = 0; i < _proxyModel->rowCount(proxyParent); ++i) {
                QModelIndex proxyIndex = _proxyModel->index(i, 0, proxyParent);
                QModelIndex sourceIndex = _proxyModel->mapToSource(proxyIndex);
                auto* item = dynamic_cast<FileTreeItem*>(_treeModel->itemFromIndex(sourceIndex));
                if (!item || item->getType() != FileTreeItem::Directory) {
                    continue;
                }
                QString path = parentPath + "/" + item->text();
                if (_treeView->isExpanded(proxyIndex)) {
                    expanded.insert(path);
                    collect(proxyIndex, path);
                }
            }
        };

    collect(QModelIndex(), "");
    return expanded;
}

void FileBrowserPanel::restoreExpandedPaths(const QSet<QString>& paths) {
    if (paths.isEmpty() || !_treeView || !_proxyModel || !_treeModel) {
        return;
    }

    std::function<void(const QModelIndex&, const QString&)> restore =
        [&](const QModelIndex& proxyParent, const QString& parentPath) {
            for (int i = 0; i < _proxyModel->rowCount(proxyParent); ++i) {
                QModelIndex proxyIndex = _proxyModel->index(i, 0, proxyParent);
                QModelIndex sourceIndex = _proxyModel->mapToSource(proxyIndex);
                auto* item = dynamic_cast<FileTreeItem*>(_treeModel->itemFromIndex(sourceIndex));
                if (!item || item->getType() != FileTreeItem::Directory) {
                    continue;
                }
                QString path = parentPath + "/" + item->text();
                if (paths.contains(path)) {
                    _treeView->expand(proxyIndex);
                    restore(proxyIndex, path);
                }
            }
        };

    restore(QModelIndex(), "");
}

void FileBrowserPanel::openProEditor(const QString& filePath) {
    if (_isLoading) {
        QMessageBox::information(this, "Loading in Progress",
            "Files are still being loaded. Please wait for loading to complete before opening PRO files.");
        return;
    }

    try {
        _statusLabel->setText(QString("Loading PRO file: %1...").arg(filePath));

        auto buffer = _resourcesShared->files().readRawBytes(filePath.toStdString());
        if (!buffer) {
            QMessageBox::critical(this, "PRO Editor Error",
                QString("Failed to open PRO file from data paths: %1").arg(filePath));
            _statusLabel->setText("PRO Editor failed: Could not open PRO file");
            return;
        }
        auto pro = ReaderFactory::readFileFromMemory<Pro>(*buffer, filePath.toStdString());

        if (!pro) {
            QMessageBox::critical(this, "PRO Editor Error",
                "Failed to parse PRO file. It may be corrupted or in an unsupported format.");
            _statusLabel->setText("PRO Editor failed: Could not parse PRO file");

            return;
        }

        pro->setPath(std::filesystem::path(filePath.toStdString()));

        ProEditorDialog dialog(*_resourcesShared, std::shared_ptr<Pro>(pro.release()), this);
        dialog.exec();

        _statusLabel->setText("Ready");

    } catch (const std::exception& e) {
        QMessageBox::critical(this, "PRO Editor Error",
            QString("Failed to open PRO editor: %1").arg(e.what()));
        _statusLabel->setText("PRO Editor failed");
        spdlog::error("FileBrowserPanel::openProEditor failed: {}", e.what());
    }
}

void FileBrowserPanel::expandFilteredItems() {
    int totalVisibleRows = 0;

    std::function<void(const QModelIndex&)> countRows = [&](const QModelIndex& parent) {
        int rowCount = _proxyModel->rowCount(parent);
        totalVisibleRows += rowCount;
        if (totalVisibleRows > 200)
            return; // Stop counting if too many items

        for (int i = 0; i < rowCount && totalVisibleRows <= 200; ++i) {
            QModelIndex index = _proxyModel->index(i, 0, parent);
            if (index.isValid()) {
                countRows(index);
            }
        }
    };

    countRows(QModelIndex());

    if (totalVisibleRows > 200) {
        for (int i = 0; i < _proxyModel->rowCount(); ++i) {
            _treeView->expand(_proxyModel->index(i, 0));
        }
        return;
    }

    _treeView->setUpdatesEnabled(false);

    std::function<void(const QModelIndex&, int)> expandRecursive = [&](const QModelIndex& parent, int depth) {
        if (depth > 3)
            return; // Limit expansion depth to prevent UI freeze

        int rowCount = _proxyModel->rowCount(parent);
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex index = _proxyModel->index(i, 0, parent);
            if (index.isValid()) {
                _treeView->expand(index);
                expandRecursive(index, depth + 1);
            }
        }
    };

    expandRecursive(QModelIndex(), 0);

    _treeView->setUpdatesEnabled(true);
}

void FileBrowserPanel::setupHeaderContextMenu() {
    QHeaderView* header = _treeView->header();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QHeaderView::customContextMenuRequested, this, &FileBrowserPanel::showHeaderContextMenu);
}

void FileBrowserPanel::showHeaderContextMenu(const QPoint& pos) {
    QHeaderView* header = _treeView->header();
    QMenu contextMenu(this);

    QStringList columnNames = { "Name", "Type", "Source", "Path", "PRO Name" };

    for (int i = 0; i < columnNames.size(); ++i) {
        QAction* action = contextMenu.addAction(columnNames[i]);
        action->setCheckable(true);
        action->setChecked(!_treeView->isColumnHidden(i));
        action->setData(i);

        if (i == 0) {
            action->setEnabled(false);
        }

        connect(action, &QAction::triggered, [this, i]() {
            toggleColumnVisibility(i);
        });
    }

    contextMenu.exec(header->mapToGlobal(pos));
}

void FileBrowserPanel::toggleColumnVisibility(int column) {
    if (column == 0)
        return;

    bool currentlyHidden = _treeView->isColumnHidden(column);
    _treeView->setColumnHidden(column, !currentlyHidden);
}

void FileBrowserPanel::applyDefaultColumnVisibility() {
    for (int i = 0; i < 5; ++i) {
        _treeView->setColumnHidden(i, !DEFAULT_COLUMN_VISIBILITY[i]);
    }
}

std::vector<std::filesystem::path> FileBrowserPanel::getNativeDirectoryPaths() const {
    std::vector<std::filesystem::path> nativeDirectories;

    auto& settings = *_settings;
    auto dataPaths = settings.getDataPaths();

    for (const auto& path : dataPaths) {
        if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
            nativeDirectories.push_back(path);
        }
    }

    return nativeDirectories;
}

} // namespace geck
