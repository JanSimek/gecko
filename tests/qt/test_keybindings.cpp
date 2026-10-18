#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QShortcut>
#include <QLineEdit>
#include <QAbstractButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QWidget>

#include <memory>
#include <set>
#include <string>

#include "ui/Settings.h"
#include "ui/input/ActionSpec.h"
#include "ui/input/KeyBindingRegistry.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/widgets/KeybindingsWidget.h"

#include "support/QtFind.h"

using namespace geck;

// The table is what every menu, toolbar and canvas binding reads, and what settings.json points
// at, so its invariants are worth asserting directly: a duplicate id would make one row
// unreachable, and two actions sharing a key would make Qt fire neither.
TEST_CASE("The shipped keybinding table is internally consistent", "[qt][keybindings]") {
    std::set<std::string, std::less<>> ids;
    std::set<std::string, std::less<>> keys;

    for (const ActionSpec& spec : actionSpecs()) {
        REQUIRE(spec.id != nullptr);
        REQUIRE(spec.label != nullptr);
        REQUIRE(spec.category != nullptr);

        INFO("action id: " << spec.id);
        CHECK(ids.insert(spec.id).second); // ids are unique

        // Exactly one of the two ways of naming a default is used.
        const bool hasText = (spec.defaultKeys != nullptr && *spec.defaultKeys != '\0');
        const bool hasStandardKey = (spec.standardKey != QKeySequence::UnknownKey);
        CHECK_FALSE((hasText && hasStandardKey));

        if (hasText) {
            // A typo'd default would silently parse to an empty sequence and leave the action
            // unbound rather than failing anywhere visible.
            CHECK_FALSE(QKeySequence::fromString(QString::fromLatin1(spec.defaultKeys),
                QKeySequence::PortableText)
                    .isEmpty());
        }
    }

    // Defaults do not collide — across both scopes, since an Application shortcut fires while the
    // canvas has focus too. A platform where a standard key resolves to nothing (Quit on Windows)
    // legitimately yields several empty sequences, so only bound ones are compared.
    KeyBindingRegistry registry(std::make_shared<Settings>());
    for (const ActionSpec& spec : actionSpecs()) {
        const QKeySequence keySequence = registry.defaultShortcut(QString::fromLatin1(spec.id));
        if (keySequence.isEmpty()) {
            continue;
        }
        INFO("action id: " << spec.id << ", key: " << keySequence.toString().toStdString());
        CHECK(keys.insert(keySequence.toString().toStdString()).second);
    }
}

TEST_CASE("An override is stored, reloaded and dropped again on reset", "[qt][keybindings]") {
    auto settings = std::make_shared<Settings>();
    const QKeySequence rebound("Ctrl+Alt+K");

    {
        KeyBindingRegistry registry(settings);
        REQUIRE(registry.shortcut(actions::PANEL_SELECTION) == QKeySequence("Alt+2"));
        REQUIRE_FALSE(registry.isCustomized(actions::PANEL_SELECTION));

        registry.setShortcut(actions::PANEL_SELECTION, rebound);
        CHECK(registry.shortcut(actions::PANEL_SELECTION) == rebound);
        CHECK(registry.isCustomized(actions::PANEL_SELECTION));
        registry.save();
    }

    // Only the changed action is written: everything still on its default stays absent, so a
    // later release is free to move those defaults.
    const QMap<QString, QString> stored = settings->getKeyBindings();
    CHECK(stored.size() == 1);
    CHECK(stored.value(actions::PANEL_SELECTION) == rebound.toString(QKeySequence::PortableText));

    {
        KeyBindingRegistry reloaded(settings);
        CHECK(reloaded.shortcut(actions::PANEL_SELECTION) == rebound);
        CHECK(reloaded.shortcut(actions::PANEL_MAP_INFO) == QKeySequence("Alt+1")); // untouched

        reloaded.resetToDefault(actions::PANEL_SELECTION);
        CHECK_FALSE(reloaded.isCustomized(actions::PANEL_SELECTION));
        reloaded.save();
    }

    CHECK(settings->getKeyBindings().isEmpty());
}

// Unbinding is a real state, distinct from "never changed": it has to survive a round trip, or a
// reload would hand the key back.
TEST_CASE("An unbound action persists as unbound", "[qt][keybindings]") {
    auto settings = std::make_shared<Settings>();
    {
        KeyBindingRegistry registry(settings);
        registry.setShortcut(actions::TOOL_ROTATE, QKeySequence());
        registry.save();
    }

    CHECK(settings->getKeyBindings().value(actions::TOOL_ROTATE) == QString(""));

    KeyBindingRegistry reloaded(settings);
    CHECK(reloaded.shortcut(actions::TOOL_ROTATE).isEmpty());
    CHECK(reloaded.isCustomized(actions::TOOL_ROTATE));
}

TEST_CASE("Conflicts are found across scopes and ignore unbound actions", "[qt][keybindings]") {
    KeyBindingRegistry registry(std::make_shared<Settings>());

    // Alt+2 belongs to the Selection panel.
    CHECK(registry.conflictingActionId(actions::PANEL_MAP_INFO, QKeySequence("Alt+2"))
        == QString(actions::PANEL_SELECTION));
    // An action never conflicts with itself.
    CHECK(registry.conflictingActionId(actions::PANEL_SELECTION, QKeySequence("Alt+2")).isEmpty());
    // A free key is free.
    CHECK(registry.conflictingActionId(actions::PANEL_MAP_INFO, QKeySequence("Ctrl+Alt+J")).isEmpty());
    // Any number of actions may be unbound.
    CHECK(registry.conflictingActionId(actions::PANEL_MAP_INFO, QKeySequence()).isEmpty());

    // The two scopes are one namespace: a window-scoped shortcut fires while the canvas has focus,
    // so a canvas action must not be allowed to shadow one.
    CHECK(registry.conflictingActionId(actions::TOOL_SELECT, QKeySequence("Ctrl+A"))
        == QString(actions::SELECT_ALL));
}

// The point of the sink registration: a rebind reaches the live UI without a restart and without
// re-walking the menu bar.
TEST_CASE("Bound actions and shortcuts follow a rebind", "[qt][keybindings]") {
    KeyBindingRegistry registry(std::make_shared<Settings>());

    QWidget host;
    QAction action(&host);
    auto* shortcut = new QShortcut(&host);

    registry.bind(actions::PANEL_SELECTION, &action);
    registry.bind(actions::TOOL_ROTATE, shortcut);

    // bind() applies the current key straight away.
    CHECK(action.shortcut() == QKeySequence("Alt+2"));
    CHECK(shortcut->key() == QKeySequence("R"));

    registry.setShortcut(actions::PANEL_SELECTION, QKeySequence("Ctrl+Alt+2"));
    registry.setShortcut(actions::TOOL_ROTATE, QKeySequence("Ctrl+Alt+R"));

    CHECK(action.shortcut() == QKeySequence("Ctrl+Alt+2"));
    CHECK(shortcut->key() == QKeySequence("Ctrl+Alt+R"));

    registry.resetAllToDefaults();
    CHECK(action.shortcut() == QKeySequence("Alt+2"));
    CHECK(shortcut->key() == QKeySequence("R"));
}

// A sink dies with the map view it hangs off; the registry must not touch it afterwards.
TEST_CASE("A destroyed sink is dropped instead of dangling", "[qt][keybindings]") {
    KeyBindingRegistry registry(std::make_shared<Settings>());

    {
        QWidget host;
        registry.bind(actions::TOOL_ROTATE, new QShortcut(&host));
    }

    registry.setShortcut(actions::TOOL_ROTATE, QKeySequence("Ctrl+Alt+R")); // must not crash
    CHECK(registry.shortcut(actions::TOOL_ROTATE) == QKeySequence("Ctrl+Alt+R"));
}

// Canvas shortcuts are re-bound on every map load, so binding the same id again has to drop the
// sinks that died with the previous map rather than piling nulls up for the session's lifetime.
TEST_CASE("Re-binding an action drops sinks that have been destroyed", "[qt][keybindings]") {
    KeyBindingRegistry registry(std::make_shared<Settings>());

    for (int round = 0; round < 3; ++round) {
        QWidget host;
        registry.bind(actions::TOOL_ROTATE, new QShortcut(&host));
        registry.setShortcut(actions::TOOL_ROTATE, QKeySequence(round % 2 == 0 ? "Ctrl+Alt+R" : "R"));
    }

    // The survivor of the last round is gone too; rebinding must still be safe.
    QWidget host;
    auto* live = new QShortcut(&host);
    registry.bind(actions::TOOL_ROTATE, live);
    registry.setShortcut(actions::TOOL_ROTATE, QKeySequence("Ctrl+Alt+T"));
    CHECK(live->key() == QKeySequence("Ctrl+Alt+T"));
}

// A settings file naming an action the table no longer has (renamed, retired) must not resurrect
// a key that nothing listens for.
TEST_CASE("An override for an unknown action is discarded", "[qt][keybindings]") {
    auto settings = std::make_shared<Settings>();
    settings->setKeyBindings({ { "panel.thatNeverExisted", "Ctrl+Alt+Z" } });

    KeyBindingRegistry registry(settings);
    registry.save();

    CHECK(settings->getKeyBindings().isEmpty());
    CHECK(registry.shortcut("panel.thatNeverExisted").isEmpty());
}

// The Preferences page: one row per table entry, a filter over them, and edits that only reach the
// registry on apply — so Cancel really cancels.
TEST_CASE("The keybindings page lists the table and commits only on apply", "[qt][keybindings]") {
    auto settings = std::make_shared<Settings>();
    KeyBindingRegistry registry(settings);
    KeybindingsWidget widget(&registry);

    auto* tree = widget.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);

    CHECK(test::visibleLeafCount(*tree) == static_cast<int>(actionSpecs().size())); // nothing filtered yet
    CHECK(tree->topLevelItemCount() > 1); // grouped by category, not one flat list

    // Nothing edited yet.
    CHECK_FALSE(widget.hasPendingChanges());

    // The filter hides non-matching rows and the categories left empty by them.
    auto* filter = widget.findChild<QLineEdit*>();
    REQUIRE(filter != nullptr);
    filter->setText("Selection Panel");
    QApplication::processEvents();

    CHECK(test::visibleLeafCount(*tree) == 1);

    // Applying with nothing pending leaves the bindings — and the settings file — untouched.
    widget.applyChanges();
    CHECK(registry.shortcut(actions::PANEL_SELECTION) == QKeySequence("Alt+2"));
    CHECK(settings->getKeyBindings().isEmpty());
}

// Reset All is the page's escape hatch, so it has to clear an override made in an earlier sitting,
// not just the edits typed in this one.
TEST_CASE("Reset All on the keybindings page clears a stored override", "[qt][keybindings]") {
    auto settings = std::make_shared<Settings>();
    KeyBindingRegistry registry(settings);
    registry.setShortcut(actions::PANEL_SELECTION, QKeySequence("Ctrl+Alt+K"));
    registry.save();

    KeybindingsWidget widget(&registry);

    // By text rather than by order, so adding a button next to it doesn't silently move this test.
    QAbstractButton* resetAllButton = test::findButton(widget, "Reset All");
    REQUIRE(resetAllButton != nullptr);

    resetAllButton->click();
    CHECK(widget.hasPendingChanges());

    widget.applyChanges();
    CHECK(registry.shortcut(actions::PANEL_SELECTION) == QKeySequence("Alt+2"));
    CHECK_FALSE(registry.isCustomized(actions::PANEL_SELECTION));
    CHECK(settings->getKeyBindings().isEmpty());
}

// The tab is optional: a dialog built without a registry (the first-run one, and a bare-constructed
// one in a test) must still build, just without the page.
TEST_CASE("The Preferences dialog gains a shortcuts tab only with a registry", "[qt][keybindings]") {
    auto settings = std::make_shared<Settings>();
    KeyBindingRegistry registry(settings);

    const auto tabTitles = [](const SettingsDialog& dialog) {
        QStringList titles;
        for (const QTabWidget* tabs : dialog.findChildren<QTabWidget*>()) {
            for (int index = 0; index < tabs->count(); ++index) {
                titles.append(tabs->tabText(index));
            }
        }
        return titles;
    };

    SettingsDialog withRegistry(settings, &registry);
    CHECK(tabTitles(withRegistry).contains("Keyboard Shortcuts"));
    CHECK(withRegistry.findChild<KeybindingsWidget*>() != nullptr);

    SettingsDialog withoutRegistry(settings, nullptr);
    CHECK_FALSE(tabTitles(withoutRegistry).contains("Keyboard Shortcuts"));
    CHECK(withoutRegistry.findChild<KeybindingsWidget*>() == nullptr);
}

// Resolving a conflict changes two rows, not one: the row typed into and whichever was holding
// that key before. Both have to lose the error colour.
TEST_CASE("The keybindings page repaints both sides of a conflict", "[qt][keybindings]") {
    auto settings = std::make_shared<Settings>();
    KeyBindingRegistry registry(settings);
    KeybindingsWidget widget(&registry);

    auto* tree = widget.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);

    QTreeWidgetItem* mapInfo = test::findTreeItem(*tree, "Map Information Panel");
    QTreeWidgetItem* selection = test::findTreeItem(*tree, "Selection Panel");
    REQUIRE(mapInfo != nullptr);
    REQUIRE(selection != nullptr);

    const QBrush plain = mapInfo->foreground(1);

    // Put Map Info on the Selection panel's key: both rows are now half of a conflict.
    REQUIRE(QMetaObject::invokeMethod(&widget, "onEditFinished", Q_ARG(QTreeWidgetItem*, mapInfo),
        Q_ARG(QKeySequence, QKeySequence("Alt+2"))));
    CHECK(mapInfo->foreground(1) != plain);
    CHECK(selection->foreground(1) != plain);

    // Move it somewhere free: neither row is conflicting any more.
    REQUIRE(QMetaObject::invokeMethod(&widget, "onEditFinished", Q_ARG(QTreeWidgetItem*, mapInfo),
        Q_ARG(QKeySequence, QKeySequence("Ctrl+Alt+J"))));
    CHECK(mapInfo->foreground(1) == plain);
    CHECK(selection->foreground(1) == plain);

    // A conflicting edit is never committed — it would leave Qt firing neither shortcut.
    REQUIRE(QMetaObject::invokeMethod(&widget, "onEditFinished", Q_ARG(QTreeWidgetItem*, mapInfo),
        Q_ARG(QKeySequence, QKeySequence("Alt+2"))));
    widget.applyChanges();
    CHECK(registry.shortcut(actions::PANEL_SELECTION) == QKeySequence("Alt+2"));
    CHECK(registry.shortcut(actions::PANEL_MAP_INFO) == QKeySequence("Alt+1"));
}

// The tree is rebuilt on reload/apply, so a filter left in the box has to survive it.
TEST_CASE("The keybindings page keeps its filter across a rebuild", "[qt][keybindings]") {
    auto settings = std::make_shared<Settings>();
    KeyBindingRegistry registry(settings);
    KeybindingsWidget widget(&registry);

    auto* tree = widget.findChild<QTreeWidget*>();
    auto* filter = widget.findChild<QLineEdit*>();
    REQUIRE(tree != nullptr);
    REQUIRE(filter != nullptr);

    filter->setText("Selection Panel");
    QApplication::processEvents();
    REQUIRE(test::visibleLeafCount(*tree) == 1);

    widget.reload(); // rebuilds the tree
    CHECK(filter->text() == "Selection Panel");
    CHECK(test::visibleLeafCount(*tree) == 1);
}

// Esc, Delete, Backspace and Space belong to the map view's tool state machine, and a shortcut on
// one would consume it before the canvas sees it; a bare letter on an Application action would
// fire while typing in any panel.
TEST_CASE("Reserved and canvas-only keys cannot be bound", "[qt][keybindings]") {
    auto settings = std::make_shared<Settings>();
    settings->setKeyBindings({ { actions::FIT_MAP, "Del" }, { actions::PANEL_LOG, "Q" } });
    KeyBindingRegistry registry(settings);

    // Nothing shipped breaks the rule, or Reset would be refused.
    for (const ActionSpec& spec : actionSpecs()) {
        const QString id = QString::fromLatin1(spec.id);
        CHECK(KeyBindingRegistry::isAllowed(id, registry.defaultShortcut(id)));
    }

    // Stored overrides that break the rule are dropped on load.
    CHECK(registry.shortcut(actions::FIT_MAP) == registry.defaultShortcut(actions::FIT_MAP));
    CHECK(registry.shortcut(actions::PANEL_LOG) == registry.defaultShortcut(actions::PANEL_LOG));

    for (const QKeySequence& reserved : { QKeySequence(Qt::Key_Escape), QKeySequence(Qt::Key_Delete),
             QKeySequence(Qt::Key_Backspace), QKeySequence(Qt::Key_Space), QKeySequence(Qt::Key_Enter) }) {
        registry.setShortcut(actions::FIT_MAP, reserved);
        CHECK(registry.shortcut(actions::FIT_MAP) == QKeySequence(Qt::Key_F));
    }

    registry.setShortcut(actions::PANEL_LOG, QKeySequence(Qt::Key_R));
    CHECK(registry.shortcut(actions::PANEL_LOG) == registry.defaultShortcut(actions::PANEL_LOG));
    registry.setShortcut(actions::PANEL_LOG, QKeySequence(Qt::Key_Return));
    CHECK(registry.shortcut(actions::PANEL_LOG) == registry.defaultShortcut(actions::PANEL_LOG));

    // With a modifier, or on a canvas action, they are fine.
    registry.setShortcut(actions::PANEL_LOG, QKeySequence("Ctrl+Alt+R"));
    CHECK(registry.shortcut(actions::PANEL_LOG) == QKeySequence("Ctrl+Alt+R"));
    registry.setShortcut(actions::FIT_MAP, QKeySequence(Qt::Key_Z));
    CHECK(registry.shortcut(actions::FIT_MAP) == QKeySequence(Qt::Key_Z));

    // The page refuses them too, without leaving a pending edit behind.
    registry.resetAllToDefaults();
    KeybindingsWidget widget(&registry);
    auto* tree = widget.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);
    QTreeWidgetItem* fitMap = test::findTreeItem(*tree, "Fit Map in View");
    REQUIRE(fitMap != nullptr);
    REQUIRE(QMetaObject::invokeMethod(&widget, "onEditFinished", Q_ARG(QTreeWidgetItem*, fitMap),
        Q_ARG(QKeySequence, QKeySequence(Qt::Key_Delete))));
    CHECK_FALSE(widget.hasPendingChanges());
}

// Dropping one conflicting edit puts that action back on its committed key, which can collide with
// an edit that only looked free while the dropped one was pending.
TEST_CASE("Applying drops edits that conflict once another edit is dropped", "[qt][keybindings]") {
    auto settings = std::make_shared<Settings>();
    KeyBindingRegistry registry(settings);
    KeybindingsWidget widget(&registry);
    auto* tree = widget.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);

    QTreeWidgetItem* scripts = test::findTreeItem(*tree, "Scripts Panel");
    QTreeWidgetItem* mapInfo = test::findTreeItem(*tree, "Map Information Panel");
    REQUIRE(scripts != nullptr);
    REQUIRE(mapInfo != nullptr);

    // Scripts onto the Tile Palette's key (a conflict, so it will be dropped), then Map Info onto
    // the key Scripts appears to be leaving.
    REQUIRE(QMetaObject::invokeMethod(&widget, "onEditFinished", Q_ARG(QTreeWidgetItem*, scripts),
        Q_ARG(QKeySequence, QKeySequence("Alt+4"))));
    REQUIRE(QMetaObject::invokeMethod(&widget, "onEditFinished", Q_ARG(QTreeWidgetItem*, mapInfo),
        Q_ARG(QKeySequence, QKeySequence("Alt+3"))));
    widget.applyChanges();

    CHECK(registry.shortcut(actions::PANEL_SCRIPTS) == QKeySequence("Alt+3"));
    CHECK(registry.shortcut(actions::PANEL_MAP_INFO) == QKeySequence("Alt+1"));
    CHECK(registry.shortcut(actions::PANEL_TILE_PALETTE) == QKeySequence("Alt+4"));
}

TEST_CASE("Clear unbinds the selected row and resolves its conflict", "[qt][keybindings]") {
    auto settings = std::make_shared<Settings>();
    KeyBindingRegistry registry(settings);
    KeybindingsWidget widget(&registry);
    auto* tree = widget.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);

    QTreeWidgetItem* mapInfo = test::findTreeItem(*tree, "Map Information Panel");
    QTreeWidgetItem* selection = test::findTreeItem(*tree, "Selection Panel");
    REQUIRE(mapInfo != nullptr);
    REQUIRE(selection != nullptr);

    REQUIRE(QMetaObject::invokeMethod(&widget, "onEditFinished", Q_ARG(QTreeWidgetItem*, mapInfo),
        Q_ARG(QKeySequence, QKeySequence("Alt+2"))));

    QAbstractButton* clear = test::findButton(widget, "Clear");
    REQUIRE(clear != nullptr);

    tree->setCurrentItem(selection);
    REQUIRE(clear->isEnabled());
    clear->click();
    widget.applyChanges();

    CHECK(registry.shortcut(actions::PANEL_SELECTION).isEmpty());
    CHECK(registry.shortcut(actions::PANEL_MAP_INFO) == QKeySequence("Alt+2"));
}
