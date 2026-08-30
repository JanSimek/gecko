#pragma once

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QShortcut>
#include <QString>

#include <memory>

#include "ActionSpec.h"

namespace geck {

class Settings;

/**
 * @brief The one place that decides which keys run which commands.
 *
 * Holds the shipped defaults (ActionSpec) plus the user's overrides, answers "what key is this
 * action on" for the menus, the toolbar and the canvas, and re-keys every registered sink the
 * moment a binding changes — so a rebind takes effect without a restart and without walking the
 * menu bar.
 *
 * Owned by MainWindow and injected into SettingsDialog.
 */
class KeyBindingRegistry : public QObject {
    Q_OBJECT

public:
    explicit KeyBindingRegistry(std::shared_ptr<Settings> settings, QObject* parent = nullptr);

    /// The action's current key: the user's override where there is one, else the shipped default.
    /// An empty sequence means unbound.
    QKeySequence shortcut(const QString& id) const;
    /// The shipped default, resolved for this platform (see ActionSpec::standardKey).
    QKeySequence defaultShortcut(const QString& id) const;
    /// True when the user has moved this action off its default (including unbinding it).
    bool isCustomized(const QString& id) const;

    /// The action `seq` is already assigned to, or an empty string when nothing is. An action never
    /// conflicts with itself, and an empty sequence never conflicts — any number of actions may be
    /// unbound. Canvas actions are checked against Application ones too: a window-scoped shortcut
    /// fires while the canvas has focus, so the two scopes share one namespace.
    QString conflictingActionId(const QString& id, const QKeySequence& seq) const;

    /// Rebind (an empty sequence unbinds). Setting an action back to its default drops the
    /// override, so a later release changing that default still reaches this user.
    void setShortcut(const QString& id, const QKeySequence& seq);
    void resetToDefault(const QString& id);
    void resetAllToDefaults();
    /// Write the overrides through to Settings (the caller saves).
    void save();

    /// Register a sink: it takes the action's current key now, and every later one automatically.
    void bind(const QString& id, QAction* action);
    void bind(const QString& id, QShortcut* shortcut);

    /// The spec row for an id, or nullptr when the id is unknown.
    static const ActionSpec* spec(const QString& id);

signals:
    void bindingChanged(const QString& id, const QKeySequence& seq);

private:
    void applyToSinks(const QString& id, const QKeySequence& seq);

    std::shared_ptr<Settings> _settings;
    /// Overrides only. A present-but-empty sequence is a deliberate unbind, which is not the same
    /// as an absent entry (= "use the default").
    QMap<QString, QKeySequence> _overrides;
    QHash<QString, QList<QPointer<QAction>>> _actionSinks;
    QHash<QString, QList<QPointer<QShortcut>>> _shortcutSinks;
};

} // namespace geck
