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
#include <QRadioButton>
#include <QTabWidget>
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
    void onEditorModeChanged();
    void onBrowseEditor();
    void onAutoDetectGame();
    void onBrowseGameLocation();
    void onGameTypeChanged();

private:
    void setupUI();
    void setupTabs();
    void setupGeneralTab();
    void setupEditorTab();
    void setupDataPathsGroup();
    void setupTextEditorGroup();
    void setupGameLocationGroup();
    void setupButtonBox();
    
    void loadSettings();
    void saveSettings();
    void updateUI();
    void validateDataPaths();
    
    void addDataPathToList(const std::filesystem::path& path);
    void removeSelectedDataPath();
    std::vector<std::filesystem::path> getDataPathsFromUI() const;
    void validateGameLocation(const QString& gameDir, bool isSteam);
    
    // UI Components
    QVBoxLayout* _mainLayout;
    QTabWidget* _tabWidget;
    
    // General Tab
    QWidget* _generalTab;
    QVBoxLayout* _generalTabLayout;
    
    // Editor Tab  
    QWidget* _editorTab;
    QVBoxLayout* _editorTabLayout;
    
    // Data Paths Group
    QGroupBox* _dataPathsGroup;
    QVBoxLayout* _dataPathsLayout;
    QHBoxLayout* _pathsControlLayout;
    QListWidget* _dataPathsList;
    QPushButton* _addPathButton;
    QPushButton* _removePathButton;
    QPushButton* _autoDetectButton;
    QLabel* _pathsHelpLabel;
    
    // Text Editor Group
    QGroupBox* _textEditorGroup;
    QVBoxLayout* _textEditorLayout;
    QRadioButton* _systemEditorRadio;
    QRadioButton* _customEditorRadio;
    QHBoxLayout* _customEditorLayout;
    QLineEdit* _customEditorPathEdit;
    QPushButton* _browseEditorButton;
    QLabel* _editorHelpLabel;
    
    // Game Location Group
    QGroupBox* _gameLocationGroup;
    QVBoxLayout* _gameLocationLayout;
    QRadioButton* _steamGameRadio;
    QRadioButton* _executableGameRadio;
    QHBoxLayout* _steamGameControlLayout;
    QLineEdit* _steamAppIdEdit;
    QHBoxLayout* _executableGameControlLayout;
    QLineEdit* _executableGameLocationEdit;
    QPushButton* _browseExecutableGameButton;
    QHBoxLayout* _gameDataControlLayout;
    QLineEdit* _gameDataDirectoryEdit;
    QPushButton* _browseGameDataButton;
    QHBoxLayout* _gameLocationControlLayout;
    QPushButton* _autoDetectGameButton;
    QLabel* _gameLocationHelpLabel;
    
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