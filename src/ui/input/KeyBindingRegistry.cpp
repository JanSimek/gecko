#include "KeyBindingRegistry.h"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <span>

#include "ui/Settings.h"

namespace geck {

KeyBindingRegistry::KeyBindingRegistry(std::shared_ptr<Settings> settings, QObject* parent)
    : QObject(parent)
    , _settings(std::move(settings)) {
    if (!_settings) {
        return;
    }

    // Stored overrides are portable text; an entry for an id the table no longer has is dropped
    // rather than kept, so a renamed or retired action cannot resurrect a key nothing listens for.
    const QMap<QString, QString> stored = _settings->getKeyBindings();
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        if (spec(it.key()) == nullptr) {
            spdlog::debug("KeyBindingRegistry: dropping override for unknown action '{}'", it.key().toStdString());
            continue;
        }
        _overrides.insert(it.key(), QKeySequence::fromString(it.value(), QKeySequence::PortableText));
    }
}

const ActionSpec* KeyBindingRegistry::spec(const QString& id) {
    // Indexed off the span's data pointer rather than by taking the address of a range-for
    // reference: the table it views has static storage, but the span itself is a temporary, and
    // returning &element out of a loop over one reads as a dangling return to cppcheck.
    const std::span<const ActionSpec> specs = actionSpecs();
    for (std::size_t index = 0; index < specs.size(); ++index) {
        if (id == QLatin1StringView(specs[index].id)) {
            return specs.data() + index;
        }
    }
    return nullptr;
}

QKeySequence KeyBindingRegistry::defaultShortcut(const QString& id) const {
    const ActionSpec* found = spec(id);
    if (!found) {
        return {};
    }
    if (found->standardKey != QKeySequence::UnknownKey) {
        return QKeySequence(found->standardKey);
    }
    return QKeySequence::fromString(QLatin1StringView(found->defaultKeys), QKeySequence::PortableText);
}

QKeySequence KeyBindingRegistry::shortcut(const QString& id) const {
    const auto stored = _overrides.constFind(id);
    return stored != _overrides.constEnd() ? stored.value() : defaultShortcut(id);
}

bool KeyBindingRegistry::isCustomized(const QString& id) const {
    return _overrides.contains(id);
}

QString KeyBindingRegistry::conflictingActionId(const QString& id, const QKeySequence& seq) const {
    if (seq.isEmpty()) {
        return {}; // any number of actions may be unbound
    }

    for (const ActionSpec& candidate : actionSpecs()) {
        const QString candidateId = QString::fromLatin1(candidate.id);
        if (candidateId == id) {
            continue;
        }
        if (shortcut(candidateId) == seq) {
            return candidateId;
        }
    }

    return {};
}

void KeyBindingRegistry::setShortcut(const QString& id, const QKeySequence& seq) {
    if (spec(id) == nullptr) {
        spdlog::warn("KeyBindingRegistry: ignoring rebind of unknown action '{}'", id.toStdString());
        return;
    }
    const bool isDefault = (seq == defaultShortcut(id));
    if (shortcut(id) == seq && _overrides.contains(id) != isDefault) {
        return; // already exactly this, and already stored the way it should be
    }

    if (isDefault) {
        // Back on the default: forget the override so a later release changing that default is
        // still free to move this user's key.
        _overrides.remove(id);
    } else {
        _overrides.insert(id, seq);
    }

    applyToSinks(id, seq);
    Q_EMIT bindingChanged(id, seq);
}

void KeyBindingRegistry::resetToDefault(const QString& id) {
    setShortcut(id, defaultShortcut(id));
}

void KeyBindingRegistry::resetAllToDefaults() {
    const QList<QString> customized = _overrides.keys();
    for (const QString& id : customized) {
        resetToDefault(id);
    }
}

void KeyBindingRegistry::save() {
    if (!_settings) {
        return;
    }

    QMap<QString, QString> stored;
    for (auto it = _overrides.constBegin(); it != _overrides.constEnd(); ++it) {
        // An unbound action persists as an empty string, which is distinct from being absent.
        stored.insert(it.key(), it.value().toString(QKeySequence::PortableText));
    }
    _settings->setKeyBindings(stored);
}

void KeyBindingRegistry::bind(const QString& id, QAction* action) {
    if (!action || spec(id) == nullptr) {
        return;
    }
    // Prune here as well as on rebind: canvas shortcuts are re-bound on every map load, so a list
    // whose dead entries were only cleared by a rebind would grow with nulls across a session.
    QList<QPointer<QAction>>& sinks = _actionSinks[id];
    sinks.removeIf([](const QPointer<QAction>& sink) { return sink.isNull(); });
    sinks.append(QPointer<QAction>(action));
    action->setShortcut(shortcut(id));
}

void KeyBindingRegistry::bind(const QString& id, QShortcut* shortcutSink) {
    if (!shortcutSink || spec(id) == nullptr) {
        return;
    }
    QList<QPointer<QShortcut>>& sinks = _shortcutSinks[id];
    sinks.removeIf([](const QPointer<QShortcut>& sink) { return sink.isNull(); });
    sinks.append(QPointer<QShortcut>(shortcutSink));
    shortcutSink->setKey(shortcut(id));
}

void KeyBindingRegistry::applyToSinks(const QString& id, const QKeySequence& seq) {
    // Sinks are held as QPointers because a canvas shortcut dies with the map view it hangs off;
    // clear the dead ones as we go rather than growing the list across map loads.
    QList<QPointer<QAction>>& actions = _actionSinks[id];
    actions.removeIf([&seq](const QPointer<QAction>& action) {
        if (!action) {
            return true;
        }
        action->setShortcut(seq);
        return false;
    });

    QList<QPointer<QShortcut>>& shortcuts = _shortcutSinks[id];
    shortcuts.removeIf([&seq](const QPointer<QShortcut>& sink) {
        if (!sink) {
            return true;
        }
        sink->setKey(seq);
        return false;
    });
}

} // namespace geck
