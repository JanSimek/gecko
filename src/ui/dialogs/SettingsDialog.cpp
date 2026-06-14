#include "SettingsDialog.h"
#include "ui/widgets/DataPathsWidget.h"
#include "ui/widgets/GameLocationWidget.h"
#include "ui/widgets/TextEditorWidget.h"
#include "ui/UIConstants.h"
#include "ui/theme/ThemeManager.h"
#include "ui/Settings.h"

#include <QApplication>
#include <QStyle>
#include <QMessageBox>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <array>
#include <spdlog/spdlog.h>

namespace geck {

using namespace ui::constants;
using namespace ui::defaults;

SettingsDialog::SettingsDialog(std::shared_ptr<Settings> settings, QWidget* parent)
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
    , _settings(std::move(settings))
    , _hasChanges(false) {

    setWindowTitle("Preferences");
    setModal(true);
    setMinimumSize(dialog_sizes::SETTINGS_MIN_WIDTH, dialog_sizes::SETTINGS_MIN_HEIGHT);
    resize(dialog_sizes::SETTINGS_DEFAULT_WIDTH, dialog_sizes::SETTINGS_DEFAULT_HEIGHT);

    setupUI();
    loadSettings();
    updateUI();
}

void SettingsDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE);
    _mainLayout->setSpacing(SPACING_LOOSE);

    setupTabs();

    _statusLabel = new QLabel(READY_STATUS);
    _statusLabel->setStyleSheet(ui::theme::styles::statusNormal());
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
    setupColorsTab();

    _mainLayout->addWidget(_tabWidget);
}

void SettingsDialog::setupColorsTab() {
    _colorsTab = new QWidget();
    auto* layout = new QVBoxLayout(_colorsTab);
    layout->setContentsMargins(SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE);
    layout->setSpacing(SPACING_NORMAL);

    auto* intro = new QLabel("Colours used to highlight the current selection. Click a swatch to change it.");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    const std::array<std::pair<QString, QString>, 4> rows{ {
        { "object", "Objects" },
        { "wall", "Walls" },
        { "critter", "Critters" },
        { "tile", "Tiles" },
    } };

    for (const auto& [key, label] : rows) {
        auto* row = new QHBoxLayout();
        auto* name = new QLabel(label + ":");
        name->setMinimumWidth(90);

        auto* swatch = new QPushButton();
        swatch->setFixedSize(48, 24);
        swatch->setCursor(Qt::PointingHandCursor);
        _colorSwatches[key] = swatch;

        connect(swatch, &QPushButton::clicked, this, [this, key, label]() {
            const QColor chosen = QColorDialog::getColor(_selectionColors.value(key, Qt::white), this,
                QString("Select the %1 selection colour").arg(label.toLower()));
            if (chosen.isValid()) {
                _selectionColors[key] = chosen;
                updateColorButton(key);
                onWidgetChanged();
            }
        });

        row->addWidget(name);
        row->addWidget(swatch);
        row->addStretch();
        layout->addLayout(row);
    }

    layout->addStretch();
    _tabWidget->addTab(_colorsTab, "Selection Colours");
}

void SettingsDialog::updateColorButton(const QString& key) {
    auto* swatch = _colorSwatches.value(key, nullptr);
    if (!swatch) {
        return;
    }
    const QColor color = _selectionColors.value(key);
    swatch->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(color.name()));
    swatch->setToolTip(color.name());
}

void SettingsDialog::setupGeneralTab() {
    _generalTab = new QWidget();
    _generalTabLayout = new QVBoxLayout(_generalTab);
    _generalTabLayout->setContentsMargins(SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE);
    _generalTabLayout->setSpacing(SPACING_LOOSE);

    _dataPathsWidget = new DataPathsWidget(_settings);
    _generalTabLayout->addWidget(_dataPathsWidget);

    _gameLocationWidget = new GameLocationWidget();
    _generalTabLayout->addWidget(_gameLocationWidget);

    _generalTabLayout->addStretch();

    _tabWidget->addTab(_generalTab, "General");

    connect(_dataPathsWidget, &DataPathsWidget::dataPathsChanged, this, &SettingsDialog::onWidgetChanged);
    connect(_dataPathsWidget, &DataPathsWidget::statusChanged, this, &SettingsDialog::onStatusChanged);
    connect(_gameLocationWidget, &GameLocationWidget::configurationChanged, this, &SettingsDialog::onWidgetChanged);
    connect(_gameLocationWidget, &GameLocationWidget::statusChanged, this, &SettingsDialog::onStatusChanged);
}

void SettingsDialog::setupEditorTab() {
    _editorTab = new QWidget();
    _editorTabLayout = new QVBoxLayout(_editorTab);
    _editorTabLayout->setContentsMargins(SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE);
    _editorTabLayout->setSpacing(SPACING_LOOSE);

    _textEditorWidget = new TextEditorWidget();
    _editorTabLayout->addWidget(_textEditorWidget);

    _editorTabLayout->addStretch();

    _tabWidget->addTab(_editorTab, "Text Editor");

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
    auto& settings = *_settings;

    // Kept for the Reset action so changes can be reverted
    _originalDataPaths = settings.getDataPaths();

    _dataPathsWidget->setDataPaths(_originalDataPaths);

    _textEditorWidget->setEditorMode(settings.getTextEditorMode());
    _textEditorWidget->setCustomEditorPath(settings.getCustomEditorPath());

    _gameLocationWidget->setExecutableLocation(settings.getExecutableGameLocation());
    _gameLocationWidget->setDataDirectory(settings.getGameDataDirectory());

    // Selection colours. Defaults mirror RenderingEngine::SelectionPalette — keep in sync.
    const QMap<QString, QColor> defaults{
        { "object", QColor(140, 110, 220) },
        { "wall", QColor(74, 206, 168) },
        { "critter", QColor(224, 180, 96) },
        { "tile", QColor(74, 144, 226) },
    };
    for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) {
        _selectionColors[it.key()] = settings.getSelectionColor(it.key(), it.value());
        updateColorButton(it.key());
    }

    _hasChanges = false;
    updateUI();
}

void SettingsDialog::saveSettings() {
    auto& settings = *_settings;

    auto dataPaths = _dataPathsWidget->getDataPaths();
    bool pathsHaveChanged = dataPaths != _originalDataPaths;
    settings.setDataPaths(dataPaths);

    settings.setTextEditorMode(_textEditorWidget->getEditorMode());
    if (_textEditorWidget->getEditorMode() == Settings::TextEditorMode::CUSTOM) {
        settings.setCustomEditorPath(_textEditorWidget->getCustomEditorPath());
    } else {
        settings.setCustomEditorPath(""); // Clear custom path when using system default
    }

    settings.setExecutableGameLocation(_gameLocationWidget->getExecutableLocation());
    settings.setGameDataDirectory(_gameLocationWidget->getDataDirectory());

    for (auto it = _selectionColors.constBegin(); it != _selectionColors.constEnd(); ++it) {
        settings.setSelectionColor(it.key(), it.value());
    }

    settings.save();

    _originalDataPaths = dataPaths;
    _hasChanges = false;

    setMainStatus("Settings saved successfully", "success");
    spdlog::info("Settings saved from preferences dialog");

    Q_EMIT settingsSaved(pathsHaveChanged);
}

void SettingsDialog::updateUI() {
    _applyButton->setEnabled(_hasChanges);
}

void SettingsDialog::setMainStatus(const QString& message, const QString& styleClass) {
    _statusLabel->setText(message);

    if (styleClass == "warning") {
        _statusLabel->setStyleSheet(ui::theme::styles::statusWarning());
    } else if (styleClass == "error") {
        _statusLabel->setStyleSheet(ui::theme::styles::statusError());
    } else if (styleClass == "success") {
        _statusLabel->setStyleSheet(ui::theme::styles::statusSuccess());
    } else if (styleClass == "info") {
        _statusLabel->setStyleSheet(ui::theme::styles::statusInfo());
    } else {
        _statusLabel->setStyleSheet(ui::theme::styles::statusNormal());
    }
}

void SettingsDialog::onWidgetChanged() {
    _hasChanges = true;
    updateUI();
}

void SettingsDialog::onStatusChanged(const QString& message, const QString& styleClass) {
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
