#include "SettingsDialog.h"
#include "../widgets/DataPathsWidget.h"
#include "../widgets/GameLocationWidget.h"
#include "../widgets/TextEditorWidget.h"
#include "../UIConstants.h"
#include "../../util/Settings.h"
#include "../../util/ResourceManager.h"
#include "../../state/loader/DataPathLoader.h"
#include "../widgets/LoadingWidget.h"

#include <QApplication>
#include <QStyle>
#include <QMessageBox>
#include <spdlog/spdlog.h>

namespace geck {

using namespace ui::constants;
using namespace ui::styles;
using namespace ui::defaults;

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _tabWidget(nullptr)
    , _generalTab(nullptr)
    , _generalTabLayout(nullptr)
    , _dataPathsWidget(nullptr)
    , _gameLocationWidget(nullptr)
    , _editorTab(nullptr)
    , _editorTabLayout(nullptr)
    , _textEditorWidget(nullptr)
    , _statusLabel(nullptr)
    , _progressBar(nullptr)
    , _buttonBox(nullptr)
    , _applyButton(nullptr)
    , _resetButton(nullptr)
    , _hasChanges(false) {
    
    setWindowTitle("Preferences");
    setModal(true);
    setMinimumSize(SETTINGS_MIN_WIDTH, SETTINGS_MIN_HEIGHT);
    resize(SETTINGS_DEFAULT_WIDTH, SETTINGS_DEFAULT_HEIGHT);
    
    setupUI();
    loadSettings();
    updateUI();
}

void SettingsDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(DEFAULT_MARGIN, DEFAULT_MARGIN, DEFAULT_MARGIN, DEFAULT_MARGIN);
    _mainLayout->setSpacing(DEFAULT_SPACING);
    
    setupTabs();
    
    // Status area
    _statusLabel = new QLabel(READY_STATUS);
    _statusLabel->setStyleSheet(STATUS_NORMAL);
    _mainLayout->addWidget(_statusLabel);
    
    _progressBar = new QProgressBar();
    _progressBar->setVisible(false);
    _mainLayout->addWidget(_progressBar);
    
    setupButtonBox();
    
    setLayout(_mainLayout);
}

void SettingsDialog::setupTabs() {
    _tabWidget = new QTabWidget();
    
    setupGeneralTab();
    setupEditorTab();
    
    _mainLayout->addWidget(_tabWidget);
}

void SettingsDialog::setupGeneralTab() {
    _generalTab = new QWidget();
    _generalTabLayout = new QVBoxLayout(_generalTab);
    _generalTabLayout->setContentsMargins(DEFAULT_MARGIN, DEFAULT_MARGIN, DEFAULT_MARGIN, DEFAULT_MARGIN);
    _generalTabLayout->setSpacing(DEFAULT_SPACING);
    
    // Create and add widget instances
    _dataPathsWidget = new DataPathsWidget();
    _generalTabLayout->addWidget(_dataPathsWidget);
    
    _gameLocationWidget = new GameLocationWidget();
    _generalTabLayout->addWidget(_gameLocationWidget);
    
    _generalTabLayout->addStretch();
    
    _tabWidget->addTab(_generalTab, "General");
    
    // Connect signals
    connect(_dataPathsWidget, &DataPathsWidget::dataPathsChanged, this, &SettingsDialog::onWidgetChanged);
    connect(_dataPathsWidget, &DataPathsWidget::statusChanged, this, &SettingsDialog::onStatusChanged);
    connect(_gameLocationWidget, &GameLocationWidget::configurationChanged, this, &SettingsDialog::onWidgetChanged);
    connect(_gameLocationWidget, &GameLocationWidget::statusChanged, this, &SettingsDialog::onStatusChanged);
}

void SettingsDialog::setupEditorTab() {
    _editorTab = new QWidget();
    _editorTabLayout = new QVBoxLayout(_editorTab);
    _editorTabLayout->setContentsMargins(DEFAULT_MARGIN, DEFAULT_MARGIN, DEFAULT_MARGIN, DEFAULT_MARGIN);
    _editorTabLayout->setSpacing(DEFAULT_SPACING);
    
    // Create and add text editor widget
    _textEditorWidget = new TextEditorWidget();
    _editorTabLayout->addWidget(_textEditorWidget);
    
    _editorTabLayout->addStretch();
    
    _tabWidget->addTab(_editorTab, "Text Editor");
    
    // Connect signals
    connect(_textEditorWidget, &TextEditorWidget::configurationChanged, this, &SettingsDialog::onWidgetChanged);
}

void SettingsDialog::setupButtonBox() {
    _buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    
    _applyButton = _buttonBox->addButton("Apply", QDialogButtonBox::ApplyRole);
    _resetButton = _buttonBox->addButton("Reset", QDialogButtonBox::ResetRole);
    
    _applyButton->setEnabled(false);
    
    connect(_buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_applyButton, &QPushButton::clicked, this, &SettingsDialog::onApply);
    connect(_resetButton, &QPushButton::clicked, this, &SettingsDialog::onReset);
    
    _mainLayout->addWidget(_buttonBox);
}

void SettingsDialog::loadSettings() {
    auto& settings = Settings::getInstance();
    
    // Store original data paths for reset functionality
    _originalDataPaths = settings.getDataPaths();
    
    // Load data paths
    _dataPathsWidget->setDataPaths(_originalDataPaths);
    
    // Load text editor settings
    _textEditorWidget->setEditorMode(settings.getTextEditorMode());
    _textEditorWidget->setCustomEditorPath(settings.getCustomEditorPath());
    
    // Load game location settings
    _gameLocationWidget->setInstallationType(settings.getGameInstallationType());
    _gameLocationWidget->setSteamAppId(settings.getSteamAppId());
    _gameLocationWidget->setExecutableLocation(settings.getExecutableGameLocation());
    _gameLocationWidget->setDataDirectory(settings.getGameDataDirectory());
    
    _hasChanges = false;
    updateUI();
}

void SettingsDialog::saveSettings() {
    auto& settings = Settings::getInstance();
    
    // Save data paths
    auto dataPaths = _dataPathsWidget->getDataPaths();
    bool pathsHaveChanged = dataPaths != _originalDataPaths;
    settings.setDataPaths(dataPaths);
    
    // Save text editor settings
    settings.setTextEditorMode(_textEditorWidget->getEditorMode());
    if (_textEditorWidget->getEditorMode() == Settings::TextEditorMode::CUSTOM) {
        settings.setCustomEditorPath(_textEditorWidget->getCustomEditorPath());
    } else {
        settings.setCustomEditorPath(""); // Clear custom path when using system default
    }
    
    // Save game location settings
    settings.setGameInstallationType(_gameLocationWidget->getInstallationType());
    settings.setSteamAppId(_gameLocationWidget->getSteamAppId());
    settings.setExecutableGameLocation(_gameLocationWidget->getExecutableLocation());
    settings.setGameDataDirectory(_gameLocationWidget->getDataDirectory());
    
    settings.save();
    
    // If data paths changed, reload the ResourceManager
    if (pathsHaveChanged) {
        spdlog::info("Data paths changed, reloading ResourceManager...");
        
        // Emit signal that data paths changed
        emit dataPathsChanged();
        
        // Clear the ResourceManager
        auto& resourceManager = ResourceManager::getInstance();
        resourceManager.clearAllDataPaths();
        
        // Reload data paths with a loading dialog
        auto loadingWidget = std::make_unique<LoadingWidget>(this);
        loadingWidget->setWindowTitle("Reloading Game Data");
        loadingWidget->addLoader(std::make_unique<DataPathLoader>(dataPaths));
        loadingWidget->exec();
        
        spdlog::info("ResourceManager reloaded with new data paths");
    }
    
    _originalDataPaths = dataPaths;
    _hasChanges = false;
    
    setMainStatus("Settings saved successfully", "success");
    spdlog::info("Settings saved from preferences dialog");
    
    // Notify that settings have been saved
    Q_EMIT settingsSaved();
}

void SettingsDialog::updateUI() {
    _applyButton->setEnabled(_hasChanges);
}

void SettingsDialog::setMainStatus(const QString& message, const QString& styleClass) {
    _statusLabel->setText(message);
    
    if (styleClass == "warning") {
        _statusLabel->setStyleSheet(STATUS_WARNING);
    } else if (styleClass == "error") {
        _statusLabel->setStyleSheet(STATUS_ERROR);
    } else if (styleClass == "success") {
        _statusLabel->setStyleSheet(STATUS_SUCCESS);
    } else if (styleClass == "info") {
        _statusLabel->setStyleSheet(STATUS_INFO);
    } else {
        _statusLabel->setStyleSheet(STATUS_NORMAL);
    }
}

// Slots
void SettingsDialog::onWidgetChanged() {
    _hasChanges = true;
    updateUI();
}

void SettingsDialog::onStatusChanged(const QString& message, const QString& styleClass) {
    // Propagate status messages from child widgets to main status
    setMainStatus(message, styleClass);
}

void SettingsDialog::onAccept() {
    if (_hasChanges) {
        saveSettings();
    }
    accept();
}

void SettingsDialog::onApply() {
    saveSettings();
    updateUI();
}

void SettingsDialog::onReset() {
    int ret = QMessageBox::question(this, "Reset Settings", 
        "Are you sure you want to reset all settings to their original values?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        loadSettings();
        updateUI();
    }
}

} // namespace geck