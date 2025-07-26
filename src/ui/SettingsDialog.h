#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <filesystem>
#include <vector>

namespace geck {

/**
 * @brief Settings configuration dialog
 * 
 * Provides UI for managing application settings including:
 * - Data path management
 * - Auto-detection of Fallout 2 installations
 * - Configuration validation
 */
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() = default;

private slots:
    void onAddDataPath();
    void onRemoveDataPath();
    void onAutoDetect();
    void onDataPathSelectionChanged();
    void onAccept();
    void onApply();
    void onReset();

private:
    void setupUI();
    void setupDataPathsGroup();
    void setupButtonBox();
    
    void loadSettings();
    void saveSettings();
    void updateUI();
    void validateDataPaths();
    
    void addDataPathToList(const std::filesystem::path& path);
    void removeSelectedDataPath();
    std::vector<std::filesystem::path> getDataPathsFromUI() const;
    
    // UI Components
    QVBoxLayout* _mainLayout;
    
    // Data Paths Group
    QGroupBox* _dataPathsGroup;
    QVBoxLayout* _dataPathsLayout;
    QHBoxLayout* _pathsControlLayout;
    QListWidget* _dataPathsList;
    QPushButton* _addPathButton;
    QPushButton* _removePathButton;
    QPushButton* _autoDetectButton;
    QLabel* _pathsHelpLabel;
    
    // Status
    QLabel* _statusLabel;
    QProgressBar* _progressBar;
    
    // Button Box
    QDialogButtonBox* _buttonBox;
    QPushButton* _applyButton;
    QPushButton* _resetButton;
    
    // State
    std::vector<std::filesystem::path> _originalDataPaths;
    bool _hasChanges;
};

} // namespace geck