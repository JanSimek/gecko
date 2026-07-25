#include "TextEditorWidget.h"
#include "ui/IconHelper.h"
#include "ui/UIConstants.h"

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

    _helpLabel = new QLabel(
        "Choose how to open text files (txt, gam, lst, ini, etc.) from the file browser.");
    _helpLabel->setWordWrap(true);
    _helpLabel->setStyleSheet(ui::theme::styles::helpText());
    _layout->addWidget(_helpLabel);

    _systemEditorRadio = new QRadioButton("Use default system editor");
    _systemEditorRadio->setChecked(true);
    _layout->addWidget(_systemEditorRadio);

    _customEditorRadio = new QRadioButton("Use custom editor:");
    _customEditorRadio->setToolTip(QStringLiteral(
        R"(Set this to VS Code (the "code" binary) and install the BGforge MLS extension to get SSL )"
        R"(syntax highlighting, diagnostics, and compilation. "Edit Script Source" opens the script's )"
        R"(folder as a VS Code workspace so BGforge MLS can resolve its headers and compile.)"));
    _layout->addWidget(_customEditorRadio);

    _customEditorLayout = new QHBoxLayout();
    _customEditorLayout->setContentsMargins(ui::theme::spacing::MARGIN_INDENT, 0, 0, 0); // Indent under radio button

    _customEditorPathEdit = new QLineEdit();
    _customEditorPathEdit->setPlaceholderText("Path to editor executable (e.g. VS Code's \"code\")...");
    _customEditorPathEdit->setToolTip(
        "For editing and compiling Fallout SSL scripts, point this at VS Code and install the "
        "BGforge MLS extension (https://github.com/BGforgeNet/BGforge-MLS) — it bundles the compiler "
        "and resolves headers from the opened workspace folder.");
    _customEditorPathEdit->setEnabled(false);
    _customEditorLayout->addWidget(_customEditorPathEdit);

    _browseEditorButton = new QPushButton("Browse...");
    _browseEditorButton->setIcon(createIcon(":/icons/actions/open.svg"));
    _browseEditorButton->setEnabled(false);
    _customEditorLayout->addWidget(_browseEditorButton);

    _layout->addLayout(_customEditorLayout);

    auto* sslNote = new QLabel(
        "<b>Editing Fallout SSL scripts.</b> Gecko does not compile scripts itself — set the custom "
        "editor above to VS Code and install the "
        "<a href=\"https://github.com/BGforgeNet/BGforge-MLS\">BGforge MLS</a> extension, which adds SSL "
        "syntax highlighting, live diagnostics and a bundled compiler. \"Edit Script Source\" (in the "
        "Selection panel, Map Info and the Scripts panel) opens the script's <code>.ssl</code> with its "
        "source tree as the VS Code workspace, so MLS can resolve the <code>#include</code> headers and "
        "compile on save."
        "<br><br>"
        "<b>Deploying the compiled script.</b> The game and Gecko load <code>.int</code> bytecode only "
        "from a data folder's <code>scripts/</code> directory. By default BGforge MLS writes the "
        "<code>.int</code> next to the source, which is <i>not</i> that folder — so set MLS's output "
        "directory (<code>bgforge.falloutSSL.outputDirectory</code>) to your writable data folder's "
        "<code>scripts/</code>. The recompiled script then lands where it is loaded; reopen the map (or "
        "reload data) to pick up the new bytecode.");
    sslNote->setWordWrap(true);
    sslNote->setOpenExternalLinks(true);
    sslNote->setTextFormat(Qt::RichText);
    sslNote->setStyleSheet(ui::theme::styles::helpText());
    sslNote->setContentsMargins(0, ui::theme::spacing::NORMAL, 0, 0);
    _layout->addWidget(sslNote);

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

    _customEditorPathEdit->setEnabled(customSelected);
    _browseEditorButton->setEnabled(customSelected);
}

void TextEditorWidget::onEditorModeChanged() {
    updateControlStates();
    Q_EMIT editorModeChanged();
    Q_EMIT configurationChanged();
}

void TextEditorWidget::onCustomEditorPathChanged() {
    Q_EMIT customEditorPathChanged();
    Q_EMIT configurationChanged();
}

void TextEditorWidget::onBrowseEditor() {
    QString currentPath = _customEditorPathEdit->text();

    QString startPath = currentPath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation) : QFileInfo(currentPath).absolutePath();

    QString editorPath = QFileDialog::getOpenFileName(this,
        "Select Text Editor Executable",
        startPath,
        "Executable Files (*.exe *.app *);;All Files (*)");

    if (!editorPath.isEmpty()) {
        _customEditorPathEdit->setText(editorPath);
        Q_EMIT customEditorPathChanged();
        Q_EMIT configurationChanged();
    }
}

} // namespace geck