#include "KeybindingsWidget.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui/input/ActionSpec.h"
#include "ui/input/KeyBindingRegistry.h"
#include "ui/theme/ThemeManager.h"

namespace geck {

namespace {

    constexpr int COLUMN_ACTION = 0;
    constexpr int COLUMN_SHORTCUT = 1;
    constexpr int COLUMN_DEFAULT = 2;

    // The action id a row stands for; category rows carry none.
    constexpr int ACTION_ID_ROLE = Qt::UserRole + 1;

    QString actionIdOf(const QTreeWidgetItem* item) {
        return item ? item->data(COLUMN_ACTION, ACTION_ID_ROLE).toString() : QString();
    }

    QString displayKeys(const QKeySequence& keys) {
        // Native text ("⌘S" on macOS, "Ctrl+S" elsewhere): this column is read, never parsed.
        return keys.isEmpty() ? QObject::tr("Unbound") : keys.toString(QKeySequence::NativeText);
    }

} // namespace

KeybindingsWidget::KeybindingsWidget(KeyBindingRegistry* registry, QWidget* parent)
    : QGroupBox(tr("Keyboard Shortcuts"), parent)
    , _registry(registry) {
    setupUI();
    reload();
}

void KeybindingsWidget::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(ui::theme::spacing::LOOSE, ui::theme::spacing::LOOSE,
        ui::theme::spacing::LOOSE, ui::theme::spacing::LOOSE);
    layout->setSpacing(ui::theme::spacing::NORMAL);

    _filterEdit = new QLineEdit(this);
    _filterEdit->setPlaceholderText(tr("Filter by action, category or key..."));
    _filterEdit->setClearButtonEnabled(true);
    layout->addWidget(_filterEdit);

    _tree = new QTreeWidget(this);
    _tree->setColumnCount(3);
    _tree->setHeaderLabels({ tr("Action"), tr("Shortcut"), tr("Default") });
    _tree->setRootIsDecorated(false);
    _tree->setIndentation(ui::theme::spacing::LOOSE);
    _tree->setAllColumnsShowFocus(true);
    _tree->setMinimumHeight(ui::constants::LIST_MIN_HEIGHT);
    _tree->header()->setSectionResizeMode(COLUMN_ACTION, QHeaderView::Stretch);
    _tree->header()->setSectionResizeMode(COLUMN_SHORTCUT, QHeaderView::ResizeToContents);
    _tree->header()->setSectionResizeMode(COLUMN_DEFAULT, QHeaderView::ResizeToContents);
    layout->addWidget(_tree, /*stretch=*/1);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(ui::theme::spacing::NORMAL);
    _resetButton = new QPushButton(tr("Reset"), this);
    _resetButton->setToolTip(tr("Put the selected shortcut back to its default"));
    _resetButton->setEnabled(false);
    _resetAllButton = new QPushButton(tr("Reset All"), this);
    _resetAllButton->setToolTip(tr("Put every shortcut back to its default"));
    buttons->addWidget(_resetButton);
    buttons->addWidget(_resetAllButton);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(_filterEdit, &QLineEdit::textChanged, this, &KeybindingsWidget::onFilterChanged);
    connect(_tree, &QTreeWidget::itemSelectionChanged, this, &KeybindingsWidget::onSelectionChanged);
    connect(_resetButton, &QPushButton::clicked, this, &KeybindingsWidget::onResetSelected);
    connect(_resetAllButton, &QPushButton::clicked, this, &KeybindingsWidget::onResetAll);

    // Double-clicking the Shortcut cell opens a one-chord capture in place.
    connect(_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (column != COLUMN_SHORTCUT || actionIdOf(item).isEmpty()) {
            return;
        }

        // QKeySequenceEdit captures the chord itself (modifiers included) and emits
        // editingFinished on focus-out or after its own short idle timeout. One chord only:
        // multi-chord sequences are not wanted for editor commands.
        auto* editor = new QKeySequenceEdit(_tree);
        editor->setMaximumSequenceLength(1);
        editor->setKeySequence(pendingShortcut(actionIdOf(item)));
        _tree->setItemWidget(item, COLUMN_SHORTCUT, editor);
        editor->setFocus();

        connect(editor, &QKeySequenceEdit::editingFinished, this, [this, item, editor]() {
            const QKeySequence keys = editor->keySequence();
            _tree->removeItemWidget(item, COLUMN_SHORTCUT);
            editor->deleteLater();
            onEditFinished(item, keys);
        });
    });
}

void KeybindingsWidget::reload() {
    _pending.clear();
    populate();
    Q_EMIT statusChanged(QString(), QStringLiteral("normal"));
}

void KeybindingsWidget::populate() {
    _tree->clear();
    if (!_registry) {
        return;
    }

    QHash<QString, QTreeWidgetItem*> categories;
    for (const ActionSpec& spec : actionSpecs()) {
        const QString category = QString::fromLatin1(spec.category);
        QTreeWidgetItem*& group = categories[category];
        if (group == nullptr) {
            group = new QTreeWidgetItem(_tree, { category });
            group->setFirstColumnSpanned(true);
            group->setFlags(Qt::ItemIsEnabled); // a heading, not a target
            QFont bold = group->font(COLUMN_ACTION);
            bold.setBold(true);
            group->setFont(COLUMN_ACTION, bold);
            group->setExpanded(true);
        }

        auto* row = new QTreeWidgetItem(group, { QString::fromLatin1(spec.label) });
        row->setData(COLUMN_ACTION, ACTION_ID_ROLE, QString::fromLatin1(spec.id));
        row->setToolTip(COLUMN_ACTION, tr("%1 — %2 scope").arg(QString::fromLatin1(spec.id), spec.scope == ActionScope::Canvas ? tr("map view") : tr("application")));
        refreshRow(row);
    }
}

void KeybindingsWidget::refreshRow(QTreeWidgetItem* item) {
    const QString id = actionIdOf(item);
    if (id.isEmpty() || !_registry) {
        return;
    }

    const QKeySequence keys = pendingShortcut(id);
    item->setText(COLUMN_SHORTCUT, displayKeys(keys));
    item->setText(COLUMN_DEFAULT, displayKeys(_registry->defaultShortcut(id)));

    // Bold marks a row that no longer sits on its default, whether the user moved it in this
    // sitting or in an earlier one.
    const bool customized = (keys != _registry->defaultShortcut(id));
    QFont font = item->font(COLUMN_SHORTCUT);
    font.setBold(customized);
    item->setFont(COLUMN_SHORTCUT, font);

    const QString conflict = pendingConflict(id, keys);
    item->setForeground(COLUMN_SHORTCUT,
        conflict.isEmpty() ? QBrush() : QBrush(QColor(ui::theme::colors::STATUS_ERROR)));
}

QKeySequence KeybindingsWidget::pendingShortcut(const QString& id) const {
    const auto pending = _pending.constFind(id);
    if (pending != _pending.constEnd()) {
        return pending.value();
    }
    return _registry ? _registry->shortcut(id) : QKeySequence();
}

QString KeybindingsWidget::pendingConflict(const QString& id, const QKeySequence& keys) const {
    if (keys.isEmpty()) {
        return {}; // any number of actions may be unbound
    }

    for (const ActionSpec& candidate : actionSpecs()) {
        const QString candidateId = QString::fromLatin1(candidate.id);
        if (candidateId == id) {
            continue;
        }
        if (pendingShortcut(candidateId) == keys) {
            return candidateId;
        }
    }

    return {};
}

QTreeWidgetItem* KeybindingsWidget::itemForAction(const QString& id) const {
    for (int group = 0; group < _tree->topLevelItemCount(); ++group) {
        QTreeWidgetItem* category = _tree->topLevelItem(group);
        for (int row = 0; row < category->childCount(); ++row) {
            if (actionIdOf(category->child(row)) == id) {
                return category->child(row);
            }
        }
    }
    return nullptr;
}

void KeybindingsWidget::onEditFinished(QTreeWidgetItem* item, const QKeySequence& keys) {
    const QString id = actionIdOf(item);
    if (id.isEmpty() || !_registry) {
        return;
    }
    if (keys == pendingShortcut(id)) {
        return; // nothing typed, or the same chord again
    }

    const QString conflict = pendingConflict(id, keys);
    if (!conflict.isEmpty()) {
        // Reported rather than refused: the user can still see what they typed, and clearing the
        // other action's key resolves it. Applying a conflicting pair would make Qt fire neither.
        const ActionSpec* other = KeyBindingRegistry::spec(conflict);
        Q_EMIT statusChanged(tr("%1 is already assigned to %2 — clear that one first")
                                 .arg(displayKeys(keys), other ? QString::fromLatin1(other->label) : conflict),
            QStringLiteral("error"));
    } else {
        Q_EMIT statusChanged(QString(), QStringLiteral("normal"));
    }

    _pending.insert(id, keys);
    refreshRow(item);
    // The other row's colour changes too: it is now half of a conflict, or no longer part of one.
    if (QTreeWidgetItem* otherItem = itemForAction(conflict)) {
        refreshRow(otherItem);
    }
    onSelectionChanged();
    Q_EMIT changed();
}

void KeybindingsWidget::onFilterChanged(const QString& text) {
    const QString needle = text.trimmed();

    for (int group = 0; group < _tree->topLevelItemCount(); ++group) {
        QTreeWidgetItem* category = _tree->topLevelItem(group);
        int visibleRows = 0;

        for (int row = 0; row < category->childCount(); ++row) {
            QTreeWidgetItem* item = category->child(row);
            const bool matches = needle.isEmpty()
                || item->text(COLUMN_ACTION).contains(needle, Qt::CaseInsensitive)
                || item->text(COLUMN_SHORTCUT).contains(needle, Qt::CaseInsensitive)
                || category->text(COLUMN_ACTION).contains(needle, Qt::CaseInsensitive);
            item->setHidden(!matches);
            visibleRows += matches ? 1 : 0;
        }

        category->setHidden(visibleRows == 0);
    }
}

void KeybindingsWidget::onSelectionChanged() {
    const QList<QTreeWidgetItem*> selected = _tree->selectedItems();
    const QString id = selected.isEmpty() ? QString() : actionIdOf(selected.first());
    _resetButton->setEnabled(!id.isEmpty() && _registry
        && pendingShortcut(id) != _registry->defaultShortcut(id));
}

void KeybindingsWidget::onResetSelected() {
    const QList<QTreeWidgetItem*> selected = _tree->selectedItems();
    if (selected.isEmpty() || !_registry) {
        return;
    }

    QTreeWidgetItem* item = selected.first();
    const QString id = actionIdOf(item);
    if (id.isEmpty()) {
        return;
    }

    _pending.insert(id, _registry->defaultShortcut(id));
    refreshRow(item);
    onSelectionChanged();
    Q_EMIT statusChanged(QString(), QStringLiteral("normal"));
    Q_EMIT changed();
}

void KeybindingsWidget::onResetAll() {
    if (!_registry) {
        return;
    }

    for (const ActionSpec& spec : actionSpecs()) {
        const QString id = QString::fromLatin1(spec.id);
        _pending.insert(id, _registry->defaultShortcut(id));
    }

    for (int group = 0; group < _tree->topLevelItemCount(); ++group) {
        QTreeWidgetItem* category = _tree->topLevelItem(group);
        for (int row = 0; row < category->childCount(); ++row) {
            refreshRow(category->child(row));
        }
    }

    onSelectionChanged();
    Q_EMIT statusChanged(QString(), QStringLiteral("normal"));
    Q_EMIT changed();
}

bool KeybindingsWidget::hasPendingChanges() const {
    if (!_registry) {
        return false;
    }
    for (auto it = _pending.constBegin(); it != _pending.constEnd(); ++it) {
        if (it.value() != _registry->shortcut(it.key())) {
            return true;
        }
    }
    return false;
}

void KeybindingsWidget::applyChanges() {
    if (!_registry) {
        return;
    }

    // A conflicting pair would leave Qt firing neither shortcut, so those edits are dropped here
    // rather than written through; the status line already said so when they were typed.
    for (auto it = _pending.constBegin(); it != _pending.constEnd(); ++it) {
        if (pendingConflict(it.key(), it.value()).isEmpty()) {
            _registry->setShortcut(it.key(), it.value());
        }
    }

    _registry->save();
    _pending.clear();
    populate();
}

} // namespace geck
