#pragma once

#include <QWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QTimer>
#include <QStandardItem>
#include <QMenu>
#include <QAction>
#include <QThread>
#include <QProgressBar>
#include <vector>
#include <memory>
#include <unordered_set>
#include <atomic>

namespace geck {

/**
 * @brief Worker class for loading files in background thread
 */
class FileLoaderWorker : public QObject {
    Q_OBJECT

public:
    explicit FileLoaderWorker(QObject* parent = nullptr);
    
    std::atomic<bool> _shouldStop{false};

public slots:
    void loadFiles();

signals:
    void filesLoaded(const std::vector<std::string>& files);
    void fileTypesExtracted(const std::unordered_set<std::string>& fileTypes);
    void loadingProgress(int current, int total, const QString& status);
    void loadingError(const QString& error);
    void loadingComplete();
};

/**
 * @brief Custom QStandardItem for file/directory entries
 */
class FileTreeItem : public QStandardItem {
public:
    enum ItemType {
        Directory,
        File
    };

    explicit FileTreeItem(const QString& name, ItemType type);

    ItemType getType() const { return _type; }
    void setFilePath(const QString& path) { _filePath = path; }
    QString getFilePath() const { return _filePath; }
    void setFileSize(qint64 size) { _fileSize = size; }
    qint64 getFileSize() const { return _fileSize; }
    
    static QIcon getFileIcon(const QString& fileName);

private:
    ItemType _type;
    QString _filePath;
    qint64 _fileSize = 0;
};

/**
 * @brief Panel for browsing all files in the virtual file system
 *
 * Features:
 * - Tree view showing directory hierarchy
 * - Search/filter functionality
 * - File type filtering
 * - File information display
 * - Refresh capability
 */
class FileBrowserPanel : public QWidget {
    Q_OBJECT

public:
    explicit FileBrowserPanel(QWidget* parent = nullptr);
    ~FileBrowserPanel();

    // File operations
    void refreshFileList();
    void loadFiles();
    void stopLoading();

    // Search and filtering
    void setSearchFilter(const QString& filter);
    void setFileTypeFilter(const QString& fileType);

signals:
    void fileSelected(const QString& filePath);
    void fileDoubleClicked(const QString& filePath);
    void fileExportRequested(const QString& filePath);

public slots:
    void onSearchTextChanged(const QString& text);
    void onFileTypeChanged(const QString& fileType);
    void onRefreshClicked();
    void onTreeItemClicked(const QModelIndex& index);
    void onTreeItemDoubleClicked(const QModelIndex& index);

private slots:
    void updateFileDisplay();
    void onCustomContextMenuRequested(const QPoint& pos);
    void onFilesLoaded(const std::vector<std::string>& files);
    void onFileTypesExtracted(const std::unordered_set<std::string>& fileTypes);
    void onLoadingProgress(int current, int total, const QString& status);
    void onLoadingError(const QString& error);
    void processNextChunk();

private:
    void setupUI();
    void setupTreeView();
    void setupFilterControls();
    void setupStatusBar();

    void buildFileTree(const std::vector<std::string>& files);
    void buildFileTreeProgressive(const std::vector<std::string>& files);
    void startProgressiveTreeBuild(const std::vector<std::string>& filteredFiles);
    FileTreeItem* createDirectoryStructure(const QString& path);
    FileTreeItem* findOrCreateDirectory(FileTreeItem* parent, const QString& dirName);
    QString getFileExtension(const QString& filePath) const;
    QString getFileIcon(const QString& extension) const;
    void updateFileCount();
    void updateFileTypeComboBox();
    void exportFile(const QString& filePath);
    void openProEditor(const QString& filePath);
    void resizeNameColumnToContent();
    bool isTextFile(const QString& filePath) const;

    // UI Components
    QVBoxLayout* _mainLayout = nullptr;

    // Filter controls
    QHBoxLayout* _filterLayout = nullptr;
    QLineEdit* _searchLineEdit = nullptr;
    QComboBox* _fileTypeComboBox = nullptr;
    QPushButton* _refreshButton = nullptr;

    // Tree view
    QTreeView* _treeView = nullptr;
    QStandardItemModel* _treeModel = nullptr;

    // Status and progress
    QLabel* _statusLabel = nullptr;
    QProgressBar* _progressBar = nullptr;

    // Background loading
    QThread* _loaderThread = nullptr;
    FileLoaderWorker* _loaderWorker = nullptr;

    // Data
    std::vector<std::string> _allFiles;
    std::unordered_set<std::string> _fileTypes;

    // State
    QString _currentSearchFilter;
    QString _currentFileTypeFilter;
    QTimer* _searchTimer = nullptr;
    
    // Progressive building state
    std::vector<std::string> _pendingFiles;
    size_t _currentChunkIndex = 0;
    QTimer* _chunkTimer = nullptr;
    bool _isLoading = false;

    // Constants
    static constexpr int SEARCH_DELAY_MS = 300; // Delay before applying search filter
    static constexpr int CHUNK_SIZE = 1000; // Files processed per chunk
    static constexpr int CHUNK_DELAY_MS = 10; // Delay between chunks
};

} // namespace geck