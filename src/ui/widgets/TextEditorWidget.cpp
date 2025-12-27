#include "TextEditorWidget.h"
#include "../UIConstants.h"

#include <QApplication>
#include <QStyle>
#include <QStandardPaths>
#include <QFileDialog>
#include <QFileInfo>

namespace geck {

using namespace ui::constants;

TextEditorWidget::TextEditorWidget(QWidget* parent)
    : QGroupBox("Text Editor", parent)
    , _layout(nullptr)
    , _helpLabel(nullptr)
    , _systemEditorRadio(nullptr)
    , _customEditorRadio(nullptr)
    , _customEditorLayout(nullptr)
    , _customEditorPathEdit(nullptr)
    , _browseEditorButton(nullptr) {

    setupUI();
    setupConnections();
}

void TextEditorWidget::setupUI() {
    _layout = new QVBoxLayout(this);

    // Help text
    _helpLabel = new QLabel(
        "Choose how to open text files (txt, gam, lst, ini, etc.) from the file browser.");
    _helpLabel->setWordWrap(true);
    _helpLabel->setStyleSheet(ui::theme::styles::helpText());
    _layout->addWidget(_helpLabel);

    // Radio buttons
    _systemEditorRadio = new QRadioButton("Use default system editor");
    _systemEditorRadio->setChecked(true); // Default selection
    _layout->addWidget(_systemEditorRadio);

    _customEditorRadio = new QRadioButton("Use custom editor:");
    _layout->addWidget(_customEditorRadio);

    // Custom editor path layout
    _customEditorLayout = new QHBoxLayout();
    _customEditorLayout->setContentsMargins(ui::theme::spacing::MARGIN_INDENT, 0, 0, 0); // Indent under radio button

    _customEditorPathEdit = new QLineEdit();
    _customEditorPathEdit->setPlaceholderText("Path to editor executable...");
    _customEditorPathEdit->setEnabled(false);
    _customEditorLayout->addWidget(_customEditorPathEdit);

    _browseEditorButton = new QPushButton("Browse...");
    _browseEditorButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton));
    _browseEditorButton->setEnabled(false);
    _customEditorLayout->addWidget(_browseEditorButton);

    _layout->addLayout(_customEditorLayout);

    // Update initial control states
    updateControlStates();
}

void TextEditorWidget::setupConnections() {
    connect(_systemEditorRadio, &QRadioButton::toggled, this, &TextEditorWidget::onEditorModeChanged);
    connect(_customEditorRadio, &QRadioButton::toggled, this, &TextEditorWidget::onEditorModeChanged);
    connect(_customEditorPathEdit, &QLineEdit::textChanged, this, &TextEditorWidget::onCustomEditorPathChanged);
    connect(_browseEditorButton, &QPushButton::clicked, this, &TextEditorWidget::onBrowseEditor);
}

Settings::TextEditorMode TextEditorWidget::getEditorMode() const {
    return _customEditorRadio->isChecked() ? Settings::TextEditorMode::CUSTOM : Settings::TextEditorMode::SYSTEM_DEFAULT;
}

void TextEditorWidget::setEditorMode(Settings::TextEditorMode mode) {
    if (mode == Settings::TextEditorMode::CUSTOM) {
        _customEditorRadio->setChecked(true);
    } else {
        _systemEditorRadio->setChecked(true);
    }
    updateControlStates();
}

QString TextEditorWidget::getCustomEditorPath() const {
    return _customEditorPathEdit->text();
}

void TextEditorWidget::setCustomEditorPath(const QString& path) {
    _customEditorPathEdit->setText(path);
}

void TextEditorWidget::updateControlStates() {
    bool customSelected = _customEditorRadio->isChecked();

    // Enable/disable custom editor controls
    _customEditorPathEdit->setEnabled(customSelected);
    _browseEditorButton->setEnabled(customSelected);
}

void TextEditorWidget::onEditorModeChanged() {
    updateControlStates();
    emit editorModeChanged();
    emit configurationChanged();
}

void TextEditorWidget::onCustomEditorPathChanged() {
    emit customEditorPathChanged();
    emit configurationChanged();
}

void TextEditorWidget::onBrowseEditor() {
    QString currentPath = _customEditorPathEdit->text();

    // Default to the current path or home directory
    QString startPath = currentPath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation) : QFileInfo(currentPath).absolutePath();

    QString editorPath = QFileDialog::getOpenFileName(this,
        "Select Text Editor Executable",
        startPath,
        "Executable Files (*.exe *.app *);;All Files (*)");

    if (!editorPath.isEmpty()) {
        _customEditorPathEdit->setText(editorPath);
        emit customEditorPathChanged();
        emit configurationChanged();
    }
}

} // namespace geck