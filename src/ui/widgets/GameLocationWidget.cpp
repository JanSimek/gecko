#include "GameLocationWidget.h"
#include "util/GameDataPathResolver.h"
#include "ui/IconHelper.h"
#include "ui/Settings.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QFormLayout>
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

    // A form puts each label beside its field instead of on a line of its own, which halves the
    // rows this panel needs and lines the two paths up with each other.
    auto* pathForm = new QFormLayout();
    // Left-aligned: right alignment leaves labels of different lengths with ragged left edges.
    pathForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pathForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    pathForm->setHorizontalSpacing(ui::theme::spacing::NORMAL);
    pathForm->setVerticalSpacing(ui::theme::spacing::NORMAL);
    pathForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    // Long labels drop above their field rather than squeezing it when the dialog is narrow.
    pathForm->setRowWrapPolicy(QFormLayout::WrapLongRows);

    _executableLabel = new QLabel("Executable:");

    _executableLayout = new QHBoxLayout();
    _executableLayout->setSpacing(ui::theme::spacing::NORMAL);
    _executableLocationEdit = new QLineEdit();
    _executableLocationEdit->setPlaceholderText("Path to the Fallout 2 executable (e.g. fallout2.exe)...");
    _executableLayout->addWidget(_executableLocationEdit);

    _browseExecutableButton = new QPushButton("Browse...");
    _browseExecutableButton->setIcon(createIcon(":/icons/actions/open.svg"));
    _browseExecutableButton->setToolTip("Browse for Fallout 2 executable file");
    _executableLayout->addWidget(_browseExecutableButton);
    pathForm->addRow(_executableLabel, _executableLayout);

    // Named for what it has to be: the folder holding data/, not the data folder itself, which is
    // what the validation checks for and what the map and configs are written relative to.
    _dataDirectoryLabel = new QLabel("Game folder:");

    _dataDirectoryLayout = new QHBoxLayout();
    _dataDirectoryLayout->setSpacing(ui::theme::spacing::NORMAL);
    _dataDirectoryEdit = new QLineEdit();
    _dataDirectoryEdit->setPlaceholderText("Folder containing data/ (e.g. .../GOG.com/Fallout 2)...");
    _dataDirectoryEdit->setToolTip(
        "The game folder itself - the one that contains data/, master.dat and the executable.\n"
        "Not the data folder inside it.");
    _dataDirectoryLayout->addWidget(_dataDirectoryEdit);

    _browseDataDirectoryButton = new QPushButton("Browse...");
    _browseDataDirectoryButton->setIcon(createIcon(":/icons/actions/open.svg"));
    _browseDataDirectoryButton->setToolTip("Browse for the Fallout 2 game folder");
    _dataDirectoryLayout->addWidget(_browseDataDirectoryButton);
    pathForm->addRow(_dataDirectoryLabel, _dataDirectoryLayout);

    _layout->addLayout(pathForm);

    _controlLayout = new QHBoxLayout();
    _controlLayout->setContentsMargins(0, 0, 0, 0);
    _controlLayout->addStretch();

    _autoDetectButton = new QPushButton("Auto-Detect");
    _autoDetectButton->setIcon(createIcon(":/icons/ui/auto-detect.svg"));
    _autoDetectButton->setToolTip("Automatically detect Fallout 2 game installations");
    _controlLayout->addWidget(_autoDetectButton);

    // Consistent icon size + minimum height so the buttons don't shrink and clip their icons on resize.
    for (QPushButton* btn : { _browseExecutableButton, _browseDataDirectoryButton, _autoDetectButton }) {
        geck::ui::styleActionButton(btn);
    }
    // One height per row so the centre lines coincide: QFormLayout otherwise top-aligns a 26px
    // label against a 30px field, leaving its text two pixels high.
    for (QWidget* rowWidget : { static_cast<QWidget*>(_executableLocationEdit),
             static_cast<QWidget*>(_dataDirectoryEdit), static_cast<QWidget*>(_executableLabel),
             static_cast<QWidget*>(_dataDirectoryLabel) }) {
        rowWidget->setMinimumHeight(ui::constants::sizes::ACTION_BUTTON_HEIGHT);
    }

    _layout->addLayout(_controlLayout);

    _progressBar = new QProgressBar();
    _progressBar->setVisible(false);
    _layout->addWidget(_progressBar);

    _statusLabel = new QLabel();
    _statusLabel->setWordWrap(true);
    _statusLabel->setStyleSheet(ui::theme::styles::statusNormal());
    _statusLabel->setVisible(false);
    _layout->addWidget(_statusLabel);
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
    // In this section: a shared line meant adding a data path wiped out the executable warning.
    ui::setStatusText(_statusLabel, message, styleClass);
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
                spdlog::debug("Auto-set data directory to: {}", dataDirectory->string());
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
            spdlog::debug("Auto-set data directory to: {}", parentDir.toStdString());
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
