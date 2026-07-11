#include "ui/dialogs/PluginManagerDialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/plugin/PluginManager.h"
#include "ui/theme/ThemeManager.h"

namespace geck {

namespace {

    using State = plugin::PluginManager::State;

    QString stateLabel(State state) {
        switch (state) {
            case State::Enabled:
                return QObject::tr("Enabled");
            case State::Disabled:
                return QObject::tr("Disabled");
            case State::Faulted:
                return QObject::tr("Faulted");
            case State::Discovered:
            default:
                return QObject::tr("Not enabled");
        }
    }

    QString stateColor(State state) {
        switch (state) {
            case State::Enabled:
                return QString::fromLatin1(ui::theme::colors::SUCCESS);
            case State::Faulted:
                return QString::fromLatin1(ui::theme::colors::STATUS_ERROR);
            case State::Disabled:
            case State::Discovered:
            default:
                return QString::fromLatin1(ui::theme::colors::TEXT_SECONDARY);
        }
    }

} // namespace

PluginManagerDialog::PluginManagerDialog(plugin::PluginManager& manager, QWidget* parent)
    : QDialog(parent)
    , _manager(manager) {
    setupUI();
    refresh();
}

void PluginManagerDialog::setupUI() {
    setWindowTitle(tr("Plugin Manager"));
    resize(720, 440);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(ui::theme::spacing::NORMAL);

    auto* note = new QLabel(
        tr("Enabled plugins are read-only and run their entry script once. Tools, panels and "
           "map-write permissions arrive in a later release."),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: %1;").arg(ui::theme::colors::TEXT_MUTED));
    mainLayout->addWidget(note);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    _list = new QListWidget(splitter);
    _list->setMinimumWidth(200);
    connect(_list, &QListWidget::itemSelectionChanged, this, &PluginManagerDialog::onSelectionChanged);
    splitter->addWidget(_list);

    auto* detail = new QWidget(splitter);
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(ui::theme::spacing::NORMAL, 0, 0, 0);

    _title = new QLabel(detail);
    QFont titleFont = _title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    _title->setFont(titleFont);
    detailLayout->addWidget(_title);

    _meta = new QLabel(detail);
    _meta->setStyleSheet(QStringLiteral("color: %1;").arg(ui::theme::colors::TEXT_SECONDARY));
    detailLayout->addWidget(_meta);

    _description = new QLabel(detail);
    _description->setWordWrap(true);
    detailLayout->addWidget(_description);

    _status = new QLabel(detail);
    _status->setWordWrap(true);
    detailLayout->addWidget(_status);

    auto* consoleLabel = new QLabel(tr("Console"), detail);
    detailLayout->addWidget(consoleLabel);

    _console = new QPlainTextEdit(detail);
    _console->setReadOnly(true);
    _console->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    detailLayout->addWidget(_console, 1);

    splitter->addWidget(detail);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter, 1);

    auto* buttonRow = new QHBoxLayout();
    _enableButton = new QPushButton(tr("Enable"), this);
    _disableButton = new QPushButton(tr("Disable"), this);
    _rescanButton = new QPushButton(tr("Rescan"), this);
    connect(_enableButton, &QPushButton::clicked, this, &PluginManagerDialog::onEnableClicked);
    connect(_disableButton, &QPushButton::clicked, this, &PluginManagerDialog::onDisableClicked);
    connect(_rescanButton, &QPushButton::clicked, this, &PluginManagerDialog::onRescanClicked);
    buttonRow->addWidget(_enableButton);
    buttonRow->addWidget(_disableButton);
    buttonRow->addWidget(_rescanButton);
    buttonRow->addStretch(1);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonRow->addWidget(buttonBox);

    mainLayout->addLayout(buttonRow);
}

QString PluginManagerDialog::selectedId() const {
    const QListWidgetItem* item = _list->currentItem();
    return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
}

void PluginManagerDialog::refresh() {
    const QString keepId = selectedId();

    _list->blockSignals(true);
    _list->clear();
    const std::vector<plugin::PluginManager::Info> infos = _manager.list();
    for (const auto& info : infos) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 — %2").arg(info.name, stateLabel(info.state)), _list);
        item->setData(Qt::UserRole, info.id);
        if (info.state == State::Faulted) {
            item->setForeground(QColor(ui::theme::colors::STATUS_ERROR));
        }
    }

    // Restore the previous selection by id, else select the first row.
    int selectRow = _list->count() > 0 ? 0 : -1;
    if (!keepId.isEmpty()) {
        for (int row = 0; row < _list->count(); ++row) {
            if (_list->item(row)->data(Qt::UserRole).toString() == keepId) {
                selectRow = row;
                break;
            }
        }
    }
    if (selectRow >= 0) {
        _list->setCurrentRow(selectRow);
    }
    _list->blockSignals(false);

    onSelectionChanged();
}

void PluginManagerDialog::onSelectionChanged() {
    const QString id = selectedId();
    const std::vector<plugin::PluginManager::Info> infos = _manager.list();
    const plugin::PluginManager::Info* selected = nullptr;
    for (const auto& info : infos) {
        if (info.id == id) {
            selected = &info;
            break;
        }
    }

    if (selected == nullptr) {
        _title->clear();
        _meta->clear();
        _description->clear();
        _status->clear();
        _console->clear();
        _enableButton->setEnabled(false);
        _disableButton->setEnabled(false);
        return;
    }

    _title->setText(selected->name);
    _meta->setText(tr("%1 · v%2 · %3").arg(selected->id, selected->version, selected->source));
    _description->setText(selected->description);
    _description->setVisible(!selected->description.isEmpty());

    QString statusText = tr("Status: %1").arg(stateLabel(selected->state));
    if (!selected->error.isEmpty()) {
        statusText += QStringLiteral("\n%1").arg(selected->error);
    }
    _status->setText(statusText);
    _status->setStyleSheet(QStringLiteral("color: %1;").arg(stateColor(selected->state)));

    _console->setPlainText(selected->console);

    _enableButton->setEnabled(selected->state != State::Enabled);
    _disableButton->setEnabled(selected->state == State::Enabled);
}

void PluginManagerDialog::onEnableClicked() {
    const QString id = selectedId();
    if (!id.isEmpty()) {
        _manager.enable(id);
        refresh();
    }
}

void PluginManagerDialog::onDisableClicked() {
    const QString id = selectedId();
    if (!id.isEmpty()) {
        _manager.disable(id);
        refresh();
    }
}

void PluginManagerDialog::onRescanClicked() {
    _manager.discover();
    refresh();
}

} // namespace geck
