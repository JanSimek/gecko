#include "FileBrowserPanel.h"
#include "../../util/ResourceManager.h"
#include "../../util/QtDialogs.h"
#include "../../util/PathUtils.h"
#include "../dialogs/ProEditorDialog.h"
#include "../../reader/pro/ProReader.h"
#include "../../reader/ReaderFactory.h"
#include "../../format/pro/Pro.h"
#include "../../format/msg/Msg.h"

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
#include <QIcon>
#include <QProgressBar>
#include <QThread>
#include <QMetaObject>
#include <QApplication>
#include <QRegularExpression>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <util/ProHelper.h>
#include <vfspp/VirtualFileSystem.hpp>

namespace geck {

// FileBrowserProxyModel implementation
FileBrowserProxyModel::FileBrowserProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setRecursiveFilteringEnabled(true);
}

bool FileBrowserProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    if (filterRegularExpression().pattern().isEmpty()) {
        return true; // Accept all rows when no filter is set
    }

    QModelIndex sourceModel = this->sourceModel()->index(sourceRow, 0, sourceParent);
    if (!sourceModel.isValid()) {
        return false;
    }

    // Get the filename from column 0 (Name column)
    QString fileName = sourceModel.data(Qt::DisplayRole).toString();
    
    // Get the PRO name from column 3 (PRO Name column)
    QModelIndex proNameIndex = this->sourceModel()->index(sourceRow, 3, sourceParent);
    QString proName = proNameIndex.isValid() ? proNameIndex.data(Qt::DisplayRole).toString() : QString();
    
    // Check if either filename or PRO name matches the filter
    bool fileNameMatches = fileName.contains(filterRegularExpression());
    bool proNameMatches = !proName.isEmpty() && proName.contains(filterRegularExpression());
    
    return fileNameMatches || proNameMatches;
}

bool FileBrowserProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    // The left and right indexes are already from the source model
    if (!left.isValid() || !right.isValid()) {
        return QSortFilterProxyModel::lessThan(left, right);
    }
    
    // Get the type from column 1 (Type column)
    QModelIndex leftTypeIndex = left.sibling(left.row(), 1);
    QModelIndex rightTypeIndex = right.sibling(right.row(), 1);
    
    QString leftType = leftTypeIndex.data(Qt::DisplayRole).toString();
    QString rightType = rightTypeIndex.data(Qt::DisplayRole).toString();
    
    // Sort directories before files
    bool leftIsDir = (leftType == "Directory");
    bool rightIsDir = (rightType == "Directory");
    
    if (leftIsDir && !rightIsDir) {
        return true;  // Directory comes before file
    }
    if (!leftIsDir && rightIsDir) {
        return false; // File comes after directory
    }
    
    // Both are directories or both are files - use default alphabetical comparison
    return QSortFilterProxyModel::lessThan(left, right);
}

// FileLoaderWorker implementation
FileLoaderWorker::FileLoaderWorker(QObject* parent)
    : QObject(parent) {
}

void FileLoaderWorker::loadFiles() {
    try {
        spdlog::info("FileLoaderWorker: Starting background file loading...");
        emit loadingProgress(0, 100, "Initializing file system...");

        // Get files from ResourceManager
        auto& resourceManager = ResourceManager::getInstance();
        spdlog::debug("FileLoaderWorker: Calling ResourceManager::listAllFiles()...");
        auto allFiles = resourceManager.listAllFiles();
        spdlog::debug("FileLoaderWorker: Got {} files from ResourceManager", allFiles.size());
        
        if (allFiles.empty()) {
            spdlog::warn("FileLoaderWorker: ResourceManager returned no files - data may not be loaded yet");
            emit loadingError("No files found - data paths may not be loaded yet");
            emit loadingComplete();
            return;
        }
        
        emit loadingProgress(50, 100, "Processing file list...");
        
        if (_shouldStop.load()) {
            return;
        }

        // Extract file types for filter dropdown
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
            
            // Update progress every 1000 files
            if (++processed % 1000 == 0) {
                int progressPercent = 50 + (processed * 50) / totalFiles;
                emit loadingProgress(progressPercent, 100, 
                    QString("Processing files... %1/%2").arg(processed).arg(totalFiles));
            }
        }

        emit loadingProgress(100, 100, "File loading completed");
        spdlog::debug("FileLoaderWorker: Emitting fileTypesExtracted with {} types", fileTypes.size());
        emit fileTypesExtracted(fileTypes);
        spdlog::debug("FileLoaderWorker: Emitting filesLoaded with {} files", allFiles.size());
        emit filesLoaded(allFiles);
        
        spdlog::info("FileLoaderWorker: Loaded {} files with {} file types", 
                     allFiles.size(), fileTypes.size());
        
        // Signal that work is complete
        emit loadingComplete();

    } catch (const std::exception& e) {
        spdlog::error("FileLoaderWorker: Error loading files: {}", e.what());
        emit loadingError(QString::fromStdString(e.what()));
        emit loadingComplete();
    }
}

// FileTreeItem implementation
FileTreeItem::FileTreeItem(const QString& name, ItemType type)
    : QStandardItem(name)
    , _type(type) {
    setEditable(false);

    // Set different icons for files and directories
    if (type == Directory) {
        setIcon(QIcon(":/icons/filetypes/folder.svg"));
    } else {
        setIcon(getFileIcon(name));
    }
}

QIcon FileTreeItem::getFileIcon(const QString& fileName) {
    QFileInfo fileInfo(fileName);
    QString suffix = fileInfo.suffix().toLower();
    
    // Map file extensions to icon paths
    static const QMap<QString, QString> iconMap = {
        {"map", ":/icons/filetypes/map.svg"},
        {"frm", ":/icons/filetypes/image.svg"},
        {"pro", ":/icons/filetypes/proto.svg"},
        {"msg", ":/icons/filetypes/message.svg"},
        {"dat", ":/icons/filetypes/data.svg"},
        {"lst", ":/icons/filetypes/list.svg"},
        {"int", ":/icons/filetypes/script.svg"},
        {"ssl", ":/icons/filetypes/script.svg"},
        {"txt", ":/icons/filetypes/text.svg"},
        {"cfg", ":/icons/filetypes/text.svg"},
        {"gam", ":/icons/filetypes/text.svg"},
        {"ini", ":/icons/filetypes/text.svg"},
        {"pal", ":/icons/filetypes/palette.svg"}
    };
    
    if (iconMap.contains(suffix)) {
        return QIcon(iconMap[suffix]);
    }
    
    return QIcon(":/icons/filetypes/default.svg");
}

bool FileBrowserPanel::isTextFile(const QString& filePath) const {
    static const QStringList textExtensions = {
        "cfg", "txt", "gam", "msg", "lst", "int", "ssl", "ini"
    };
    QString suffix = QFileInfo(filePath).suffix().toLower();
    return textExtensions.contains(suffix);
}

// FileBrowserPanel implementation
FileBrowserPanel::FileBrowserPanel(QWidget* parent)
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
    , _isLoading(false) {

    setupUI();

    // Setup chunk processing timer
    _chunkTimer->setSingleShot(true);
    _chunkTimer->setInterval(CHUNK_DELAY_MS);
    connect(_chunkTimer, &QTimer::timeout, this, &FileBrowserPanel::processNextChunk);

    // Set initial status
    _statusLabel->setText("Ready - Click Refresh to load files");
}

FileBrowserPanel::~FileBrowserPanel() {
    spdlog::debug("FileBrowserPanel: Destructor called, cleaning up threads...");
    
    // First, signal the worker to stop
    if (_loaderWorker) {
        _loaderWorker->_shouldStop.store(true);
    }
    
    // Stop timers
    
    if (_chunkTimer) {
        _chunkTimer->stop();
    }
    
    // Handle thread cleanup carefully
    if (_loaderThread) {
        // Disconnect signals to prevent any further processing
        if (_loaderWorker) {
            disconnect(_loaderWorker, nullptr, this, nullptr);
        }
        disconnect(_loaderThread, nullptr, this, nullptr);
        
        // Only try to quit if the thread is still running
        if (_loaderThread->isRunning()) {
            _loaderThread->quit();
            // Wait for thread to finish with a reasonable timeout
            if (!_loaderThread->wait(2000)) {
                spdlog::warn("FileBrowserPanel: Thread didn't finish within 2 seconds, forcing termination");
                _loaderThread->terminate();
                _loaderThread->wait(500);
            }
        }
        
        // Clean up the thread object
        delete _loaderThread;
        _loaderThread = nullptr;
    }
    
    // Clean up worker
    if (_loaderWorker) {
        delete _loaderWorker;
        _loaderWorker = nullptr;
    }
    
    spdlog::debug("FileBrowserPanel: Destructor completed");
}

void FileBrowserPanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(4, 4, 4, 4);
    _mainLayout->setSpacing(4);

    setupFilterControls();
    setupTreeView();
    setupStatusBar();

    setLayout(_mainLayout);
}

void FileBrowserPanel::setupFilterControls() {
    QGroupBox* filterGroup = new QGroupBox("Filters", this);
    QVBoxLayout* filterGroupLayout = new QVBoxLayout(filterGroup);

    // Search filter
    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:", this));

    _searchLineEdit = new QLineEdit(this);
    _searchLineEdit->setPlaceholderText("Type to search in all files instantly...");
    _searchLineEdit->setClearButtonEnabled(true);
    connect(_searchLineEdit, &QLineEdit::textChanged, this, &FileBrowserPanel::onSearchTextChanged);
    searchLayout->addWidget(_searchLineEdit);

    filterGroupLayout->addLayout(searchLayout);

    // File type filter and refresh
    QHBoxLayout* typeLayout = new QHBoxLayout();
    typeLayout->addWidget(new QLabel("Type:", this));

    _fileTypeComboBox = new QComboBox(this);
    _fileTypeComboBox->addItem("All Files");
    connect(_fileTypeComboBox, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
        this, &FileBrowserPanel::onFileTypeChanged);
    typeLayout->addWidget(_fileTypeComboBox, 1);

    _refreshButton = new QPushButton("Refresh", this);
    _refreshButton->setIcon(QIcon(":/icons/ui/refresh.svg"));
    connect(_refreshButton, &QPushButton::clicked, this, &FileBrowserPanel::onRefreshClicked);
    typeLayout->addWidget(_refreshButton);

    filterGroupLayout->addLayout(typeLayout);

    _mainLayout->addWidget(filterGroup);
}

void FileBrowserPanel::setupTreeView() {
    _treeView = new QTreeView(this);
    _treeModel = new QStandardItemModel(this);
    _proxyModel = new FileBrowserProxyModel(this);

    // Set up model headers
    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Path" << "PRO Name");

    // Configure proxy model
    _proxyModel->setSourceModel(_treeModel);

    _treeView->setModel(_proxyModel);
    _treeView->setAlternatingRowColors(true);
    _treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    _treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    _treeView->setSortingEnabled(true);
    _treeView->sortByColumn(0, Qt::AscendingOrder);
    
    // Optimize performance
    _treeView->setUniformRowHeights(true);
    _treeView->setAnimated(false);

    // Configure headers
    QHeaderView* header = _treeView->header();
    header->setStretchLastSection(true);  // Let the last visible column stretch
    header->setSectionResizeMode(0, QHeaderView::Interactive);     // Name column user-resizable
    header->resizeSection(0, 300);                                 // Start with wider default for long filenames
    header->resizeSection(1, 80);                                  // Type column fixed width
    header->setSectionResizeMode(2, QHeaderView::Interactive);     // Path column user-resizable when visible
    header->setSectionResizeMode(3, QHeaderView::Interactive);     // PRO Name column user-resizable

    // Apply default column visibility BEFORE setting up context menu
    applyDefaultColumnVisibility();

    // Enable context menu
    _treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Setup header context menu for column visibility
    setupHeaderContextMenu();
    
    // Connect signals
    connect(_treeView, &QTreeView::clicked, this, &FileBrowserPanel::onTreeItemClicked);
    connect(_treeView, &QTreeView::doubleClicked, this, &FileBrowserPanel::onTreeItemDoubleClicked);
    connect(_treeView, &QTreeView::customContextMenuRequested, this, &FileBrowserPanel::onCustomContextMenuRequested);

    // Add tree view to main layout
    _mainLayout->addWidget(_treeView, 1);
}

void FileBrowserPanel::setupStatusBar() {
    // Create horizontal layout for status bar
    QHBoxLayout* statusLayout = new QHBoxLayout();
    
    _statusLabel = new QLabel("Ready", this);
    _statusLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; }");
    statusLayout->addWidget(_statusLabel, 1);
    
    _progressBar = new QProgressBar(this);
    _progressBar->setMaximumHeight(16);
    _progressBar->setVisible(false);
    statusLayout->addWidget(_progressBar);
    
    // Add the layout to main layout
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

    // Clear existing tree
    _treeModel->clear();
    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Path" << "PRO Name");
    
    // Reapply column visibility after clearing
    applyDefaultColumnVisibility();

    // Show progress bar and update status
    _progressBar->setVisible(true);
    _progressBar->setRange(0, 100);
    _progressBar->setValue(0);
    _statusLabel->setText("Starting file loading...");

    // Clean up previous thread and worker if they exist
    if (_loaderThread) {
        if (_loaderWorker) {
            _loaderWorker->_shouldStop.store(true);
        }
        
        if (_loaderThread->isRunning()) {
            _loaderThread->quit();
            _loaderThread->wait(3000);
        }
        
        // Clean up old worker and thread
        if (_loaderWorker) {
            _loaderWorker->deleteLater();
            _loaderWorker = nullptr;
        }
        
        _loaderThread->deleteLater();
        _loaderThread = nullptr;
    }

    // Create new worker thread
    _loaderThread = new QThread(this);
    _loaderWorker = new FileLoaderWorker();
    _loaderWorker->moveToThread(_loaderThread);

    // Connect signals with explicit Qt::QueuedConnection for cross-thread communication
    spdlog::debug("FileBrowserPanel: Connecting worker signals...");
    connect(_loaderThread, &QThread::started, _loaderWorker, &FileLoaderWorker::loadFiles);
    connect(_loaderWorker, &FileLoaderWorker::filesLoaded, this, &FileBrowserPanel::onFilesLoaded, Qt::QueuedConnection);
    connect(_loaderWorker, &FileLoaderWorker::fileTypesExtracted, this, &FileBrowserPanel::onFileTypesExtracted, Qt::QueuedConnection);
    connect(_loaderWorker, &FileLoaderWorker::loadingProgress, this, &FileBrowserPanel::onLoadingProgress, Qt::QueuedConnection);
    connect(_loaderWorker, &FileLoaderWorker::loadingError, this, &FileBrowserPanel::onLoadingError, Qt::QueuedConnection);
    connect(_loaderWorker, &FileLoaderWorker::loadingComplete, _loaderThread, &QThread::quit, Qt::QueuedConnection);
    
    // Don't use deleteLater for thread/worker - we'll manage cleanup manually
    // This prevents crashes during destruction
    
    spdlog::debug("FileBrowserPanel: Worker signals connected");

    // Start loading
    spdlog::info("FileBrowserPanel: Starting background file loading...");
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
        // Don't wait here - let the finished signal handle cleanup
    }
    
    if (_chunkTimer) {
        _chunkTimer->stop();
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

    // Add file types in sorted order
    std::vector<std::string> sortedTypes(_fileTypes.begin(), _fileTypes.end());
    std::sort(sortedTypes.begin(), sortedTypes.end());

    for (const auto& type : sortedTypes) {
        _fileTypeComboBox->addItem(QString::fromStdString(type));
    }

    // Restore previous selection if it still exists
    int index = _fileTypeComboBox->findText(currentSelection);
    if (index >= 0) {
        _fileTypeComboBox->setCurrentIndex(index);
    }
}

void FileBrowserPanel::buildFileTree(const std::vector<std::string>& files) {
    _treeModel->clear();
    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Path" << "PRO Name");
    
    // Reapply column visibility after clearing
    applyDefaultColumnVisibility();

    FileTreeItem* rootItem = static_cast<FileTreeItem*>(_treeModel->invisibleRootItem());

    // Apply filters
    std::vector<std::string> filteredFiles;
    for (const auto& file : files) {
        QString qFile = QString::fromStdString(file);

        // Apply search filter
        if (!_currentSearchFilter.isEmpty()) {
            if (!qFile.contains(_currentSearchFilter, Qt::CaseInsensitive)) {
                continue;
            }
        }

        // Apply file type filter
        if (_currentFileTypeFilter != "All Files") {
            QString extension = getFileExtension(qFile);
            if (extension != _currentFileTypeFilter) {
                continue;
            }
        }

        filteredFiles.push_back(file);
    }

    // Build tree structure
    for (const auto& file : filteredFiles) {
        QString qFile = QString::fromStdString(file);

        // Split path into components
        QStringList pathComponents = qFile.split('/', Qt::SkipEmptyParts);
        if (pathComponents.isEmpty())
            continue;

        FileTreeItem* currentParent = rootItem;
        QString currentPath = "";

        // Create directory structure
        for (int i = 0; i < pathComponents.size() - 1; ++i) {
            currentPath += "/" + pathComponents[i];
            currentParent = findOrCreateDirectory(currentParent, pathComponents[i]);
        }

        // Add file
        QString fileName = pathComponents.last();
        FileTreeItem* fileItem = new FileTreeItem(fileName, FileTreeItem::File);
        fileItem->setFilePath(qFile);

        QString extension = getFileExtension(fileName);
        QStandardItem* typeItem = new QStandardItem(extension);
        typeItem->setEditable(false);

        QStandardItem* pathItem = new QStandardItem(qFile);
        pathItem->setEditable(false);
        
        // Add PRO name for .pro files
        QString proName = (extension.toLower() == ".pro") ? getProName(qFile) : QString();
        QStandardItem* proNameItem = new QStandardItem(proName);
        proNameItem->setEditable(false);

        currentParent->appendRow(QList<QStandardItem*>() << fileItem << typeItem << pathItem << proNameItem);
    }

    // Expand first level directories
    for (int i = 0; i < _treeModel->rowCount(); ++i) {
        QModelIndex index = _treeModel->index(i, 0);
        _treeView->expand(index);
    }
    
    // Trigger sorting to ensure proper directory/file order
    _proxyModel->sort(0, Qt::AscendingOrder);
    
    // Resize Name column to fit content after tree is built and expanded
    resizeNameColumnToContent();
}

FileTreeItem* FileBrowserPanel::findOrCreateDirectory(FileTreeItem* parent, const QString& dirName) {
    // Check if directory already exists
    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* child = parent->child(i, 0);
        FileTreeItem* treeItem = static_cast<FileTreeItem*>(child);

        if (treeItem && treeItem->getType() == FileTreeItem::Directory && treeItem->text() == dirName) {
            return treeItem;
        }
    }

    // Create new directory
    FileTreeItem* dirItem = new FileTreeItem(dirName, FileTreeItem::Directory);
    QStandardItem* typeItem = new QStandardItem("Directory");
    typeItem->setEditable(false);
    QStandardItem* pathItem = new QStandardItem("");
    pathItem->setEditable(false);
    QStandardItem* proNameItem = new QStandardItem("");
    proNameItem->setEditable(false);

    parent->appendRow(QList<QStandardItem*>() << dirItem << typeItem << pathItem << proNameItem);
    return dirItem;
}

QString FileBrowserPanel::getFileExtension(const QString& filePath) const {
    QFileInfo fileInfo(filePath);
    QString suffix = fileInfo.suffix().toLower();
    return suffix.isEmpty() ? QString() : "." + suffix;
}

QString FileBrowserPanel::getFileIcon(const QString& extension) const {
    // This could be expanded to return different icons for different file types
    return extension;
}

void FileBrowserPanel::updateFileCount() {
    int totalFiles = static_cast<int>(_allFiles.size());
    int visibleFiles = 0;

    // Count visible files in the filtered proxy model
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
                // Recursively count in subdirectories
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
    spdlog::info("FileBrowserPanel: Refreshing file list");
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
    
    // Apply filter immediately using proxy model
    if (!text.isEmpty()) {
        // Escape special regex characters and use case-insensitive search
        QString escapedText = QRegularExpression::escape(text);
        QRegularExpression regex(escapedText, QRegularExpression::CaseInsensitiveOption);
        _proxyModel->setFilterRegularExpression(regex);
        
        // Auto-expand all filtered items to show search results
        expandFilteredItems();
    } else {
        _proxyModel->setFilterRegularExpression(QRegularExpression(""));
        
        // When search is cleared, collapse all and expand only first level
        _treeView->collapseAll();
        for (int i = 0; i < _proxyModel->rowCount(); ++i) {
            _treeView->expand(_proxyModel->index(i, 0));
        }
    }
    
    // Update file count after filtering
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

    // Map proxy index to source index
    QModelIndex sourceIndex = _proxyModel->mapToSource(index);
    QStandardItem* item = _treeModel->itemFromIndex(sourceIndex);
    FileTreeItem* treeItem = static_cast<FileTreeItem*>(item);

    if (treeItem && treeItem->getType() == FileTreeItem::File) {
        QString filePath = treeItem->getFilePath();
        spdlog::debug("FileBrowserPanel: File selected: {}", filePath.toStdString());
        emit fileSelected(filePath);
    }
}

void FileBrowserPanel::onTreeItemDoubleClicked(const QModelIndex& index) {
    if (!index.isValid())
        return;

    // Map proxy index to source index
    QModelIndex sourceIndex = _proxyModel->mapToSource(index);
    
    // Always get the FileTreeItem from column 0 (first column), regardless of which column was clicked
    QModelIndex fileItemIndex = sourceIndex.sibling(sourceIndex.row(), 0);
    QStandardItem* item = _treeModel->itemFromIndex(fileItemIndex);
    FileTreeItem* treeItem = static_cast<FileTreeItem*>(item);

    if (treeItem && treeItem->getType() == FileTreeItem::File) {
        QString filePath = treeItem->getFilePath();
        spdlog::debug("FileBrowserPanel: File double-clicked: {}", filePath.toStdString());
        
        // Handle PRO files specially - open the PRO editor directly
        if (filePath.endsWith(".pro", Qt::CaseInsensitive)) {
            spdlog::debug("FileBrowserPanel: Opening PRO editor for double-clicked file: {}", filePath.toStdString());
            openProEditor(filePath);
            return;
        }
        
        // For other files, emit the standard signal
        emit fileDoubleClicked(filePath);
    }
}

void FileBrowserPanel::updateFileDisplay() {
    if (_isLoading) {
        return; // Don't update while loading
    }
    buildFileTree(_allFiles);
    updateFileCount();
}

void FileBrowserPanel::onFilesLoaded(const std::vector<std::string>& files) {
    spdlog::debug("FileBrowserPanel::onFilesLoaded called with {} files", files.size());
    _allFiles = files;
    spdlog::info("FileBrowserPanel: Received {} files from background loader", files.size());
    
    // Start progressive tree building
    buildFileTreeProgressive(files);
}

void FileBrowserPanel::onFileTypesExtracted(const std::unordered_set<std::string>& fileTypes) {
    spdlog::debug("FileBrowserPanel::onFileTypesExtracted called with {} types", fileTypes.size());
    _fileTypes = fileTypes;
    
    // Update file type combo box on main thread
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
    // Don't filter by search text here - search is handled separately now
    // Only apply file type filter for tree view
    std::vector<std::string> filteredFiles;
    for (const auto& file : files) {
        QString qFile = QString::fromStdString(file);

        // Apply file type filter
        if (_currentFileTypeFilter != "All Files") {
            QString extension = getFileExtension(qFile);
            if (extension != _currentFileTypeFilter) {
                continue;
            }
        }

        filteredFiles.push_back(file);
    }

    spdlog::info("FileBrowserPanel: Starting progressive build of {} filtered files", filteredFiles.size());
    startProgressiveTreeBuild(filteredFiles);
}

void FileBrowserPanel::startProgressiveTreeBuild(const std::vector<std::string>& filteredFiles) {
    // Store files to process
    _pendingFiles = filteredFiles;
    _currentChunkIndex = 0;

    // Clear existing tree
    _treeModel->clear();
    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Path" << "PRO Name");
    
    // Reapply column visibility after clearing
    applyDefaultColumnVisibility();

    // Update progress bar for tree building phase
    _progressBar->setRange(0, static_cast<int>(_pendingFiles.size()));
    _progressBar->setValue(0);
    _statusLabel->setText("Building file tree...");

    // Start processing chunks
    processNextChunk();
}

void FileBrowserPanel::processNextChunk() {
    if (_currentChunkIndex >= _pendingFiles.size()) {
        // Finished processing all files
        _isLoading = false;
        _progressBar->setVisible(false);
        
        // Expand first level directories
        for (int i = 0; i < _treeModel->rowCount(); ++i) {
            QModelIndex index = _treeModel->index(i, 0);
            _treeView->expand(index);
        }
        
        // Resize Name column to fit content
        resizeNameColumnToContent();
        
        // Trigger sorting to ensure proper directory/file order
        _proxyModel->sort(0, Qt::AscendingOrder);
        
        // Update file count
        updateFileCount();
        
        spdlog::info("FileBrowserPanel: Progressive tree building completed");
        return;
    }

    FileTreeItem* rootItem = static_cast<FileTreeItem*>(_treeModel->invisibleRootItem());
    
    // Process next chunk
    size_t endIndex = std::min(_currentChunkIndex + CHUNK_SIZE, _pendingFiles.size());
    for (size_t i = _currentChunkIndex; i < endIndex; ++i) {
        // Process events every 10 files to keep UI responsive
        if ((i - _currentChunkIndex) % 10 == 0) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 1);
        }
        
        const auto& file = _pendingFiles[i];
        QString qFile = QString::fromStdString(file);

        // Split path into components
        QStringList pathComponents = qFile.split('/', Qt::SkipEmptyParts);
        if (pathComponents.isEmpty())
            continue;

        FileTreeItem* currentParent = rootItem;
        QString currentPath = "";

        // Create directory structure
        for (int j = 0; j < pathComponents.size() - 1; ++j) {
            currentPath += "/" + pathComponents[j];
            currentParent = findOrCreateDirectory(currentParent, pathComponents[j]);
        }

        // Add file
        QString fileName = pathComponents.last();
        FileTreeItem* fileItem = new FileTreeItem(fileName, FileTreeItem::File);
        fileItem->setFilePath(qFile);

        QString extension = getFileExtension(fileName);
        QStandardItem* typeItem = new QStandardItem(extension);
        typeItem->setEditable(false);

        QStandardItem* pathItem = new QStandardItem(qFile);
        pathItem->setEditable(false);
        
        // Add PRO name for .pro files
        QString proName = (extension.toLower() == ".pro") ? getProName(qFile) : QString();
        QStandardItem* proNameItem = new QStandardItem(proName);
        proNameItem->setEditable(false);

        currentParent->appendRow(QList<QStandardItem*>() << fileItem << typeItem << pathItem << proNameItem);
    }

    // Update progress
    _currentChunkIndex = endIndex;
    _progressBar->setValue(static_cast<int>(_currentChunkIndex));
    _statusLabel->setText(QString("Building tree... %1/%2 files")
        .arg(_currentChunkIndex)
        .arg(_pendingFiles.size()));

    // Schedule next chunk using Qt event queue
    // This allows UI events to be processed between chunks
    QMetaObject::invokeMethod(this, &FileBrowserPanel::processNextChunk, Qt::QueuedConnection);
}

void FileBrowserPanel::onCustomContextMenuRequested(const QPoint& pos) {
    // Get the item at the clicked position
    QModelIndex index = _treeView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    
    // Map proxy index to source index
    QModelIndex sourceIndex = _proxyModel->mapToSource(index);
    
    // Get the item from the first column (Name column)
    QModelIndex nameIndex = sourceIndex.sibling(sourceIndex.row(), 0);
    FileTreeItem* item = static_cast<FileTreeItem*>(_treeModel->itemFromIndex(nameIndex));
    if (!item) {
        return;
    }
    
    // Only show context menu for files, not directories
    if (item->getType() != FileTreeItem::File) {
        return;
    }
    
    // Create context menu
    QMenu contextMenu(this);
    
    QString filePath = item->getFilePath();
    
    // Add type-specific actions
    QAction* openAction = nullptr;
    QAction* editProAction = nullptr;
    
    if (filePath.endsWith(".map", Qt::CaseInsensitive)) {
        openAction = contextMenu.addAction("Open Map");
        openAction->setIcon(QIcon(":/icons/filetypes/map.svg"));
    } else if (filePath.endsWith(".pro", Qt::CaseInsensitive)) {
        editProAction = contextMenu.addAction("Edit PRO");
        editProAction->setIcon(QIcon(":/icons/actions/settings.svg"));
        contextMenu.addSeparator();
        openAction = contextMenu.addAction("Open");
        openAction->setIcon(QIcon(":/icons/actions/open.svg"));
    } else if (isTextFile(filePath)) {
        openAction = contextMenu.addAction("Open with System Editor");
        openAction->setIcon(QIcon(":/icons/filetypes/text.svg"));
    } else {
        openAction = contextMenu.addAction("Open");
        openAction->setIcon(QIcon(":/icons/actions/open.svg"));
    }
    
    // Add Export action
    QAction* exportAction = contextMenu.addAction("Export");
    exportAction->setIcon(QIcon(":/icons/actions/save.svg"));
    
    // Execute menu and handle selected action
    QAction* selectedAction = contextMenu.exec(_treeView->viewport()->mapToGlobal(pos));
    
    if (selectedAction == openAction) {
        // Emit the double-click signal (same behavior as double-clicking)
        spdlog::debug("FileBrowserPanel: Open action triggered for: {}", filePath.toStdString());
        emit fileDoubleClicked(filePath);
    } else if (selectedAction == editProAction) {
        // Open PRO editor
        spdlog::debug("FileBrowserPanel: Edit PRO action triggered for: {}", filePath.toStdString());
        openProEditor(filePath);
    } else if (selectedAction == exportAction) {
        // Export the file
        spdlog::debug("FileBrowserPanel: Export action triggered for: {}", filePath.toStdString());
        exportFile(filePath);
    }
}

void FileBrowserPanel::exportFile(const QString& filePath) {
    try {
        _statusLabel->setText(QString("Exporting %1...").arg(filePath));
        
        // Get file info for default save name
        QFileInfo fileInfo(filePath);
        QString defaultFileName = fileInfo.fileName();
        
        // Show save dialog
        QString filter = QString("*%1;;All Files (*.*)").arg(fileInfo.suffix().isEmpty() ? "" : "." + fileInfo.suffix());
        QString saveFilePath = QtDialogs::saveFile(this, "Export File", filter);
        
        if (saveFilePath.isEmpty()) {
            _statusLabel->setText("Export cancelled");
            return;
        }
        
        // If no extension was provided and original had one, add it
        QFileInfo saveInfo(saveFilePath);
        if (saveInfo.suffix().isEmpty() && !fileInfo.suffix().isEmpty()) {
            saveFilePath += "." + fileInfo.suffix();
        }
        
        // Read file from VFS
        auto& resourceManager = ResourceManager::getInstance();
        auto vfs = resourceManager.getVFS();
        
        if (!vfs) {
            QMessageBox::critical(this, "Export Error", "Virtual file system not available");
            _statusLabel->setText("Export failed: VFS not available");
            return;
        }
        
        // Prepare VFS path (needs leading slash)
        std::filesystem::path vfsPath = "/" / std::filesystem::path(filePath.toStdString());
        vfspp::FileInfo vfsFileInfo = PathUtils::createNormalizedFileInfo(vfsPath);
        
        // Open file in VFS
        vfspp::IFilePtr vfsFile = vfs->OpenFile(vfsFileInfo, vfspp::IFile::FileMode::Read);
        if (!vfsFile) {
            QMessageBox::critical(this, "Export Error", 
                QString("Failed to open file in virtual file system: %1").arg(filePath));
            _statusLabel->setText("Export failed: Could not open source file");
            return;
        }
        
        // Read file data
        size_t fileSize = vfsFile->Size();
        std::vector<uint8_t> buffer(fileSize);
        size_t bytesRead = vfsFile->Read(buffer.data(), fileSize);
        
        if (bytesRead != fileSize) {
            QMessageBox::warning(this, "Export Warning", 
                QString("File was partially read: %1 of %2 bytes").arg(bytesRead).arg(fileSize));
        }
        
        // Write to destination file
        QFile outputFile(saveFilePath);
        if (!outputFile.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, "Export Error", 
                QString("Failed to create output file: %1\n%2").arg(saveFilePath).arg(outputFile.errorString()));
            _statusLabel->setText("Export failed: Could not create output file");
            return;
        }
        
        qint64 bytesWritten = outputFile.write(reinterpret_cast<const char*>(buffer.data()), bytesRead);
        outputFile.close();
        
        if (bytesWritten != static_cast<qint64>(bytesRead)) {
            QMessageBox::warning(this, "Export Warning", 
                QString("File was partially written: %1 of %2 bytes").arg(bytesWritten).arg(bytesRead));
        }
        
        // Update status
        _statusLabel->setText(QString("Exported %1 (%2 bytes)").arg(fileInfo.fileName()).arg(bytesWritten));
        spdlog::info("FileBrowserPanel: Exported {} to {} ({} bytes)", 
                     filePath.toStdString(), saveFilePath.toStdString(), bytesWritten);
                     
        // Emit signal in case other components want to know about the export
        emit fileExportRequested(filePath);
        
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export Error", 
            QString("An error occurred during export: %1").arg(e.what()));
        _statusLabel->setText("Export failed");
        spdlog::error("FileBrowserPanel: Export failed for {}: {}", filePath.toStdString(), e.what());
    }
}

void FileBrowserPanel::resizeNameColumnToContent() {
    if (!_treeView || !_treeModel) {
        return;
    }
    
    // Temporarily expand all items to measure their content
    _treeView->expandAll();
    
    // Let the header resize to content now that everything is expanded
    QHeaderView* header = _treeView->header();
    header->resizeSection(0, header->sectionSizeHint(0));
    
    // Find the maximum width needed for all visible items
    int maxWidth = 200; // Minimum width
    
    std::function<void(const QModelIndex&)> measureItems = [&](const QModelIndex& parent) {
        for (int i = 0; i < _treeModel->rowCount(parent); ++i) {
            QModelIndex index = _treeModel->index(i, 0, parent);
            if (index.isValid()) {
                // Get the size hint for this item
                QSize sizeHint = _treeView->sizeHintForIndex(index);
                maxWidth = std::max(maxWidth, sizeHint.width() + 20); // Add some padding
                
                // Recursively check children
                measureItems(index);
            }
        }
    };
    
    measureItems(QModelIndex());
    
    // Apply the calculated width, but cap it at a reasonable maximum
    int finalWidth = std::min(maxWidth, 500); // Cap at 500px to avoid excessive width
    header->resizeSection(0, finalWidth);
    
    // Collapse back to original state (only first level expanded)
    _treeView->collapseAll();
    for (int i = 0; i < _treeModel->rowCount(); ++i) {
        QModelIndex index = _treeModel->index(i, 0);
        _treeView->expand(index);
    }
}

void FileBrowserPanel::openProEditor(const QString& filePath) {
    try {
        _statusLabel->setText(QString("Loading PRO file: %1...").arg(filePath));
        
        // Read PRO file from VFS
        auto& resourceManager = ResourceManager::getInstance();
        auto vfs = resourceManager.getVFS();
        
        if (!vfs) {
            QMessageBox::critical(this, "PRO Editor Error", "Virtual file system not available");
            _statusLabel->setText("PRO Editor failed: VFS not available");
            return;
        }
        
        // Prepare VFS path (needs leading slash)
        std::filesystem::path vfsPath = "/" / std::filesystem::path(filePath.toStdString());
        vfspp::FileInfo vfsFileInfo = PathUtils::createNormalizedFileInfo(vfsPath);
        
        // Open file in VFS  
        vfspp::IFilePtr vfsFile = vfs->OpenFile(vfsFileInfo, vfspp::IFile::FileMode::Read);
        if (!vfsFile) {
            QMessageBox::critical(this, "PRO Editor Error", 
                QString("Failed to open PRO file in virtual file system: %1").arg(filePath));
            _statusLabel->setText("PRO Editor failed: Could not open PRO file");
            return;
        }
        
        // Read file data
        size_t fileSize = vfsFile->Size();
        std::vector<uint8_t> buffer(fileSize);
        size_t bytesRead = vfsFile->Read(buffer.data(), fileSize);
        
        if (bytesRead != fileSize) {
            QMessageBox::warning(this, "PRO Editor Warning", 
                QString("Only read %1 of %2 bytes from PRO file").arg(bytesRead).arg(fileSize));
        }
        
        // Create a temporary file to load the PRO data
        auto tempPath = std::filesystem::temp_directory_path() / ("temp_" + std::filesystem::path(filePath.toStdString()).filename().string());
        
        // Write buffer to temporary file
        std::ofstream tempFile(tempPath, std::ios::binary);
        tempFile.write(reinterpret_cast<const char*>(buffer.data()), bytesRead);
        tempFile.close();
        
        // Use ReaderFactory to read the PRO file
        auto pro = ReaderFactory::readFile<Pro>(tempPath);
        
        if (!pro) {
            QMessageBox::critical(this, "PRO Editor Error", 
                "Failed to parse PRO file. It may be corrupted or in an unsupported format.");
            _statusLabel->setText("PRO Editor failed: Could not parse PRO file");
            
            // Clean up temp file
            std::filesystem::remove(tempPath);
            return;
        }
        
        // Set the original file path for saving later
        pro->setPath(std::filesystem::path(filePath.toStdString()));
        
        // Create and show PRO editor dialog
        ProEditorDialog dialog(std::shared_ptr<Pro>(pro.release()), this);
        dialog.exec();
        
        // Clean up temp file
        std::filesystem::remove(tempPath);
        
        _statusLabel->setText("Ready");
        
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "PRO Editor Error", 
            QString("Failed to open PRO editor: %1").arg(e.what()));
        _statusLabel->setText("PRO Editor failed");
        spdlog::error("FileBrowserPanel::openProEditor failed: {}", e.what());
    }
}

void FileBrowserPanel::expandFilteredItems() {
    // Disable updates while expanding for better performance
    _treeView->setUpdatesEnabled(false);
    
    // Recursive function to expand all visible items
    std::function<void(const QModelIndex&)> expandRecursive = [&](const QModelIndex& parent) {
        int rowCount = _proxyModel->rowCount(parent);
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex index = _proxyModel->index(i, 0, parent);
            if (index.isValid()) {
                _treeView->expand(index);
                expandRecursive(index);
            }
        }
    };
    
    // Start expansion from root
    expandRecursive(QModelIndex());
    
    // Re-enable updates
    _treeView->setUpdatesEnabled(true);
}

// Column visibility management
void FileBrowserPanel::setupHeaderContextMenu() {
    QHeaderView* header = _treeView->header();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QHeaderView::customContextMenuRequested, this, &FileBrowserPanel::showHeaderContextMenu);
}

void FileBrowserPanel::showHeaderContextMenu(const QPoint& pos) {
    QHeaderView* header = _treeView->header();
    QMenu contextMenu(this);
    
    QStringList columnNames = {"Name", "Type", "Path", "PRO Name"};
    
    for (int i = 0; i < columnNames.size(); ++i) {
        QAction* action = contextMenu.addAction(columnNames[i]);
        action->setCheckable(true);
        action->setChecked(!_treeView->isColumnHidden(i));
        action->setData(i);
        
        // Name column cannot be hidden
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
    if (column == 0) return; // Name column cannot be hidden
    
    // Toggle based on current actual visibility
    bool currentlyHidden = _treeView->isColumnHidden(column);
    _treeView->setColumnHidden(column, !currentlyHidden);
}

void FileBrowserPanel::applyDefaultColumnVisibility() {
    // Apply default column visibility
    for (int i = 0; i < 4; ++i) {
        _treeView->setColumnHidden(i, !DEFAULT_COLUMN_VISIBILITY[i]);
    }
}

// PRO name loading
QString FileBrowserPanel::getProName(const QString& filePath) const {
    std::string stdPath = filePath.toStdString();
    
    // Check cache first
    auto cacheIt = _proNameCache.find(stdPath);
    if (cacheIt != _proNameCache.end()) {
        return cacheIt->second;
    }
    
    // Load PRO name from file
    QString proName = loadProNameFromFile(filePath);
    
    // Cache the result
    _proNameCache[stdPath] = proName;
    
    return proName;
}

QString FileBrowserPanel::loadProNameFromFile(const QString& filePath) const {
    try {
        auto& resourceManager = ResourceManager::getInstance();
        
        // Create normalized path for VFS access
        std::string stdPath = filePath.toStdString();
        if (stdPath.front() == '/') {
            stdPath = stdPath.substr(1); // Remove leading slash for VFS
        }
        
        // Check if file exists in VFS
        if (!resourceManager.fileExistsInVFS(stdPath)) {
            return QString("File not found");
        }
        
        // Load PRO file using ResourceManager
        const auto* pro = resourceManager.loadResource<Pro>(stdPath);
        if (!pro) {
            return QString("Failed to load");
        }

        const auto* msgFile = ProHelper::msgFile(pro->type());
        if (!msgFile) {
            return QString("MSG not found");
        }
        
        uint32_t messageId = pro->header.message_id;
        
        // Get name (message at messageId)
        try {
            const auto& nameMessage = const_cast<Msg*>(msgFile)->message(messageId);
            QString name = QString::fromStdString(nameMessage.text);
            return name.isEmpty() ? QString("No name (ID: %1)").arg(messageId) : name;
        } catch (const std::exception& e) {
            return QString("No name (ID: %1)").arg(messageId);
        }
        
    } catch (const std::exception& e) {
        return QString("Error: %1").arg(e.what());
    }
}

} // namespace geck