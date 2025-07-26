#include "FileBrowserPanel.h"
#include "../util/ResourceManager.h"
#include "../util/QtDialogs.h"
#include "../util/PathUtils.h"

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
#include <spdlog/spdlog.h>
#include <algorithm>
#include <vfspp/VirtualFileSystem.hpp>

namespace geck {

// FileTreeItem implementation
FileTreeItem::FileTreeItem(const QString& name, ItemType type)
    : QStandardItem(name)
    , _type(type) {
    setEditable(false);

    // Set different icons for files and directories
    if (type == Directory) {
        setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    } else {
        QFileInfo fileInfo(name);
        QString suffix = fileInfo.suffix().toLower();

        // TODO: image icon for .frm
        if (suffix == "frm") {
            setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
        } else {
            setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
        }
    }
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
    , _currentSearchFilter("")
    , _currentFileTypeFilter("All Files")
    , _searchTimer(new QTimer(this)) {

    setupUI();

    // Setup search timer for delayed filtering
    _searchTimer->setSingleShot(true);
    _searchTimer->setInterval(SEARCH_DELAY_MS);
    connect(_searchTimer, &QTimer::timeout, this, &FileBrowserPanel::updateFileDisplay);

    // Load files on startup
    loadFiles();
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
    _searchLineEdit->setPlaceholderText("Filter files...");
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
    _refreshButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserReload));
    connect(_refreshButton, &QPushButton::clicked, this, &FileBrowserPanel::onRefreshClicked);
    typeLayout->addWidget(_refreshButton);

    filterGroupLayout->addLayout(typeLayout);

    _mainLayout->addWidget(filterGroup);
}

void FileBrowserPanel::setupTreeView() {
    _treeView = new QTreeView(this);
    _treeModel = new QStandardItemModel(this);

    // Set up model headers
    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Path");

    _treeView->setModel(_treeModel);
    _treeView->setAlternatingRowColors(true);
    _treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    _treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    _treeView->setSortingEnabled(true);
    _treeView->sortByColumn(0, Qt::AscendingOrder);

    // Configure headers
    QHeaderView* header = _treeView->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Interactive);     // Name column user-resizable
    header->resizeSection(0, 300);                                 // Start with wider default for long filenames
    header->resizeSection(1, 80);                                  // Type column fixed width
    header->setSectionResizeMode(2, QHeaderView::Stretch);         // Path column stretches

    // Enable context menu
    _treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Connect signals
    connect(_treeView, &QTreeView::clicked, this, &FileBrowserPanel::onTreeItemClicked);
    connect(_treeView, &QTreeView::doubleClicked, this, &FileBrowserPanel::onTreeItemDoubleClicked);
    connect(_treeView, &QTreeView::customContextMenuRequested, this, &FileBrowserPanel::onCustomContextMenuRequested);

    _mainLayout->addWidget(_treeView, 1);
}

void FileBrowserPanel::setupStatusBar() {
    _statusLabel = new QLabel("Ready", this);
    _statusLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; }");
    _mainLayout->addWidget(_statusLabel);
}

void FileBrowserPanel::loadFiles() {
    try {
        spdlog::info("FileBrowserPanel: Loading files from VFS...");
        _statusLabel->setText("Loading files...");

        // Get files from ResourceManager
        auto& resourceManager = ResourceManager::getInstance();
        _allFiles = resourceManager.listAllFiles();

        // Extract file types for filter dropdown
        _fileTypes.clear();
        for (const auto& file : _allFiles) {
            QString qFile = QString::fromStdString(file);
            QString extension = getFileExtension(qFile);
            if (!extension.isEmpty()) {
                _fileTypes.insert(extension.toStdString());
            }
        }

        // Update file type combo box
        updateFileTypeComboBox();

        // Build tree
        buildFileTree(_allFiles);

        // Update status
        updateFileCount();

        spdlog::info("FileBrowserPanel: Loaded {} files", _allFiles.size());

    } catch (const std::exception& e) {
        spdlog::error("FileBrowserPanel: Error loading files: {}", e.what());
        _statusLabel->setText("Error loading files");
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
    _treeModel->setHorizontalHeaderLabels(QStringList() << "Name" << "Type" << "Path");

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

        currentParent->appendRow(QList<QStandardItem*>() << fileItem << typeItem << pathItem);
    }

    // Expand first level directories
    for (int i = 0; i < _treeModel->rowCount(); ++i) {
        QModelIndex index = _treeModel->index(i, 0);
        _treeView->expand(index);
    }
    
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

    parent->appendRow(QList<QStandardItem*>() << dirItem << typeItem << pathItem);
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
    int displayedFiles = 0;

    // Count displayed files in tree
    std::function<void(QStandardItem*)> countFiles = [&](QStandardItem* item) {
        for (int i = 0; i < item->rowCount(); ++i) {
            QStandardItem* child = item->child(i, 0);
            FileTreeItem* treeItem = static_cast<FileTreeItem*>(child);
            if (treeItem && treeItem->getType() == FileTreeItem::File) {
                displayedFiles++;
            }
            countFiles(child);
        }
    };

    countFiles(_treeModel->invisibleRootItem());

    if (displayedFiles < totalFiles) {
        _statusLabel->setText(QString("Showing %1 of %2 files (filtered)")
                .arg(displayedFiles)
                .arg(totalFiles));
    } else {
        _statusLabel->setText(QString("Showing %1 files").arg(totalFiles));
    }
}

void FileBrowserPanel::refreshFileList() {
    loadFiles();
}

void FileBrowserPanel::setSearchFilter(const QString& filter) {
    _currentSearchFilter = filter;
    _searchTimer->start();
}

void FileBrowserPanel::setFileTypeFilter(const QString& fileType) {
    _currentFileTypeFilter = fileType;
    updateFileDisplay();
}

void FileBrowserPanel::onSearchTextChanged(const QString& text) {
    _currentSearchFilter = text;
    _searchTimer->start();
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

    QStandardItem* item = _treeModel->itemFromIndex(index);
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

    QStandardItem* item = _treeModel->itemFromIndex(index);
    FileTreeItem* treeItem = static_cast<FileTreeItem*>(item);

    if (treeItem && treeItem->getType() == FileTreeItem::File) {
        QString filePath = treeItem->getFilePath();
        spdlog::debug("FileBrowserPanel: File double-clicked: {}", filePath.toStdString());
        emit fileDoubleClicked(filePath);
    }
}

void FileBrowserPanel::updateFileDisplay() {
    buildFileTree(_allFiles);
    updateFileCount();
}

void FileBrowserPanel::onCustomContextMenuRequested(const QPoint& pos) {
    // Get the item at the clicked position
    QModelIndex index = _treeView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    
    // Get the item from the first column (Name column)
    QModelIndex nameIndex = index.sibling(index.row(), 0);
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
    
    // Add Open action
    QAction* openAction = contextMenu.addAction("Open");
    openAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    
    // Add Export action
    QAction* exportAction = contextMenu.addAction("Export");
    exportAction->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton));
    
    // Execute menu and handle selected action
    QAction* selectedAction = contextMenu.exec(_treeView->viewport()->mapToGlobal(pos));
    
    if (selectedAction == openAction) {
        // Emit the double-click signal (same behavior as double-clicking)
        QString filePath = item->getFilePath();
        spdlog::debug("FileBrowserPanel: Open action triggered for: {}", filePath.toStdString());
        emit fileDoubleClicked(filePath);
    } else if (selectedAction == exportAction) {
        // Export the file
        QString filePath = item->getFilePath();
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

} // namespace geck