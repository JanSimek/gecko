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
#include <spdlog/spdlog.h>
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

FileLoaderWorker::FileLoaderWorker(std::shared_ptr<resource::GameResources> resources, QObject* parent)
    : QObject(parent)
    , _resources(std::move(resources)) {
}

void FileLoaderWorker::loadFiles() {
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
        int processed = 0;
        const int totalFiles = static_cast<int>(allFiles.size());

        for (const auto& file : allFiles) {
            if (_shouldStop.load()) {
                return;
            }

            QString qFile = QString::fromStdString(file);
            QFileInfo fileInfo(qFile);
            QString suffix = fileInfo.suffix().toLower();
            if (!suffix.isEmpty()) {
                fileTypes.insert(("." + suffix).toStdString());
            }

            if (++processed % 1000 == 0) {
                int progressPercent = 50 + (processed * 50) / totalFiles;
                Q_EMIT loadingProgress(progressPercent, 100,
                    QString("Processing files... %1/%2").arg(processed).arg(totalFiles));
            }
        }

        Q_EMIT loadingProgress(100, 100, "File loading completed");
        spdlog::debug("FileLoaderWorker: Emitting fileTypesExtracted with {} types", fileTypes.size());
        Q_EMIT fileTypesExtracted(fileTypes);
        spdlog::debug("FileLoaderWorker: Emitting filesLoaded with {} files", allFiles.size());
        Q_EMIT filesLoaded(allFiles);

        spdlog::debug("FileLoaderWorker: Loaded {} files with {} file types",
            allFiles.size(), fileTypes.size());

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
    _allFiles.clear();
    _fileTypes.clear();
    _pendingFiles.clear();
    _currentChunkIndex = 0;
    _nativeDirectoriesForSources = getNativeDirectoryPaths();

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
    _loaderWorker->moveToThread(_loaderThread);

    spdlog::debug("FileBrowserPanel: Connecting worker signals...");
    connect(_loaderThread, &QThread::started, _loaderWorker, &FileLoaderWorker::loadFiles);
    connect(_loaderWorker, &FileLoaderWorker::filesLoaded, this, &FileBrowserPanel::onFilesLoaded, Qt::QueuedConnection);
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

void FileBrowserPanel::insertFileRow(FileTreeItem* rootItem, const std::string& file,
    const std::vector<std::filesystem::path>& nativeDirectories) {
    QString qFile = QString::fromStdString(file);

    QString normalizedPath = normalizeDisplayPath(qFile);
    QStringList pathComponents = normalizedPath.split('/', Qt::SkipEmptyParts);
    if (pathComponents.isEmpty())
        return;

    FileTreeItem* currentParent = rootItem;
    for (int i = 0; i < pathComponents.size() - 1; ++i) {
        currentParent = findOrCreateDirectory(currentParent, pathComponents[i]);
    }

    QString fileName = pathComponents.last();
    FileTreeItem* fileItem = new FileTreeItem(fileName, FileTreeItem::File);
    fileItem->setFilePath(qFile);

    QString extension = getFileExtension(fileName);
    QStandardItem* typeItem = new QStandardItem(extension);
    typeItem->setEditable(false);

    QStandardItem* sourceItem = new QStandardItem(getFileSource(qFile, nativeDirectories));
    sourceItem->setEditable(false);

    QStandardItem* pathItem = new QStandardItem(normalizeDisplayPath(qFile));
    pathItem->setEditable(false);

    QString proName = (extension.toLower() == ".pro") ? getProName(qFile) : QString();
    QStandardItem* proNameItem = new QStandardItem(proName);
    proNameItem->setEditable(false);

    currentParent->appendRow(QList<QStandardItem*>() << fileItem << typeItem << sourceItem << pathItem << proNameItem);
}

void FileBrowserPanel::buildFileTree(const std::vector<std::string>& files) {
    _treeModel->clear();
    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Source" << "Path" << "PRO Name");

    applyDefaultColumnVisibility();

    FileTreeItem* rootItem = static_cast<FileTreeItem*>(_treeModel->invisibleRootItem());

    std::vector<std::string> filteredFiles;
    for (const auto& file : files) {
        QString qFile = QString::fromStdString(file);

        if (!_currentSearchFilter.isEmpty()) {
            if (!qFile.contains(_currentSearchFilter, Qt::CaseInsensitive)) {
                continue;
            }
        }

        if (_currentFileTypeFilter != "All Files") {
            QString extension = getFileExtension(qFile);
            if (extension != _currentFileTypeFilter) {
                continue;
            }
        }

        filteredFiles.push_back(file);
    }

    const auto nativeDirectories = _nativeDirectoriesForSources.empty() ? getNativeDirectoryPaths() : _nativeDirectoriesForSources;

    for (const auto& file : filteredFiles) {
        insertFileRow(rootItem, file, nativeDirectories);
    }

    for (int i = 0; i < _treeModel->rowCount(); ++i) {
        QModelIndex index = _treeModel->index(i, 0);
        _treeView->expand(index);
    }

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
    int totalFiles = static_cast<int>(_allFiles.size());
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
    buildFileTree(_allFiles);
    updateFileCount();
}

void FileBrowserPanel::onFilesLoaded(const std::vector<std::string>& files) {
    spdlog::debug("FileBrowserPanel::onFilesLoaded called with {} files", files.size());
    _allFiles = files;
    spdlog::debug("FileBrowserPanel: Received {} files from background loader", files.size());

    buildFileTreeProgressive(files);
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

void FileBrowserPanel::buildFileTreeProgressive(const std::vector<std::string>& files) {
    std::vector<std::string> filteredFiles;
    for (const auto& file : files) {
        QString qFile = QString::fromStdString(file);

        if (_currentFileTypeFilter != "All Files") {
            QString extension = getFileExtension(qFile);
            if (extension != _currentFileTypeFilter) {
                continue;
            }
        }

        filteredFiles.push_back(file);
    }

    spdlog::debug("FileBrowserPanel: Starting progressive build of {} filtered files", filteredFiles.size());
    startProgressiveTreeBuild(filteredFiles);
}

void FileBrowserPanel::startProgressiveTreeBuild(const std::vector<std::string>& filteredFiles) {
    _pendingFiles = filteredFiles;
    _currentChunkIndex = 0;

    _treeModel->clear();
    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Source" << "Path" << "PRO Name");
    applyDefaultColumnVisibility();

    _progressBar->setRange(0, static_cast<int>(_pendingFiles.size()));
    _progressBar->setValue(0);
    _statusLabel->setText("Building file tree...");

    processNextChunk();
}

void FileBrowserPanel::processNextChunk() {
    if (_currentChunkIndex >= _pendingFiles.size()) {
        _isLoading = false;
        _progressBar->setVisible(false);

        _proxyModel->sort(0, Qt::AscendingOrder);

        if (!_savedExpandedPaths.isEmpty()) {
            restoreExpandedPaths(_savedExpandedPaths);
            _savedExpandedPaths.clear();
        }

        resizeNameColumnToContent();
        updateFileCount();

        spdlog::debug("FileBrowserPanel: Progressive tree building completed");
        return;
    }

    FileTreeItem* rootItem = static_cast<FileTreeItem*>(_treeModel->invisibleRootItem());
    const auto nativeDirectories = _nativeDirectoriesForSources.empty() ? getNativeDirectoryPaths() : _nativeDirectoriesForSources;

    // No nested processEvents here: a chunk is small (CHUNK_SIZE rows) and the queued
    // re-invoke below already yields to the real event loop between chunks. Re-entrant
    // event pumping from inside the chunk let other timers (notably the SFML render
    // loop) run long work re-entrantly, starving modal progress dialogs — a map load's
    // progress bar sat frozen until the load finished.
    size_t endIndex = std::min(_currentChunkIndex + CHUNK_SIZE, _pendingFiles.size());
    for (size_t i = _currentChunkIndex; i < endIndex; ++i) {
        insertFileRow(rootItem, _pendingFiles[i], nativeDirectories);
    }

    _currentChunkIndex = endIndex;
    _progressBar->setValue(static_cast<int>(_currentChunkIndex));
    _statusLabel->setText(QString("Building tree... %1/%2 files")
            .arg(_currentChunkIndex)
            .arg(_pendingFiles.size()));

    QMetaObject::invokeMethod(this, &FileBrowserPanel::processNextChunk, Qt::QueuedConnection);
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

QString FileBrowserPanel::getProName(const QString& filePath) const {
    std::string stdPath = filePath.toStdString();

    auto cacheIt = _proNameCache.find(stdPath);
    if (cacheIt != _proNameCache.end()) {
        return cacheIt->second;
    }

    QString proName = loadProNameFromFile(filePath);
    _proNameCache[stdPath] = proName;

    return proName;
}

QString FileBrowserPanel::loadProNameFromFile(const QString& filePath) const {
    try {
        std::string stdPath = filePath.toStdString();
        if (stdPath.front() == '/') {
            stdPath = stdPath.substr(1);
        }

        if (!_resourcesShared->files().exists(stdPath)) {
            return QString("File not found");
        }

        const auto* pro = _resourcesShared->repository().load<Pro>(stdPath);
        if (!pro) {
            return QString("Failed to load");
        }

        const auto* msgFile = ProHelper::msgFile(*_resourcesShared, pro->type());
        if (!msgFile) {
            return QString("MSG not found");
        }

        uint32_t messageId = pro->header.message_id;

        try {
            const auto& nameMessage = const_cast<Msg*>(msgFile)->message(messageId);
            QString name = QString::fromStdString(nameMessage.text);
            return name.isEmpty() ? QString("No name (ID: %1)").arg(messageId) : name;
        } catch ([[maybe_unused]] const std::exception& e) {
            spdlog::warn("Failed to resolve PRO name for message {}: {}", messageId, e.what());
            return QString("No name (ID: %1)").arg(messageId);
        }

    } catch ([[maybe_unused]] const std::exception& e) {
        spdlog::error("Failed to read PRO metadata for '{}': {}", filePath.toStdString(), e.what());
        return QString("Error: %1").arg(e.what());
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

QString FileBrowserPanel::getFileSource(const QString& filePath) const {
    return getFileSource(filePath, getNativeDirectoryPaths());
}

QString FileBrowserPanel::getFileSource(const QString& filePath, const std::vector<std::filesystem::path>& nativeDirectories) const {
    Q_UNUSED(nativeDirectories);

    if (auto source = _resourcesShared->files().sourceInfo(filePath.toStdString())) {
        return QString::fromStdString(source->displayLabel);
    }

    return QStringLiteral("Unknown");
}

QString FileBrowserPanel::normalizeDisplayPath(const QString& fullPath) const {
    auto nativeDirectories = getNativeDirectoryPaths();
    std::string fullPathStr = fullPath.toStdString();

    // VFS may return paths with double leading slashes, so handle both forms
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

} // namespace geck
