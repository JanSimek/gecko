#include "GameLocationWidget.h"
#include "util/GameDataPathResolver.h"
#include "ui/Settings.h"
#include "ui/common/ButtonStyle.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QStyle>
#include <QStandardPaths>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>

namespace geck {

namespace {

    bool looksLikeFalloutExecutable(const QString& lowercaseFileName) {
        return lowercaseFileName.contains("fallout2") || lowercaseFileName.contains("fallout 2")
            || lowercaseFileName.endsWith(".app");
    }

    bool directoryHasFalloutExecutable(const std::filesystem::path& dir) {
        static constexpr std::array<const char*, 8> kExecutableNames = {
            "fallout2.exe", "Fallout2.exe", "fallout2", "Fallout2",
            "fallout2-ce.exe", "Fallout2-ce.exe", "fallout2-ce", "Fallout2-ce"
        };
        return std::ranges::any_of(kExecutableNames,
            [&dir](const char* name) { return std::filesystem::exists(dir / name); });
    }

} // namespace

GameLocationWidget::GameLocationWidget(QWidget* parent)
    : QGroupBox("Fallout 2 Game Location", parent)
    , _layout(nullptr)
    , _helpLabel(nullptr)
    , _executableLabel(nullptr)
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

    _helpLabel = new QLabel(
        "Select the Fallout 2 game installation. This is used for the Play feature to launch the game with your current map.");
    _helpLabel->setWordWrap(true);
    _helpLabel->setStyleSheet(ui::theme::styles::helpText());
    _layout->addWidget(_helpLabel);

    _executableLabel = new QLabel("Fallout 2 Executable (or GOG install):");
    _layout->addWidget(_executableLabel);

    _executableLayout = new QHBoxLayout();
    _executableLayout->setContentsMargins(ui::theme::spacing::MARGIN_INDENT, 0, 0, 0);

    _executableLocationEdit = new QLineEdit();
    _executableLocationEdit->setPlaceholderText("Path to Fallout 2 executable (e.g., fallout2.exe)...");
    _executableLayout->addWidget(_executableLocationEdit);

    _browseExecutableButton = new QPushButton("Browse...");
    _browseExecutableButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    _browseExecutableButton->setToolTip("Browse for Fallout 2 executable file");
    _executableLayout->addWidget(_browseExecutableButton);

    _layout->addLayout(_executableLayout);

    // Game data directory
    _dataDirectoryLabel = new QLabel("Game Data Directory:");
    _dataDirectoryLabel->setContentsMargins(ui::theme::spacing::MARGIN_INDENT, 8, 0, 0);
    _dataDirectoryLabel->setStyleSheet(ui::theme::styles::smallLabel());
    _layout->addWidget(_dataDirectoryLabel);

    _dataDirectoryLayout = new QHBoxLayout();
    _dataDirectoryLayout->setContentsMargins(ui::theme::spacing::MARGIN_INDENT, 0, 0, 0);

    _dataDirectoryEdit = new QLineEdit();
    _dataDirectoryEdit->setPlaceholderText("Path to game directory containing ddraw.ini...");
    _dataDirectoryLayout->addWidget(_dataDirectoryEdit);

    _browseDataDirectoryButton = new QPushButton("Browse...");
    _browseDataDirectoryButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    _browseDataDirectoryButton->setToolTip("Browse for Fallout 2 game data directory");
    _dataDirectoryLayout->addWidget(_browseDataDirectoryButton);

    _layout->addLayout(_dataDirectoryLayout);

    _controlLayout = new QHBoxLayout();
    _controlLayout->addStretch();

    _autoDetectButton = new QPushButton("Auto-Detect");
    _autoDetectButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    _autoDetectButton->setToolTip("Automatically detect Fallout 2 game installations");
    _controlLayout->addWidget(_autoDetectButton);

    // Consistent icon size + minimum height so the buttons don't shrink and clip their icons on resize.
    for (QPushButton* btn : { _browseExecutableButton, _browseDataDirectoryButton, _autoDetectButton }) {
        geck::ui::styleActionButton(btn);
    }

    _layout->addLayout(_controlLayout);

    _progressBar = new QProgressBar();
    _progressBar->setVisible(false);
    _layout->addWidget(_progressBar);
}

void GameLocationWidget::setupConnections() {
    connect(_executableLocationEdit, &QLineEdit::textChanged, this, &GameLocationWidget::onExecutableLocationChanged);
    connect(_dataDirectoryEdit, &QLineEdit::textChanged, this, &GameLocationWidget::onDataDirectoryChanged);
    connect(_browseExecutableButton, &QPushButton::clicked, this, &GameLocationWidget::onBrowseExecutable);
    connect(_browseDataDirectoryButton, &QPushButton::clicked, this, &GameLocationWidget::onBrowseDataDirectory);
    connect(_autoDetectButton, &QPushButton::clicked, this, &GameLocationWidget::onAutoDetect);
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
    Q_EMIT statusChanged(message, styleClass);
}

void GameLocationWidget::onExecutableLocationChanged() {
    Q_EMIT configurationChanged();
}

void GameLocationWidget::onDataDirectoryChanged() {
    Q_EMIT configurationChanged();
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
            if (const auto dataDirectory = util::resolveGameDataRoot(std::filesystem::path(selectedFile.toStdString()))) {
                _dataDirectoryEdit->setText(QString::fromStdString(dataDirectory->string()));
                spdlog::info("Auto-set data directory to: {}", dataDirectory->string());
            }
        }

        validateGameLocation(selectedFile);
        Q_EMIT configurationChanged();
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

        validateGameLocation(selectedFile);
        Q_EMIT configurationChanged();
    }
#endif
}

void GameLocationWidget::onBrowseDataDirectory() {
    QString currentPath = _dataDirectoryEdit->text();
    QString startPath = currentPath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) : currentPath;

    QString gameDir = QFileDialog::getExistingDirectory(this, "Select Fallout 2 Game Data Directory", startPath);
    if (!gameDir.isEmpty()) {
        _dataDirectoryEdit->setText(gameDir);
        Q_EMIT configurationChanged();
    }
}

void GameLocationWidget::onAutoDetect() {
    _autoDetectButton->setEnabled(false);
    _progressBar->setVisible(true);
    _progressBar->setRange(0, 0); // Indeterminate progress
    setStatusMessage("Detecting Fallout 2 game installations...", "normal");

    QApplication::processEvents();

    auto detectedInstallations = Settings::detectFallout2InstallationsDetailed();

    _progressBar->setVisible(false);
    _autoDetectButton->setEnabled(true);

    if (!detectedInstallations.empty()) {
        const auto& installation = detectedInstallations.front();
        _executableLocationEdit->setText(QString::fromStdString(installation.path.string()));
        if (const auto dataDirectory = util::resolveGameDataRoot(installation.path)) {
            _dataDirectoryEdit->setText(QString::fromStdString(dataDirectory->string()));
        }

        setStatusMessage(QString("Auto-detected installation: %1").arg(QString::fromStdString(installation.description)), "success");
        Q_EMIT configurationChanged();
    } else {
        setStatusMessage("No Fallout 2 game installations detected automatically. Please select the directory manually.", "warning");
        onBrowseExecutable();

        const QString manualSelection = _executableLocationEdit->text().trimmed();
        if (!manualSelection.isEmpty()) {
            validateGameLocation(manualSelection);
            Q_EMIT configurationChanged();
        }
    }
}

void GameLocationWidget::validateGameLocation(const QString& gamePath) {
    const std::filesystem::path path(gamePath.toStdString());

    if (std::filesystem::is_regular_file(path)) {
        validateExecutableFile(path);
    } else if (std::filesystem::is_directory(path)) {
        validateInstallDirectory(path);
    } else {
        setStatusMessage("Warning: Selected path does not exist.", "error");
    }
}

void GameLocationWidget::validateExecutableFile(const std::filesystem::path& path) {
    const QString fileName = QString::fromStdString(path.filename().string()).toLower();
    if (!looksLikeFalloutExecutable(fileName)) {
        setStatusMessage("Warning: Selected file may not be a valid Fallout 2 executable.", "warning");
        return;
    }

    setStatusMessage("Valid Fallout 2 executable selected.", "success");

    const std::filesystem::path dataDir(_dataDirectoryEdit->text().toStdString());
    if (dataDir.empty()) {
        return;
    }
    if (std::filesystem::exists(dataDir / "data")) {
        setStatusMessage("Valid Fallout 2 executable and data directory selected.", "success");
    } else {
        setStatusMessage("Executable selected. Warning: Data directory may not contain game files.", "warning");
    }
}

void GameLocationWidget::validateInstallDirectory(const std::filesystem::path& path) {
    // Legacy behaviour: accept a directory that looks like a Fallout 2 install.
    const bool hasDataDir = std::filesystem::exists(path / "data");
    const bool hasExecutable = directoryHasFalloutExecutable(path);
    if (hasDataDir && hasExecutable) {
        setStatusMessage("Valid Fallout 2 installation directory selected.", "success");
    } else {
        setStatusMessage("Warning: Selected directory may not be a valid Fallout 2 installation.", "warning");
    }
}

} // namespace geck
