#include "ui/dialogs/FillDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include "pattern/FillLibrary.h"
#include "ui/core/EditorWidget.h"

namespace geck {

namespace {
    constexpr int PATH_ROLE = Qt::UserRole + 1;
    constexpr int PREVIEW_DEBOUNCE_MS = 150; // recompute the ghost only after the seed settles

#ifdef GECK_SCRIPTING_ENABLED
    // A fill script's display label: a leading `-- name: Foo` comment if the file opens with one, else
    // the file's base name.
    QString scriptDisplayName(const QString& source, const QString& fallback) {
        const QStringList lines = source.left(512).split(QLatin1Char('\n'));
        for (const QString& raw : lines) {
            const QString line = raw.trimmed();
            if (!line.startsWith(QStringLiteral("--"))) {
                break; // a name must lead the file; stop at the first non-comment line
            }
            const QString body = line.mid(2).trimmed();
            if (body.startsWith(QStringLiteral("name:"), Qt::CaseInsensitive)) {
                const QString name = body.mid(5).trimmed();
                if (!name.isEmpty()) {
                    return name;
                }
            }
        }
        return fallback;
    }
#endif
} // namespace

FillDialog::FillDialog(EditorWidget& editor, QWidget* parent)
    : QDialog(parent)
    , _editor(&editor)
    , _area(editor.selectionFillArea()) {
    setWindowTitle(QStringLiteral("Fill Selection"));
    resize(620, 520);

    buildUi();
    populateBrowser();

    _previewTimer = new QTimer(this);
    _previewTimer->setSingleShot(true);
    connect(_previewTimer, &QTimer::timeout, this, &FillDialog::runPreview);

    // A random starting seed (the fill is reproducible from it; Lock keeps it across script changes).
    _seed->setValue(static_cast<int>(QRandomGenerator::global()->generate() & 0x7FFFFFFFu));
}

FillDialog::~FillDialog() {
    // Drop the ghost overlay when the dialog closes (Apply already cleared it; this covers Close/Esc).
    if (_editor) {
        _editor->clearFillPreview();
    }
}

void FillDialog::buildUi() {
    _browser = new QListWidget(this);
    _browser->setViewMode(QListView::ListMode);
    _browser->setMovement(QListView::Static);
    _browser->setWordWrap(true);
    _browser->setMinimumWidth(220);
    connect(_browser, &QListWidget::itemActivated, this, &FillDialog::onScriptActivated);
    connect(_browser, &QListWidget::itemClicked, this, &FillDialog::onScriptActivated);

    // --- Seed + options -------------------------------------------------------------------------
    _seed = new QSpinBox(this);
    _seed->setRange(0, 0x7FFFFFFF);
    _randomizeSeed = new QPushButton(QStringLiteral("Randomise"), this);
    _seedLock = new QCheckBox(QStringLiteral("Lock"), this);
    _seedLock->setToolTip(QStringLiteral("Keep this seed when switching scripts"));
    auto* seedRow = new QHBoxLayout; // NOSONAR: Qt owns this layout once it is added to the widget tree
    seedRow->addWidget(_seed, 1);
    seedRow->addWidget(_randomizeSeed);
    seedRow->addWidget(_seedLock);
    _livePreview = new QCheckBox(QStringLiteral("Live preview"), this);
    _livePreview->setChecked(true);
    auto* optionsForm = new QFormLayout; // NOSONAR: Qt owns this layout once it is added to the widget tree
    optionsForm->addRow(QStringLiteral("Seed"), seedRow);
    optionsForm->addRow(_livePreview);

    _summary = new QLabel(QStringLiteral("Select a fill script."), this);
    _summary->setWordWrap(true);

    auto* controls = new QVBoxLayout; // NOSONAR: Qt owns this layout once it is added to the widget tree
    controls->addLayout(optionsForm);
    controls->addWidget(_summary);
    controls->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    _applyButton = buttons->addButton(QStringLiteral("Apply"), QDialogButtonBox::AcceptRole);
    _applyButton->setDefault(true);

    auto* top = new QHBoxLayout; // NOSONAR: Qt owns this layout once it is added to the widget tree
    top->addWidget(_browser, 1);
    top->addLayout(controls, 1);

    auto* root = new QVBoxLayout(this);
    root->addLayout(top, 1);
    root->addWidget(buttons);

    connect(_seed, &QSpinBox::valueChanged, this, &FillDialog::schedulePreview);
    connect(_livePreview, &QCheckBox::toggled, this, &FillDialog::schedulePreview);
    connect(_randomizeSeed, &QPushButton::clicked, this, &FillDialog::onRandomizeSeed);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_applyButton, &QPushButton::clicked, this, &FillDialog::onApply);
}

void FillDialog::populateBrowser() {
    _browser->clear();
#ifdef GECK_SCRIPTING_ENABLED
    // Luau procedural fills (the only kind). The user library wins over a bundled example of the same
    // file name, so scan the user dir first and skip any later bundled file whose name was taken.
    QStringList seen;
    const QIcon scriptIcon(QStringLiteral(":/icons/filetypes/script.svg"));
    for (const QString& dirPath : { pattern::FillLibrary::rootDir(), pattern::FillLibrary::bundledDir() }) {
        const QDir dir(dirPath);
        for (const QString& file : dir.entryList({ QStringLiteral("*.luau") }, QDir::Files, QDir::Name)) {
            if (seen.contains(file)) {
                continue; // a user script of this name already won
            }
            seen.append(file);
            const QString path = dir.filePath(file);
            QFile handle(path);
            if (!handle.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QString source = QString::fromUtf8(handle.readAll());
            const QString label = scriptDisplayName(source, QFileInfo(file).completeBaseName());
            auto* item = new QListWidgetItem(scriptIcon, label, _browser);
            item->setData(PATH_ROLE, path);
            item->setToolTip(QStringLiteral("Procedural fill script (Luau)"));
        }
    }
#endif
}

void FillDialog::onScriptActivated(const QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const QString path = item->data(PATH_ROLE).toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return; // transient read error: keep the prior valid selection rather than half-changing
    }
    _scriptSource = file.readAll().toStdString();
    _selectedName = item->text();
    schedulePreview();
}

void FillDialog::onRandomizeSeed() {
    if (_seedLock->isChecked()) {
        return;
    }
    _seed->setValue(static_cast<int>(QRandomGenerator::global()->generate() & 0x7FFFFFFFu));
    // valueChanged on _seed triggers schedulePreview.
}

void FillDialog::schedulePreview() {
    if (_livePreview->isChecked()) {
        _previewTimer->start(PREVIEW_DEBOUNCE_MS);
    } else {
        if (_editor) {
            _editor->clearFillPreview();
        }
        _summary->setText(QStringLiteral("Live preview off — Apply to fill."));
    }
}

void FillDialog::runPreview() {
    if (!_editor) {
        return; // editor went away (e.g. its map closed) while the dialog was open
    }
    if (_scriptSource.empty()) {
        _editor->clearFillPreview();
        _summary->setText(QStringLiteral("Select a fill script."));
        return;
    }

#ifdef GECK_SCRIPTING_ENABLED
    if (const ScriptResult result
        = _editor->previewLuaFill(_area, _scriptSource, static_cast<uint32_t>(_seed->value()));
        !result.ok) {
        // previewLuaFill already posted the error to the status bar and cleared the ghost.
        _summary->setText(QStringLiteral("Script error: %1").arg(QString::fromStdString(result.error)));
        return;
    }
    const pattern::FillPlan& plan = _editor->fillPlan();
    QString summary = QStringLiteral("%1 tile(s), %2 object(s)")
                          .arg(plan.tiles.size())
                          .arg(plan.objects.size());
    if (plan.dropped > 0) {
        summary += QStringLiteral(" (%1 skipped: off-grid or over cap)").arg(plan.dropped);
    }
    _summary->setText(summary);
#endif
}

void FillDialog::onApply() {
    if (!_editor) {
        reject(); // nothing to apply to
        return;
    }
    if (_scriptSource.empty()) {
        return; // no script chosen yet: keep the dialog open
    }
    // Build/refresh the plan from the current seed (covers live-preview-off), then commit it.
    runPreview();
    const QString name
        = _selectedName.isEmpty() ? QStringLiteral("Fill") : QStringLiteral("Fill: %1").arg(_selectedName);
    _editor->applyFillPreview(name);
    accept();
}

} // namespace geck
