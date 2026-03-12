#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <filesystem>
#include <vector>

// Forward declarations for our widgets
namespace geck {
class DataPathsWidget;
class GameLocationWidget;
class TextEditorWidget;
}

namespace geck {

/**
 * @brief Settings configuration dialog
 *
 * Provides UI for managing application settings using modular widgets:
 * - DataPathsWidget for Fallout 2 data path management
 * - GameLocationWidget for game installation configuration
 * - TextEditorWidget for text editor preferences
 */
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() = default;

signals:
    void settingsSaved(bool dataPathsChanged);

private slots:
    void onWidgetChanged();
    void onAccept();
    void onApply();
    void onReset();
    void onStatusChanged(const QString& message, const QString& styleClass);

private:
    void setupUI();
    void setupTabs();
    void setupGeneralTab();
    void setupEditorTab();
    void setupButtonBox();

    void loadSettings();
    void saveSettings();
    void updateUI();
    void setMainStatus(const QString& message, const QString& styleClass = "normal");

    // UI Components
    QVBoxLayout* _mainLayout;
    QTabWidget* _tabWidget;

    // General Tab
    QWidget* _generalTab;
    QVBoxLayout* _generalTabLayout;
    DataPathsWidget* _dataPathsWidget;
    GameLocationWidget* _gameLocationWidget;

    // Editor Tab
    QWidget* _editorTab;
    QVBoxLayout* _editorTabLayout;
    TextEditorWidget* _textEditorWidget;

    // Status and Controls
    QLabel* _statusLabel;
    QProgressBar* _progressBar;
    QDialogButtonBox* _buttonBox;
    QPushButton* _applyButton;
    QPushButton* _resetButton;

    // State
    std::vector<std::filesystem::path> _originalDataPaths;
    bool _hasChanges;
};

} // namespace geck
