#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QProgressBar>
#include <filesystem>
#include <vector>

namespace geck {

/**
 * @brief Widget for managing Fallout 2 data paths
 * 
 * Provides UI for adding, removing, and auto-detecting Fallout 2 data paths.
 * Handles validation and visual feedback for path entries.
 */
class DataPathsWidget : public QGroupBox {
    Q_OBJECT

public:
    explicit DataPathsWidget(QWidget* parent = nullptr);
    ~DataPathsWidget() = default;

    // Data access
    std::vector<std::filesystem::path> getDataPaths() const;
    void setDataPaths(const std::vector<std::filesystem::path>& paths);
    
    // Validation
    void validatePaths();
    
    // Status updates
    void setStatusMessage(const QString& message, const QString& styleClass = "normal");

signals:
    void dataPathsChanged();
    void statusChanged(const QString& message, const QString& styleClass);

private slots:
    void onAddPath();
    void onRemovePath();
    void onAutoDetect();
    void onSelectionChanged();
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    void setupUI();
    void setupConnections();
    void addPathToList(const std::filesystem::path& path);
    void removeSelectedPath();
    void updateButtonStates();
    void moveSelectedPath(int offset);
    
    // UI Components
    QVBoxLayout* _layout;
    QLabel* _helpLabel;
    QListWidget* _pathsList;
    QHBoxLayout* _controlLayout;
    QPushButton* _addButton;
    QPushButton* _removeButton;
    QPushButton* _moveUpButton;
    QPushButton* _moveDownButton;
    QPushButton* _autoDetectButton;
    QProgressBar* _progressBar;
};

} // namespace geck
