#include "GameLocationWidget.h"
#include "../../util/Settings.h"

#include <QApplication>
#include <QStyle>
#include <QStandardPaths>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <spdlog/spdlog.h>

namespace geck {

// Constants for styling and configuration
namespace {
    constexpr const char* HELP_STYLE = "QLabel { color: gray; font-size: 11px; margin-bottom: 8px; }";
    constexpr const char* LABEL_STYLE = "QLabel { color: gray; font-size: 11px; }";
    constexpr const char* STATUS_NORMAL = "QLabel { color: gray; font-size: 11px; }";
    constexpr const char* STATUS_WARNING = "QLabel { color: orange; font-size: 11px; }";
    constexpr const char* STATUS_ERROR = "QLabel { color: red; font-size: 11px; }";
    constexpr const char* STATUS_SUCCESS = "QLabel { color: green; font-size: 11px; }";
    constexpr const char* STATUS_INFO = "QLabel { color: blue; font-size: 11px; }";
    constexpr int STEAM_APPID_WIDTH = 100;
    constexpr int INDENT_MARGIN = 20;
    constexpr const char* DEFAULT_STEAM_APPID = "38410";
}

GameLocationWidget::GameLocationWidget(QWidget* parent)
    : QGroupBox("Fallout 2 Game Location", parent)
    , _layout(nullptr)
    , _helpLabel(nullptr)
    , _steamRadio(nullptr)
    , _steamLayout(nullptr)
    , _steamAppIdLabel(nullptr)
    , _steamAppIdEdit(nullptr)
    , _steamHelpLabel(nullptr)
    , _executableRadio(nullptr)
    , _executableLayout(nullptr)
    , _executableLocationEdit(nullptr)
    , _browseExecutableButton(nullptr)
    , _dataDirectoryLabel(nullptr)
    , _dataDirectoryLayout(nullptr)
    , _dataDirectoryEdit(nullptr)
    , _browseDataDirectoryButton(nullptr)
    , _controlLayout(nullptr)
    , _autoDetectButton(nullptr)
    , _progressBar(nullptr)
    , _statusLabel(nullptr) {
    
    setupUI();
    setupConnections();
}

void GameLocationWidget::setupUI() {
    _layout = new QVBoxLayout(this);
    
    // Help text
    _helpLabel = new QLabel(
        "Select the Fallout 2 game installation directory. This is used for the Play feature to launch the game with your current map."
    );
    _helpLabel->setWordWrap(true);
    _helpLabel->setStyleSheet(HELP_STYLE);
    _layout->addWidget(_helpLabel);
    
    // Steam installation option
    _steamRadio = new QRadioButton("Steam Installation");
    _steamRadio->setChecked(false);
    _layout->addWidget(_steamRadio);
    
    // Steam App ID input
    _steamLayout = new QHBoxLayout();
    _steamLayout->setContentsMargins(INDENT_MARGIN, 0, 0, 0);
    
    _steamAppIdLabel = new QLabel("Steam App ID:");
    _steamLayout->addWidget(_steamAppIdLabel);
    
    _steamAppIdEdit = new QLineEdit();
    _steamAppIdEdit->setPlaceholderText(DEFAULT_STEAM_APPID);
    _steamAppIdEdit->setEnabled(false);
    _steamAppIdEdit->setMaximumWidth(STEAM_APPID_WIDTH);
    _steamLayout->addWidget(_steamAppIdEdit);
    
    _steamHelpLabel = new QLabel("(Default: 38410 for Fallout 2)");
    _steamHelpLabel->setStyleSheet(LABEL_STYLE);
    _steamLayout->addWidget(_steamHelpLabel);
    
    _steamLayout->addStretch();
    _layout->addLayout(_steamLayout);
    
    // Executable installation option
    _executableRadio = new QRadioButton("Executable/GOG Installation");
    _executableRadio->setChecked(true); // Default selection
    _layout->addWidget(_executableRadio);
    
    // Executable location input
    _executableLayout = new QHBoxLayout();
    _executableLayout->setContentsMargins(INDENT_MARGIN, 0, 0, 0);
    
    _executableLocationEdit = new QLineEdit();
    _executableLocationEdit->setPlaceholderText("Path to Fallout 2 executable installation...");
    _executableLayout->addWidget(_executableLocationEdit);
    
    _browseExecutableButton = new QPushButton("Browse...");
    _browseExecutableButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    _browseExecutableButton->setToolTip("Browse for Fallout 2 executable installation directory");
    _executableLayout->addWidget(_browseExecutableButton);
    
    _layout->addLayout(_executableLayout);
    
    // Game data directory (for executable installs)
    _dataDirectoryLabel = new QLabel("Game Data Directory:");
    _dataDirectoryLabel->setContentsMargins(INDENT_MARGIN, 8, 0, 0);
    _dataDirectoryLabel->setStyleSheet(LABEL_STYLE);
    _layout->addWidget(_dataDirectoryLabel);
    
    _dataDirectoryLayout = new QHBoxLayout();
    _dataDirectoryLayout->setContentsMargins(INDENT_MARGIN, 0, 0, 0);
    
    _dataDirectoryEdit = new QLineEdit();
    _dataDirectoryEdit->setPlaceholderText("Path to game directory containing ddraw.ini...");
    _dataDirectoryLayout->addWidget(_dataDirectoryEdit);
    
    _browseDataDirectoryButton = new QPushButton("Browse...");
    _browseDataDirectoryButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    _browseDataDirectoryButton->setToolTip("Browse for Fallout 2 game data directory");
    _dataDirectoryLayout->addWidget(_browseDataDirectoryButton);
    
    _layout->addLayout(_dataDirectoryLayout);
    
    // Auto-detect button
    _controlLayout = new QHBoxLayout();
    _controlLayout->addStretch();
    
    _autoDetectButton = new QPushButton("Auto-Detect");
    _autoDetectButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    _autoDetectButton->setToolTip("Automatically detect Fallout 2 game installations");
    _controlLayout->addWidget(_autoDetectButton);
    
    _layout->addLayout(_controlLayout);
    
    // Progress bar (initially hidden)
    _progressBar = new QProgressBar();
    _progressBar->setVisible(false);
    _layout->addWidget(_progressBar);
    
    // Status label
    _statusLabel = new QLabel("Ready");
    _statusLabel->setStyleSheet(STATUS_NORMAL);
    _layout->addWidget(_statusLabel);
    
    // Update initial control states
    updateControlStates();
}

void GameLocationWidget::setupConnections() {
    connect(_steamRadio, &QRadioButton::toggled, this, &GameLocationWidget::onInstallationTypeChanged);
    connect(_executableRadio, &QRadioButton::toggled, this, &GameLocationWidget::onInstallationTypeChanged);
    connect(_steamAppIdEdit, &QLineEdit::textChanged, this, &GameLocationWidget::onSteamAppIdChanged);
    connect(_executableLocationEdit, &QLineEdit::textChanged, this, &GameLocationWidget::onExecutableLocationChanged);
    connect(_dataDirectoryEdit, &QLineEdit::textChanged, this, &GameLocationWidget::onDataDirectoryChanged);
    connect(_browseExecutableButton, &QPushButton::clicked, this, &GameLocationWidget::onBrowseExecutable);
    connect(_browseDataDirectoryButton, &QPushButton::clicked, this, &GameLocationWidget::onBrowseDataDirectory);
    connect(_autoDetectButton, &QPushButton::clicked, this, &GameLocationWidget::onAutoDetect);
}

Settings::GameInstallationType GameLocationWidget::getInstallationType() const {
    return _steamRadio->isChecked() ? Settings::GameInstallationType::STEAM : Settings::GameInstallationType::EXECUTABLE;
}

void GameLocationWidget::setInstallationType(Settings::GameInstallationType type) {
    if (type == Settings::GameInstallationType::STEAM) {
        _steamRadio->setChecked(true);
    } else {
        _executableRadio->setChecked(true);
    }
    updateControlStates();
}

std::string GameLocationWidget::getSteamAppId() const {
    QString appId = _steamAppIdEdit->text().trimmed();
    return appId.isEmpty() ? DEFAULT_STEAM_APPID : appId.toStdString();
}

void GameLocationWidget::setSteamAppId(const std::string& appId) {
    _steamAppIdEdit->setText(QString::fromStdString(appId));
}

std::filesystem::path GameLocationWidget::getExecutableLocation() const {
    QString path = _executableLocationEdit->text().trimmed();
    return path.isEmpty() ? std::filesystem::path{} : std::filesystem::path(path.toStdString());
}

void GameLocationWidget::setExecutableLocation(const std::filesystem::path& location) {
    _executableLocationEdit->setText(QString::fromStdString(location.string()));
}

std::filesystem::path GameLocationWidget::getDataDirectory() const {
    QString path = _dataDirectoryEdit->text().trimmed();
    return path.isEmpty() ? std::filesystem::path{} : std::filesystem::path(path.toStdString());
}

void GameLocationWidget::setDataDirectory(const std::filesystem::path& location) {
    _dataDirectoryEdit->setText(QString::fromStdString(location.string()));
}

void GameLocationWidget::setStatusMessage(const QString& message, const QString& styleClass) {
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
    
    emit statusChanged(message, styleClass);
}

void GameLocationWidget::updateControlStates() {
    bool steamSelected = _steamRadio->isChecked();
    
    // Enable/disable controls based on selection
    _steamAppIdEdit->setEnabled(steamSelected);
    _executableLocationEdit->setEnabled(!steamSelected);
    _browseExecutableButton->setEnabled(!steamSelected);
    _dataDirectoryEdit->setEnabled(!steamSelected);
    _browseDataDirectoryButton->setEnabled(!steamSelected);
}

void GameLocationWidget::onInstallationTypeChanged() {
    updateControlStates();
    emit installationTypeChanged();
    emit configurationChanged();
}

void GameLocationWidget::onSteamAppIdChanged() {
    emit configurationChanged();
}

void GameLocationWidget::onExecutableLocationChanged() {
    emit configurationChanged();
}

void GameLocationWidget::onDataDirectoryChanged() {
    emit configurationChanged();
}

void GameLocationWidget::onBrowseExecutable() {
    QString currentPath = _executableLocationEdit->text();
    QString startPath = currentPath.isEmpty() ? 
        QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) : 
        currentPath;
    
#ifdef __APPLE__
    // For executable, allow both .app bundles and directories
    QFileDialog dialog(this, "Select Fallout 2 Executable", startPath);
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
            _executableLocationEdit->setText(gameDir);
            validateGameLocation(gameDir, false);
            emit configurationChanged();
        }
    }
#else
    QString gameDir = QFileDialog::getExistingDirectory(this, "Select Fallout 2 Executable Directory", startPath);
    if (!gameDir.isEmpty()) {
        _executableLocationEdit->setText(gameDir);
        validateGameLocation(gameDir, false);
        emit configurationChanged();
    }
#endif
}

void GameLocationWidget::onBrowseDataDirectory() {
    QString currentPath = _dataDirectoryEdit->text();
    QString startPath = currentPath.isEmpty() ? 
        QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) : 
        currentPath;
    
    QString gameDir = QFileDialog::getExistingDirectory(this, "Select Fallout 2 Game Data Directory", startPath);
    if (!gameDir.isEmpty()) {
        _dataDirectoryEdit->setText(gameDir);
        emit configurationChanged();
    }
}

void GameLocationWidget::onAutoDetect() {
    _autoDetectButton->setEnabled(false);
    _progressBar->setVisible(true);
    _progressBar->setRange(0, 0); // Indeterminate progress
    setStatusMessage("Detecting Fallout 2 game installations...", "normal");
    
    QApplication::processEvents(); // Update UI
    
    auto detectedInstallations = Settings::detectFallout2InstallationsDetailed();
    
    _progressBar->setVisible(false);
    _autoDetectButton->setEnabled(true);
    
    if (!detectedInstallations.empty()) {
        // Populate both Steam and Executable fields based on detected installations
        bool foundSteam = false;
        bool foundExecutable = false;
        QString statusMessages;
        
        for (const auto& installation : detectedInstallations) {
            QString pathStr = QString::fromStdString(installation.path.string());
            QString description = QString::fromStdString(installation.description);
            
            if (installation.type == Settings::GameInstallationType::STEAM && !foundSteam) {
                _steamAppIdEdit->setText(DEFAULT_STEAM_APPID);
                foundSteam = true;
                statusMessages += QString("Steam: %1; ").arg(description);
            } else if (installation.type == Settings::GameInstallationType::EXECUTABLE && !foundExecutable) {
                _executableLocationEdit->setText(pathStr);
                foundExecutable = true;
                statusMessages += QString("Executable: %1; ").arg(description);
            }
        }
        
        // Set the radio button to the first detected type
        if (foundSteam && !foundExecutable) {
            _steamRadio->setChecked(true);
        } else if (foundExecutable) {
            _executableRadio->setChecked(true);
        }
        
        statusMessages = statusMessages.trimmed();
        if (statusMessages.endsWith(";")) {
            statusMessages.chop(1);
        }
        
        setStatusMessage(QString("Auto-detected installations: %1").arg(statusMessages), "success");
        emit configurationChanged();
    } else {
        setStatusMessage("No Fallout 2 game installations detected automatically. Please select the directory manually.", "warning");
        onBrowseExecutable();

        const QString manualSelection = _executableLocationEdit->text().trimmed();
        if (!manualSelection.isEmpty()) {
            _executableRadio->setChecked(true);
            validateGameLocation(manualSelection, false);
            emit configurationChanged();
        }
    }
}

void GameLocationWidget::validateGameLocation(const QString& gameDir, bool isSteam) {
    std::filesystem::path gamePath(gameDir.toStdString());
    
#ifdef __APPLE__
    if (isSteam && gameDir.endsWith(".app")) {
        // For .app bundles on macOS, just check if it exists
        if (std::filesystem::exists(gamePath)) {
            setStatusMessage("Valid macOS Fallout 2 application selected.", "success");
        } else {
            setStatusMessage("Warning: Selected application does not exist.", "error");
        }
    } else {
        // Check for data directory and executable files
        bool hasDataDir = std::filesystem::exists(gamePath / "data");
        bool hasExecutable = std::filesystem::exists(gamePath / "fallout2.exe") || 
                           std::filesystem::exists(gamePath / "Fallout2.exe") ||
                           std::filesystem::exists(gamePath / "fallout2") ||
                           std::filesystem::exists(gamePath / "Fallout2.app") ||
                           std::filesystem::exists(gamePath / "fallout2-ce") ||
                           std::filesystem::exists(gamePath / "fallout2-ce.app");
        
        if (hasDataDir || hasExecutable) {
            setStatusMessage(isSteam ? "Valid Steam Fallout 2 installation directory selected." : 
                                      "Valid Fallout 2 executable installation directory selected.", "success");
        } else {
            setStatusMessage("Warning: Selected directory may not be a valid Fallout 2 installation.", "warning");
        }
    }
#else
    // Windows/Linux validation
    bool hasDataDir = std::filesystem::exists(gamePath / "data");
    bool hasExecutable = std::filesystem::exists(gamePath / "fallout2.exe") || 
                       std::filesystem::exists(gamePath / "Fallout2.exe") ||
                       std::filesystem::exists(gamePath / "fallout2") ||
                       std::filesystem::exists(gamePath / "Fallout2") ||
                       std::filesystem::exists(gamePath / "fallout2-ce.exe") ||
                       std::filesystem::exists(gamePath / "Fallout2-ce.exe") ||
                       std::filesystem::exists(gamePath / "fallout2-ce") ||
                       std::filesystem::exists(gamePath / "Fallout2-ce");
    
    if (hasDataDir && hasExecutable) {
        setStatusMessage(isSteam ? "Valid Steam Fallout 2 installation directory selected." : 
                                  "Valid Fallout 2 executable installation directory selected.", "success");
    } else {
        setStatusMessage("Warning: Selected directory may not be a valid Fallout 2 installation.", "warning");
    }
#endif
}

} // namespace geck
