#include "SettingsDialog.h"
#include "../util/Settings.h"
#include "../util/QtDialogs.h"

#include <QApplication>
#include <QStyle>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <spdlog/spdlog.h>

namespace geck {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _tabWidget(nullptr)
    , _generalTab(nullptr)
    , _generalTabLayout(nullptr)
    , _editorTab(nullptr)
    , _editorTabLayout(nullptr)
    , _dataPathsGroup(nullptr)
    , _dataPathsLayout(nullptr)
    , _pathsControlLayout(nullptr)
    , _dataPathsList(nullptr)
    , _addPathButton(nullptr)
    , _removePathButton(nullptr)
    , _autoDetectButton(nullptr)
    , _pathsHelpLabel(nullptr)
    , _gameLocationGroup(nullptr)
    , _gameLocationLayout(nullptr)
    , _steamGameRadio(nullptr)
    , _executableGameRadio(nullptr)
    , _steamGameControlLayout(nullptr)
    , _steamAppIdEdit(nullptr)
    , _executableGameControlLayout(nullptr)
    , _executableGameLocationEdit(nullptr)
    , _browseExecutableGameButton(nullptr)
    , _gameDataControlLayout(nullptr)
    , _gameDataDirectoryEdit(nullptr)
    , _browseGameDataButton(nullptr)
    , _gameLocationControlLayout(nullptr)
    , _autoDetectGameButton(nullptr)
    , _gameLocationHelpLabel(nullptr)
    , _statusLabel(nullptr)
    , _progressBar(nullptr)
    , _buttonBox(nullptr)
    , _applyButton(nullptr)
    , _resetButton(nullptr)
    , _hasChanges(false) {
    
    setWindowTitle("Preferences");
    setModal(true);
    setMinimumSize(700, 500);
    resize(800, 650);
    
    setupUI();
    loadSettings();
    updateUI();
}

void SettingsDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(12, 12, 12, 12);
    _mainLayout->setSpacing(12);
    
    setupTabs();
    
    // Status area
    _statusLabel = new QLabel("Ready");
    _statusLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; }");
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
    _generalTabLayout->setContentsMargins(12, 12, 12, 12);
    _generalTabLayout->setSpacing(12);
    
    setupDataPathsGroup();
    setupGameLocationGroup();
    
    _generalTabLayout->addStretch();
    
    _tabWidget->addTab(_generalTab, "General");
}

void SettingsDialog::setupEditorTab() {
    _editorTab = new QWidget();
    _editorTabLayout = new QVBoxLayout(_editorTab);
    _editorTabLayout->setContentsMargins(12, 12, 12, 12);
    _editorTabLayout->setSpacing(12);
    
    setupTextEditorGroup();
    
    _editorTabLayout->addStretch();
    
    _tabWidget->addTab(_editorTab, "Text Editor");
}

void SettingsDialog::setupDataPathsGroup() {
    _dataPathsGroup = new QGroupBox("Fallout 2 Data Paths");
    _dataPathsLayout = new QVBoxLayout(_dataPathsGroup);
    
    // Help text
    _pathsHelpLabel = new QLabel(
        "Add paths to Fallout 2 data directories or .dat files. These will be searched for game resources."
    );
    _pathsHelpLabel->setWordWrap(true);
    _pathsHelpLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; margin-bottom: 8px; }");
    _dataPathsLayout->addWidget(_pathsHelpLabel);
    
    // Paths list
    _dataPathsList = new QListWidget();
    _dataPathsList->setSelectionMode(QAbstractItemView::SingleSelection);
    _dataPathsList->setAlternatingRowColors(true);
    _dataPathsList->setMaximumHeight(100); // Constrain height to allow space for game location section
    _dataPathsList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _dataPathsLayout->addWidget(_dataPathsList);
    
    // Control buttons
    _pathsControlLayout = new QHBoxLayout();
    
    _addPathButton = new QPushButton("Add Path...");
    _addPathButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton));
    _pathsControlLayout->addWidget(_addPathButton);
    
    _removePathButton = new QPushButton("Remove");
    _removePathButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton));
    _removePathButton->setEnabled(false);
    _pathsControlLayout->addWidget(_removePathButton);
    
    _pathsControlLayout->addStretch();
    
    _autoDetectButton = new QPushButton("Auto-Detect");
    _autoDetectButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    _autoDetectButton->setToolTip("Automatically detect Fallout 2 installations");
    _pathsControlLayout->addWidget(_autoDetectButton);
    
    _dataPathsLayout->addLayout(_pathsControlLayout);
    _generalTabLayout->addWidget(_dataPathsGroup);
    
    // Connect signals
    connect(_addPathButton, &QPushButton::clicked, this, &SettingsDialog::onAddDataPath);
    connect(_removePathButton, &QPushButton::clicked, this, &SettingsDialog::onRemoveDataPath);
    connect(_autoDetectButton, &QPushButton::clicked, this, &SettingsDialog::onAutoDetect);
    connect(_dataPathsList, &QListWidget::itemSelectionChanged, this, &SettingsDialog::onDataPathSelectionChanged);
    
    // Enable double-click to edit paths
    connect(_dataPathsList, &QListWidget::itemDoubleClicked, [this](QListWidgetItem* item) {
        if (item) {
            QString currentPath = item->text();
            QString newPath = QFileDialog::getExistingDirectory(this, 
                "Select Fallout 2 Data Directory", currentPath);
            if (!newPath.isEmpty() && newPath != currentPath) {
                item->setText(newPath);
                _hasChanges = true;
                updateUI();
            }
        }
    });
}

void SettingsDialog::setupTextEditorGroup() {
    _textEditorGroup = new QGroupBox("Text Editor");
    _textEditorLayout = new QVBoxLayout(_textEditorGroup);
    
    // Help text
    _editorHelpLabel = new QLabel(
        "Choose how to open text files (txt, gam, lst, ini, etc.) from the file browser."
    );
    _editorHelpLabel->setWordWrap(true);
    _editorHelpLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; margin-bottom: 8px; }");
    _textEditorLayout->addWidget(_editorHelpLabel);
    
    // Radio buttons
    _systemEditorRadio = new QRadioButton("Use default system editor");
    _systemEditorRadio->setChecked(true); // Default selection
    _textEditorLayout->addWidget(_systemEditorRadio);
    
    _customEditorRadio = new QRadioButton("Use custom editor:");
    _textEditorLayout->addWidget(_customEditorRadio);
    
    // Custom editor path layout
    _customEditorLayout = new QHBoxLayout();
    _customEditorLayout->setContentsMargins(20, 0, 0, 0); // Indent under radio button
    
    _customEditorPathEdit = new QLineEdit();
    _customEditorPathEdit->setPlaceholderText("Path to editor executable...");
    _customEditorPathEdit->setEnabled(false);
    _customEditorLayout->addWidget(_customEditorPathEdit);
    
    _browseEditorButton = new QPushButton("Browse...");
    _browseEditorButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton));
    _browseEditorButton->setEnabled(false);
    _customEditorLayout->addWidget(_browseEditorButton);
    
    _textEditorLayout->addLayout(_customEditorLayout);
    _editorTabLayout->addWidget(_textEditorGroup);
    
    // Connect signals
    connect(_systemEditorRadio, &QRadioButton::toggled, this, &SettingsDialog::onEditorModeChanged);
    connect(_customEditorRadio, &QRadioButton::toggled, this, &SettingsDialog::onEditorModeChanged);
    connect(_browseEditorButton, &QPushButton::clicked, this, &SettingsDialog::onBrowseEditor);
    connect(_customEditorPathEdit, &QLineEdit::textChanged, [this]() {
        _hasChanges = true;
        updateUI();
    });
}

void SettingsDialog::setupGameLocationGroup() {
    _gameLocationGroup = new QGroupBox("Fallout 2 Game Location");
    _gameLocationLayout = new QVBoxLayout(_gameLocationGroup);
    
    // Help text
    _gameLocationHelpLabel = new QLabel(
        "Select the Fallout 2 game installation directory. This is used for the Play feature to launch the game with your current map."
    );
    _gameLocationHelpLabel->setWordWrap(true);
    _gameLocationHelpLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; margin-bottom: 8px; }");
    _gameLocationLayout->addWidget(_gameLocationHelpLabel);
    
    // Radio buttons for installation type
    _steamGameRadio = new QRadioButton("Steam Installation");
    _steamGameRadio->setChecked(false);
    _gameLocationLayout->addWidget(_steamGameRadio);
    
    // Steam App ID input layout
    _steamGameControlLayout = new QHBoxLayout();
    _steamGameControlLayout->setContentsMargins(20, 0, 0, 0); // Indent under radio button
    
    QLabel* steamAppIdLabel = new QLabel("Steam App ID:");
    _steamGameControlLayout->addWidget(steamAppIdLabel);
    
    _steamAppIdEdit = new QLineEdit();
    _steamAppIdEdit->setPlaceholderText("38410");
    _steamAppIdEdit->setEnabled(false);
    _steamAppIdEdit->setMaximumWidth(100);
    _steamGameControlLayout->addWidget(_steamAppIdEdit);
    
    QLabel* steamHelpLabel = new QLabel("(Default: 38410 for Fallout 2)");
    steamHelpLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; }");
    _steamGameControlLayout->addWidget(steamHelpLabel);
    
    _steamGameControlLayout->addStretch();
    
    _gameLocationLayout->addLayout(_steamGameControlLayout);
    
    _executableGameRadio = new QRadioButton("Executable/GOG Installation");
    _executableGameRadio->setChecked(true); // Default selection
    _gameLocationLayout->addWidget(_executableGameRadio);
    
    // Executable game location input layout
    _executableGameControlLayout = new QHBoxLayout();
    _executableGameControlLayout->setContentsMargins(20, 0, 0, 0); // Indent under radio button
    
    _executableGameLocationEdit = new QLineEdit();
    _executableGameLocationEdit->setPlaceholderText("Path to Fallout 2 executable installation...");
    _executableGameControlLayout->addWidget(_executableGameLocationEdit);
    
    _browseExecutableGameButton = new QPushButton("Browse...");
    _browseExecutableGameButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    _browseExecutableGameButton->setToolTip("Browse for Fallout 2 executable installation directory");
    _executableGameControlLayout->addWidget(_browseExecutableGameButton);
    
    _gameLocationLayout->addLayout(_executableGameControlLayout);
    
    // Game data directory input layout (for executable installs)
    QLabel* gameDataLabel = new QLabel("Game Data Directory:");
    gameDataLabel->setContentsMargins(20, 8, 0, 0); // Indent and add some spacing
    gameDataLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; }");
    _gameLocationLayout->addWidget(gameDataLabel);
    
    _gameDataControlLayout = new QHBoxLayout();
    _gameDataControlLayout->setContentsMargins(20, 0, 0, 0); // Indent under radio button
    
    _gameDataDirectoryEdit = new QLineEdit();
    _gameDataDirectoryEdit->setPlaceholderText("Path to game directory containing ddraw.ini...");
    _gameDataControlLayout->addWidget(_gameDataDirectoryEdit);
    
    _browseGameDataButton = new QPushButton("Browse...");
    _browseGameDataButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    _browseGameDataButton->setToolTip("Browse for Fallout 2 game data directory");
    _gameDataControlLayout->addWidget(_browseGameDataButton);
    
    _gameLocationLayout->addLayout(_gameDataControlLayout);
    
    // Auto-detect button (shared)
    _gameLocationControlLayout = new QHBoxLayout();
    _gameLocationControlLayout->addStretch();
    
    _autoDetectGameButton = new QPushButton("Auto-Detect");
    _autoDetectGameButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    _autoDetectGameButton->setToolTip("Automatically detect Fallout 2 game installations");
    _gameLocationControlLayout->addWidget(_autoDetectGameButton);
    
    _gameLocationLayout->addLayout(_gameLocationControlLayout);
    _generalTabLayout->addWidget(_gameLocationGroup);
    
    // Connect signals
    connect(_steamGameRadio, &QRadioButton::toggled, this, &SettingsDialog::onGameTypeChanged);
    connect(_executableGameRadio, &QRadioButton::toggled, this, &SettingsDialog::onGameTypeChanged);
    connect(_autoDetectGameButton, &QPushButton::clicked, this, &SettingsDialog::onAutoDetectGame);
    connect(_browseExecutableGameButton, &QPushButton::clicked, this, &SettingsDialog::onBrowseGameLocation);
    connect(_browseGameDataButton, &QPushButton::clicked, this, &SettingsDialog::onBrowseGameLocation);
    connect(_steamAppIdEdit, &QLineEdit::textChanged, [this]() {
        _hasChanges = true;
        updateUI();
    });
    connect(_executableGameLocationEdit, &QLineEdit::textChanged, [this]() {
        _hasChanges = true;
        updateUI();
    });
    connect(_gameDataDirectoryEdit, &QLineEdit::textChanged, [this]() {
        _hasChanges = true;
        updateUI();
    });
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
    _originalDataPaths = settings.getDataPaths();
    
    // Load data paths into UI
    _dataPathsList->clear();
    for (const auto& path : _originalDataPaths) {
        addDataPathToList(path);
    }
    
    // Load text editor settings
    if (settings.getTextEditorMode() == Settings::TextEditorMode::CUSTOM) {
        _customEditorRadio->setChecked(true);
        _customEditorPathEdit->setText(settings.getCustomEditorPath());
    } else {
        _systemEditorRadio->setChecked(true);
    }
    
    // Load game location settings
    if (settings.getGameInstallationType() == Settings::GameInstallationType::STEAM) {
        _steamGameRadio->setChecked(true);
    } else {
        _executableGameRadio->setChecked(true);
    }
    
    auto steamAppId = settings.getSteamAppId();
    if (!steamAppId.empty()) {
        _steamAppIdEdit->setText(QString::fromStdString(steamAppId));
    }
    
    auto executableGameLocation = settings.getExecutableGameLocation();
    if (!executableGameLocation.empty()) {
        _executableGameLocationEdit->setText(QString::fromStdString(executableGameLocation.string()));
    }
    
    auto gameDataDirectory = settings.getGameDataDirectory();
    if (!gameDataDirectory.empty()) {
        _gameDataDirectoryEdit->setText(QString::fromStdString(gameDataDirectory.string()));
    }
    
    // Update UI state for editor and game controls
    onEditorModeChanged();
    onGameTypeChanged();
    
    _hasChanges = false;
}

void SettingsDialog::saveSettings() {
    auto& settings = Settings::getInstance();
    
    // Save data paths
    auto dataPaths = getDataPathsFromUI();
    settings.setDataPaths(dataPaths);
    
    // Save text editor settings
    if (_customEditorRadio->isChecked()) {
        settings.setTextEditorMode(Settings::TextEditorMode::CUSTOM);
        settings.setCustomEditorPath(_customEditorPathEdit->text());
    } else {
        settings.setTextEditorMode(Settings::TextEditorMode::SYSTEM_DEFAULT);
        settings.setCustomEditorPath(""); // Clear custom path when using system default
    }
    
    // Save game location settings
    if (_steamGameRadio->isChecked()) {
        settings.setGameInstallationType(Settings::GameInstallationType::STEAM);
    } else {
        settings.setGameInstallationType(Settings::GameInstallationType::EXECUTABLE);
    }
    
    QString steamAppIdText = _steamAppIdEdit->text().trimmed();
    if (!steamAppIdText.isEmpty()) {
        settings.setSteamAppId(steamAppIdText.toStdString());
    } else {
        settings.setSteamAppId("38410"); // Default Fallout 2 Steam App ID
    }
    
    QString executableGameLocationText = _executableGameLocationEdit->text().trimmed();
    if (!executableGameLocationText.isEmpty()) {
        settings.setExecutableGameLocation(std::filesystem::path(executableGameLocationText.toStdString()));
    } else {
        settings.setExecutableGameLocation(std::filesystem::path{});
    }
    
    QString gameDataDirectoryText = _gameDataDirectoryEdit->text().trimmed();
    if (!gameDataDirectoryText.isEmpty()) {
        settings.setGameDataDirectory(std::filesystem::path(gameDataDirectoryText.toStdString()));
    } else {
        settings.setGameDataDirectory(std::filesystem::path{});
    }
    
    settings.save();
    
    _originalDataPaths = dataPaths;
    _hasChanges = false;
    
    _statusLabel->setText("Settings saved successfully");
    spdlog::info("Settings saved from preferences dialog");
}

void SettingsDialog::updateUI() {
    _applyButton->setEnabled(_hasChanges);
    _removePathButton->setEnabled(_dataPathsList->currentItem() != nullptr);
    
    // Update status based on data paths
    validateDataPaths();
}

void SettingsDialog::validateDataPaths() {
    auto& settings = Settings::getInstance();
    auto dataPaths = getDataPathsFromUI();
    
    if (dataPaths.empty()) {
        _statusLabel->setText("Warning: No data paths configured. Game resources will not be available.");
        _statusLabel->setStyleSheet("QLabel { color: orange; }");
        return;
    }
    
    int validPaths = 0;
    for (const auto& path : dataPaths) {
        if (settings.validateDataPath(path)) {
            validPaths++;
        }
    }
    
    if (validPaths == 0) {
        _statusLabel->setText("Warning: No valid data paths found. Check that paths exist and contain Fallout 2 data.");
        _statusLabel->setStyleSheet("QLabel { color: red; }");
    } else if (validPaths < static_cast<int>(dataPaths.size())) {
        _statusLabel->setText(QString("Found %1 valid paths out of %2 configured.").arg(validPaths).arg(dataPaths.size()));
        _statusLabel->setStyleSheet("QLabel { color: orange; }");
    } else {
        _statusLabel->setText(QString("All %1 data paths are valid.").arg(validPaths));
        _statusLabel->setStyleSheet("QLabel { color: green; }");
    }
}

void SettingsDialog::addDataPathToList(const std::filesystem::path& path) {
    QString pathStr = QString::fromStdString(path.string());
    
    // Check if path already exists in list
    for (int i = 0; i < _dataPathsList->count(); ++i) {
        if (_dataPathsList->item(i)->text() == pathStr) {
            return; // Path already exists
        }
    }
    
    QListWidgetItem* item = new QListWidgetItem(pathStr);
    
    // Set icon based on path type and validity
    auto& settings = Settings::getInstance();
    if (settings.validateDataPath(path)) {
        if (std::filesystem::is_directory(path)) {
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        } else {
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
        }
        item->setToolTip("Valid Fallout 2 data path");
    } else {
        item->setIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning));
        item->setToolTip("Invalid or missing path");
        item->setForeground(QColor(Qt::red));
    }
    
    _dataPathsList->addItem(item);
}

void SettingsDialog::removeSelectedDataPath() {
    QListWidgetItem* item = _dataPathsList->currentItem();
    if (item) {
        delete _dataPathsList->takeItem(_dataPathsList->row(item));
        _hasChanges = true;
        updateUI();
    }
}

std::vector<std::filesystem::path> SettingsDialog::getDataPathsFromUI() const {
    std::vector<std::filesystem::path> paths;
    
    for (int i = 0; i < _dataPathsList->count(); ++i) {
        QListWidgetItem* item = _dataPathsList->item(i);
        if (item) {
            paths.emplace_back(item->text().toStdString());
        }
    }
    
    return paths;
}

// Slots
void SettingsDialog::onAddDataPath() {
    QString path = QFileDialog::getExistingDirectory(this, 
        "Select Fallout 2 Data Directory", 
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    
    if (!path.isEmpty()) {
        addDataPathToList(std::filesystem::path(path.toStdString()));
        _hasChanges = true;
        updateUI();
    }
}

void SettingsDialog::onRemoveDataPath() {
    removeSelectedDataPath();
}

void SettingsDialog::onAutoDetect() {
    _autoDetectButton->setEnabled(false);
    _progressBar->setVisible(true);
    _progressBar->setRange(0, 0); // Indeterminate progress
    _statusLabel->setText("Detecting Fallout 2 installations...");
    
    QApplication::processEvents(); // Update UI
    
    auto detectedPaths = Settings::detectFallout2Installations();
    
    int addedPaths = 0;
    for (const auto& path : detectedPaths) {
        // Check if path is already in the list
        bool exists = false;
        for (int i = 0; i < _dataPathsList->count(); ++i) {
            if (_dataPathsList->item(i)->text() == QString::fromStdString(path.string())) {
                exists = true;
                break;
            }
        }
        
        if (!exists) {
            addDataPathToList(path);
            addedPaths++;
        }
    }
    
    _progressBar->setVisible(false);
    _autoDetectButton->setEnabled(true);
    
    if (addedPaths > 0) {
        _hasChanges = true;
        _statusLabel->setText(QString("Auto-detection complete. Added %1 new path(s).").arg(addedPaths));
        _statusLabel->setStyleSheet("QLabel { color: green; }");
    } else if (detectedPaths.empty()) {
        _statusLabel->setText("No Fallout 2 installations detected automatically.");
        _statusLabel->setStyleSheet("QLabel { color: orange; }");
    } else {
        _statusLabel->setText("Auto-detection complete. All detected paths were already configured.");
        _statusLabel->setStyleSheet("QLabel { color: blue; }");
    }
    
    updateUI();
}

void SettingsDialog::onDataPathSelectionChanged() {
    updateUI();
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

void SettingsDialog::onEditorModeChanged() {
    bool customSelected = _customEditorRadio->isChecked();
    
    // Enable/disable custom editor controls
    _customEditorPathEdit->setEnabled(customSelected);
    _browseEditorButton->setEnabled(customSelected);
    
    // Set has changes flag if this was a user action (not just loading settings)
    if (isVisible()) {
        _hasChanges = true;
        updateUI();
    }
}

void SettingsDialog::onGameTypeChanged() {
    bool steamSelected = _steamGameRadio->isChecked();
    
    // Enable/disable game location controls based on selection
    _steamAppIdEdit->setEnabled(steamSelected);
    _executableGameLocationEdit->setEnabled(!steamSelected);
    _browseExecutableGameButton->setEnabled(!steamSelected);
    _gameDataDirectoryEdit->setEnabled(!steamSelected);
    _browseGameDataButton->setEnabled(!steamSelected);
    
    // Set has changes flag if this was a user action (not just loading settings)
    if (isVisible()) {
        _hasChanges = true;
        updateUI();
    }
}

void SettingsDialog::onBrowseEditor() {
    QString currentPath = _customEditorPathEdit->text();
    
    // Default to the current path or home directory
    QString startPath = currentPath.isEmpty() ? 
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation) : 
        QFileInfo(currentPath).absolutePath();
    
    QString editorPath = QFileDialog::getOpenFileName(this,
        "Select Text Editor Executable",
        startPath,
        "Executable Files (*.exe *.app *);;All Files (*)");
    
    if (!editorPath.isEmpty()) {
        _customEditorPathEdit->setText(editorPath);
        _hasChanges = true;
        updateUI();
    }
}

void SettingsDialog::onAutoDetectGame() {
    _autoDetectGameButton->setEnabled(false);
    _progressBar->setVisible(true);
    _progressBar->setRange(0, 0); // Indeterminate progress
    _statusLabel->setText("Detecting Fallout 2 game installations...");
    
    QApplication::processEvents(); // Update UI
    
    auto detectedInstallations = Settings::detectFallout2InstallationsDetailed();
    
    _progressBar->setVisible(false);
    _autoDetectGameButton->setEnabled(true);
    
    if (!detectedInstallations.empty()) {
        // Populate both Steam and Executable fields based on detected installations
        bool foundSteam = false;
        bool foundExecutable = false;
        QString statusMessages;
        
        for (const auto& installation : detectedInstallations) {
            QString pathStr = QString::fromStdString(installation.path.string());
            QString description = QString::fromStdString(installation.description);
            
            if (installation.type == Settings::GameInstallationType::STEAM && !foundSteam) {
                _steamAppIdEdit->setText("38410"); // Set default Steam App ID
                foundSteam = true;
                statusMessages += QString("Steam: %1; ").arg(description);
            } else if (installation.type == Settings::GameInstallationType::EXECUTABLE && !foundExecutable) {
                _executableGameLocationEdit->setText(pathStr);
                foundExecutable = true;
                statusMessages += QString("Executable: %1; ").arg(description);
            }
        }
        
        // Set the radio button to the first detected type
        if (foundSteam && !foundExecutable) {
            _steamGameRadio->setChecked(true);
        } else if (foundExecutable) {
            _executableGameRadio->setChecked(true);
        }
        
        _hasChanges = true;
        statusMessages = statusMessages.trimmed();
        if (statusMessages.endsWith(";")) {
            statusMessages.chop(1);
        }
        
        _statusLabel->setText(QString("Auto-detected installations: %1").arg(statusMessages));
        _statusLabel->setStyleSheet("QLabel { color: green; }");
    } else {
        _statusLabel->setText("No Fallout 2 game installations detected automatically.");
        _statusLabel->setStyleSheet("QLabel { color: orange; }");
    }
    
    updateUI();
}

void SettingsDialog::onBrowseGameLocation() {
    // Determine which button was clicked
    QPushButton* sender = qobject_cast<QPushButton*>(QObject::sender());
    bool isDataDirectoryBrowse = (sender == _browseGameDataButton);
    
    QString currentPath;
    QLineEdit* targetEdit;
    QString dialogTitle;
    
    if (isDataDirectoryBrowse) {
        currentPath = _gameDataDirectoryEdit->text();
        targetEdit = _gameDataDirectoryEdit;
        dialogTitle = "Select Fallout 2 Game Data Directory";
    } else {
        currentPath = _executableGameLocationEdit->text();
        targetEdit = _executableGameLocationEdit;
        dialogTitle = "Select Fallout 2 Executable";
    }
    
    // Default to the current path or standard locations
    QString startPath = currentPath.isEmpty() ? 
        QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) : 
        currentPath;
    
#ifdef __APPLE__
    if (isDataDirectoryBrowse) {
        // For data directory, only allow directory selection
        QString gameDir = QFileDialog::getExistingDirectory(this, dialogTitle, startPath);
        if (!gameDir.isEmpty()) {
            targetEdit->setText(gameDir);
            _hasChanges = true;
        }
    } else {
        // For executable, allow both .app bundles and directories
        QFileDialog dialog(this, dialogTitle, startPath);
        dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setOption(QFileDialog::ShowDirsOnly, false);
        dialog.setOption(QFileDialog::DontUseNativeDialog, false);
        dialog.setNameFilter("Applications (*.app);;All Files and Directories (*)");
        
        // Custom filter to accept both .app bundles and directories
        dialog.setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::AllEntries);
        
        if (dialog.exec() == QDialog::Accepted) {
            QStringList selectedFiles = dialog.selectedFiles();
            if (!selectedFiles.isEmpty()) {
                QString gameDir = selectedFiles.first();
                targetEdit->setText(gameDir);
                _hasChanges = true;
            }
        }
    }
#else
    QString gameDir = QFileDialog::getExistingDirectory(this, dialogTitle, startPath);
    if (!gameDir.isEmpty()) {
        targetEdit->setText(gameDir);
        _hasChanges = true;
    }
#endif
}

void SettingsDialog::validateGameLocation(const QString& gameDir, bool isSteam) {
    std::filesystem::path gamePath(gameDir.toStdString());
    
#ifdef __APPLE__
    if (isSteam && gameDir.endsWith(".app")) {
        // For .app bundles on macOS, just check if it exists
        if (std::filesystem::exists(gamePath)) {
            _statusLabel->setText("Valid macOS Fallout 2 application selected.");
            _statusLabel->setStyleSheet("QLabel { color: green; }");
        } else {
            _statusLabel->setText("Warning: Selected application does not exist.");
            _statusLabel->setStyleSheet("QLabel { color: red; }");
        }
    } else {
        // Check for data directory and executable files
        bool hasDataDir = std::filesystem::exists(gamePath / "data");
        bool hasExecutable = std::filesystem::exists(gamePath / "fallout2.exe") || 
                           std::filesystem::exists(gamePath / "Fallout2.exe") ||
                           std::filesystem::exists(gamePath / "fallout2") ||
                           std::filesystem::exists(gamePath / "Fallout2.app");
        
        if (hasDataDir || hasExecutable) {
            _statusLabel->setText(isSteam ? "Valid Steam Fallout 2 installation directory selected." : 
                                          "Valid Fallout 2 executable installation directory selected.");
            _statusLabel->setStyleSheet("QLabel { color: green; }");
        } else {
            _statusLabel->setText("Warning: Selected directory may not be a valid Fallout 2 installation.");
            _statusLabel->setStyleSheet("QLabel { color: orange; }");
        }
    }
#else
    // Windows/Linux validation
    bool hasDataDir = std::filesystem::exists(gamePath / "data");
    bool hasExecutable = std::filesystem::exists(gamePath / "fallout2.exe") || 
                       std::filesystem::exists(gamePath / "Fallout2.exe") ||
                       std::filesystem::exists(gamePath / "fallout2") ||
                       std::filesystem::exists(gamePath / "Fallout2");
    
    if (hasDataDir && hasExecutable) {
        _statusLabel->setText(isSteam ? "Valid Steam Fallout 2 installation directory selected." : 
                                      "Valid Fallout 2 executable installation directory selected.");
        _statusLabel->setStyleSheet("QLabel { color: green; }");
    } else {
        _statusLabel->setText("Warning: Selected directory may not be a valid Fallout 2 installation.");
        _statusLabel->setStyleSheet("QLabel { color: orange; }");
    }
#endif
    
    updateUI();
}

} // namespace geck