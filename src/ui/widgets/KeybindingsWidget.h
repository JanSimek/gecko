#pragma once

#include <QGroupBox>
#include <QKeySequence>
#include <QMap>
#include <QString>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace geck {

class KeyBindingRegistry;

/**
 * @brief The Preferences page for rebinding keyboard shortcuts.
 *
 * Edits an in-memory copy of the bindings and commits it on Apply/OK, like the other Preferences
 * sections, so Cancel really does cancel. Conflicts are reported live while typing a chord: the
 * row turns red and the status line names the action already holding that key, with a Reassign
 * button that unbinds the other one.
 */
class KeybindingsWidget : public QGroupBox {
    Q_OBJECT

public:
    explicit KeybindingsWidget(KeyBindingRegistry* registry, QWidget* parent = nullptr);

    /// Re-read the registry, discarding anything not yet applied.
    void reload();
    /// Push the pending edits into the registry (the dialog saves Settings afterwards).
    void applyChanges();
    /// True when there is anything to apply.
    bool hasPendingChanges() const;

signals:
    void changed();
    void statusChanged(const QString& message, const QString& styleClass);

private slots:
    void onFilterChanged(const QString& text);
    void onSelectionChanged();
    void onResetSelected();
    void onResetAll();
    void onEditFinished(QTreeWidgetItem* item, const QKeySequence& keys);

private:
    void setupUI();
    void populate();
    void refreshRow(QTreeWidgetItem* item);
    /// Repaint every row. One edit can change two rows (both sides of a conflict), so this is the
    /// honest granularity for a table this size.
    void refreshAllRows();
    /// The pending key for an action: the edit in progress if there is one, else the registry's.
    QKeySequence pendingShortcut(const QString& id) const;
    /// The action `keys` is pending on, or an empty string. Mirrors the registry's rule, but over
    /// the uncommitted edits — otherwise a swap made in one sitting would look conflict-free.
    QString pendingConflict(const QString& id, const QKeySequence& keys) const;

    KeyBindingRegistry* _registry = nullptr;
    QLineEdit* _filterEdit = nullptr;
    QTreeWidget* _tree = nullptr;
    QPushButton* _resetButton = nullptr;
    QPushButton* _resetAllButton = nullptr;
    /// Edits not yet applied: action id -> key (an empty sequence is a deliberate unbind).
    QMap<QString, QKeySequence> _pending;
};

} // namespace geck
