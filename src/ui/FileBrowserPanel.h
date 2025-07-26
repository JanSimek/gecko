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
#include <vector>
#include <memory>
#include <unordered_set>

namespace geck {

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
    ~FileBrowserPanel() = default;

    // File operations
    void refreshFileList();
    void loadFiles();

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

private:
    void setupUI();
    void setupTreeView();
    void setupFilterControls();
    void setupStatusBar();

    void buildFileTree(const std::vector<std::string>& files);
    FileTreeItem* createDirectoryStructure(const QString& path);
    FileTreeItem* findOrCreateDirectory(FileTreeItem* parent, const QString& dirName);
    QString getFileExtension(const QString& filePath) const;
    QString getFileIcon(const QString& extension) const;
    void updateFileCount();
    void updateFileTypeComboBox();
    void exportFile(const QString& filePath);
    void resizeNameColumnToContent();

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

    // Status
    QLabel* _statusLabel = nullptr;

    // Data
    std::vector<std::string> _allFiles;
    std::unordered_set<std::string> _fileTypes;

    // State
    QString _currentSearchFilter;
    QString _currentFileTypeFilter;
    QTimer* _searchTimer = nullptr;

    // Constants
    static constexpr int SEARCH_DELAY_MS = 300; // Delay before applying search filter
};

} // namespace geck