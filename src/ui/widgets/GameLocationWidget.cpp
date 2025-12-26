#include "GameLocationWidget.h"
#include "../../util/Settings.h"
#include "../UIConstants.h"

#include <QApplication>
#include <QStyle>
#include <QStandardPaths>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <spdlog/spdlog.h>

namespace geck {

using namespace ui::constants;

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
    , _progressBar(nullptr) {

    setupUI();
    setupConnections();
}

void GameLocationWidget::setupUI() {
    _layout = new QVBoxLayout(this);

    // Help text
    _helpLabel = new QLabel(
        "Select the Fallout 2 game installation directory. This is used for the Play feature to launch the game with your current map.");
    _helpLabel->setWordWrap(true);
    _helpLabel->setStyleSheet(ui::theme::styles::helpText());
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
    _steamAppIdEdit->setPlaceholderText(ui::defaults::STEAM_APPID);
    _steamAppIdEdit->setEnabled(false);
    _steamAppIdEdit->setMaximumWidth(STEAM_APPID_WIDTH);
    _steamLayout->addWidget(_steamAppIdEdit);

    _steamHelpLabel = new QLabel("(Default: 38410 for Fallout 2)");
    _steamHelpLabel->setStyleSheet(ui::theme::styles::smallLabel());
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
    _executableLocationEdit->setPlaceholderText("Path to Fallout 2 executable (e.g., fallout2.exe)...");
    _executableLayout->addWidget(_executableLocationEdit);

    _browseExecutableButton = new QPushButton("Browse...");
    _browseExecutableButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    _browseExecutableButton->setToolTip("Browse for Fallout 2 executable file");
    _executableLayout->addWidget(_browseExecutableButton);

    _layout->addLayout(_executableLayout);

    // Game data directory (for executable installs)
    _dataDirectoryLabel = new QLabel("Game Data Directory:");
    _dataDirectoryLabel->setContentsMargins(INDENT_MARGIN, 8, 0, 0);
    _dataDirectoryLabel->setStyleSheet(ui::theme::styles::smallLabel());
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
    return appId.isEmpty() ? ui::defaults::STEAM_APPID : appId.toStdString();
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
    QString startPath = currentPath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) : currentPath;

#ifdef __APPLE__
    // For executable, allow both .app bundles and executables
    QString selectedFile = QFileDialog::getOpenFileName(this,
        "Select Fallout 2 Executable",
        startPath,
        "Applications (*.app);;All Files (*)");

    if (!selectedFile.isEmpty()) {
        _executableLocationEdit->setText(selectedFile);

        // Auto-set data directory if empty
        if (_dataDirectoryEdit->text().trimmed().isEmpty()) {
            QFileInfo fileInfo(selectedFile);
            QString parentDir;
            if (selectedFile.endsWith(".app")) {
                // For .app bundles, use the parent directory
                // TODO: perhaps use Contents/Resources if it is GOG installation
                parentDir = fileInfo.dir().absolutePath();
            } else {
                // For regular executables, use the executable's directory
                parentDir = fileInfo.dir().absolutePath();
            }
            _dataDirectoryEdit->setText(parentDir);
            spdlog::info("Auto-set data directory to: {}", parentDir.toStdString());
        }

        validateGameLocation(selectedFile, false);
        emit configurationChanged();
    }
#else
    QString filters;
#ifdef _WIN32
    // Windows: Default to fallout2.exe pattern but allow all executables
    filters = "Fallout 2 Executable (fallout2.exe fallout2-ce.exe);;All Executables (*.exe)";
#else
    // Linux: Show all files by default (no restrictive filters)
    filters = "All Files (*)";
#endif

    QString selectedFile = QFileDialog::getOpenFileName(this,
        "Select Fallout 2 Executable",
        startPath,
        filters);

    if (!selectedFile.isEmpty()) {
        _executableLocationEdit->setText(selectedFile);

        // Auto-set data directory if empty
        if (_dataDirectoryEdit->text().trimmed().isEmpty()) {
            QFileInfo fileInfo(selectedFile);
            QString parentDir = fileInfo.dir().absolutePath();
            _dataDirectoryEdit->setText(parentDir);
            spdlog::info("Auto-set data directory to: {}", parentDir.toStdString());
        }

        validateGameLocation(selectedFile, false);
        emit configurationChanged();
    }
#endif
}

void GameLocationWidget::onBrowseDataDirectory() {
    QString currentPath = _dataDirectoryEdit->text();
    QString startPath = currentPath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) : currentPath;

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
                _steamAppIdEdit->setText(ui::defaults::STEAM_APPID);
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

void GameLocationWidget::validateGameLocation(const QString& gamePath, bool isSteam) {
    std::filesystem::path path(gamePath.toStdString());

    // Check if the path is an executable file or a directory
    bool isFile = std::filesystem::is_regular_file(path);
    bool isDirectory = std::filesystem::is_directory(path);

    if (isFile) {
        // User selected an executable file
        QString fileName = QString::fromStdString(path.filename().string()).toLower();
        bool isValidExecutable = fileName.contains("fallout2") || fileName.contains("fallout 2") || fileName.endsWith(".app");

        if (isValidExecutable) {
            setStatusMessage("Valid Fallout 2 executable selected.", "success");

            // Check if data directory has required files
            std::filesystem::path dataDir(_dataDirectoryEdit->text().toStdString());
            if (!dataDir.empty() && std::filesystem::exists(dataDir / "data")) {
                setStatusMessage("Valid Fallout 2 executable and data directory selected.", "success");
            } else if (!dataDir.empty()) {
                setStatusMessage("Executable selected. Warning: Data directory may not contain game files.", "warning");
            }
        } else {
            setStatusMessage("Warning: Selected file may not be a valid Fallout 2 executable.", "warning");
        }
    } else if (isDirectory) {
        // User selected a directory (legacy behavior for compatibility)
        bool hasDataDir = std::filesystem::exists(path / "data");
        bool hasExecutable = std::filesystem::exists(path / "fallout2.exe") || std::filesystem::exists(path / "Fallout2.exe") || std::filesystem::exists(path / "fallout2") || std::filesystem::exists(path / "Fallout2") || std::filesystem::exists(path / "fallout2-ce.exe") || std::filesystem::exists(path / "Fallout2-ce.exe") || std::filesystem::exists(path / "fallout2-ce") || std::filesystem::exists(path / "Fallout2-ce");

        if (hasDataDir && hasExecutable) {
            setStatusMessage(isSteam ? "Valid Steam Fallout 2 installation directory selected." : "Valid Fallout 2 installation directory selected.", "success");
        } else {
            setStatusMessage("Warning: Selected directory may not be a valid Fallout 2 installation.", "warning");
        }
    } else {
        setStatusMessage("Warning: Selected path does not exist.", "error");
    }
}

} // namespace geck
