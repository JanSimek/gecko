#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QShortcut>
#include <QSignalSpy>

#include <array>
#include <utility>

#include "format/map/Map.h"
#include "support/EditorResources.h"
#include "support/MapObjectBuilder.h"
#include "support/QtFind.h"
#include "support/QtMainWindow.h"
#include "support/QtWait.h"
#include "ui/core/EditorWidget.h"
#include "ui/core/MainWindow.h"
#include "ui/input/ActionSpec.h"
#include "ui/input/KeyBindingRegistry.h"
#include "ui/widgets/SFMLWidget.h"
#include "ui/widgets/WelcomeWidget.h"
#include "util/Constants.h"

namespace test = geck::test;

TEST_CASE("MainWindow panel toggles stay wired in the no-map layout", "[main_window]") {
    test::MainWindowFixture fixture;
    geck::MainWindow& window = fixture.window;

    auto* mapInfoDock = window.findChild<QDockWidget*>("MapInfoDock");
    auto* selectionDock = window.findChild<QDockWidget*>("SelectionDock");
    auto* tilePaletteDock = window.findChild<QDockWidget*>("TilePaletteDock");
    auto* objectPaletteDock = window.findChild<QDockWidget*>("ObjectPaletteDock");
    auto* fileBrowserDock = window.findChild<QDockWidget*>("FileBrowserDock");
    auto* selectionAction = fixture.menuAction("Selection");

    REQUIRE(mapInfoDock != nullptr);
    REQUIRE(selectionDock != nullptr);
    REQUIRE(tilePaletteDock != nullptr);
    REQUIRE(objectPaletteDock != nullptr);
    REQUIRE(fileBrowserDock != nullptr);
    REQUIRE(selectionAction != nullptr);

    REQUIRE_FALSE(mapInfoDock->isVisible());
    REQUIRE_FALSE(selectionDock->isVisible());
    REQUIRE_FALSE(tilePaletteDock->isVisible());
    REQUIRE_FALSE(objectPaletteDock->isVisible());
    REQUIRE(fileBrowserDock->isVisible());
    REQUIRE_FALSE(selectionAction->isChecked());

    selectionAction->trigger();
    QApplication::processEvents();

    REQUIRE(selectionDock->isVisible());
    REQUIRE(selectionAction->isChecked());

    selectionAction->trigger();
    QApplication::processEvents();

    REQUIRE_FALSE(selectionDock->isVisible());
    REQUIRE_FALSE(selectionAction->isChecked());
}

// One MainWindow serves the three checks on its window-scoped action shortcuts (each block scoped).
TEST_CASE("Window-scoped actions carry the shortcut table's keys, each key once", "[main_window]") {
    test::MainWindowFixture fixture;
    geck::MainWindow& window = fixture.window;

    // The panel shortcut family: Alt+1..6 on the six managed panels, Alt+` on the Log dock
    // (ui/input/ActionSpec.cpp). Alt+digit rather than Ctrl+digit so Ctrl+1..3 stays free for the
    // elevations and a panel's filter box never swallows the key.
    {
        struct ExpectedShortcut {
            const char* actionText;
            QKeySequence keys;
        };

        const std::array<ExpectedShortcut, 7> expected = { {
            { "Map Information", QKeySequence(Qt::ALT | Qt::Key_1) },
            { "Selection", QKeySequence(Qt::ALT | Qt::Key_2) },
            { "Scripts", QKeySequence(Qt::ALT | Qt::Key_3) },
            { "Tile Palette", QKeySequence(Qt::ALT | Qt::Key_4) },
            { "Object Palette", QKeySequence(Qt::ALT | Qt::Key_5) },
            { "Virtual File System Browser", QKeySequence(Qt::ALT | Qt::Key_6) },
            { "Log", QKeySequence(Qt::ALT | Qt::Key_QuoteLeft) },
        } };

        for (const ExpectedShortcut& entry : expected) {
            QAction* action = fixture.menuAction(entry.actionText);
            REQUIRE(action != nullptr);
            CHECK(action->shortcut() == entry.keys);
        }
    }

    // B and R used to be window-scoped QAction shortcuts, which fired while a palette grid or a tree
    // had focus: typing "b" in such a place started placing scroll blockers. Both now live on the
    // canvas, so the actions themselves must carry no shortcut.
    {
        for (const char* text : { "Scroll Blocker Rectangle", "Rotate" }) {
            QAction* action = test::findActionByText(window, text);
            REQUIRE(action != nullptr);
            CHECK(action->shortcut().isEmpty());
        }

        // Ctrl+Shift+E is a command, not a single letter, so it stays window-scoped.
        QAction* editSource = test::findActionByText(window, "Edit Script Source");
        REQUIRE(editSource != nullptr);
        CHECK(editSource->shortcut() == QKeySequence("Ctrl+Shift+E"));
    }

    // Two enabled actions on one key make Qt fire neither and warn about an ambiguous overload, and
    // nothing else notices: the menus and the toolbar both hand out shortcuts, so this sweeps the
    // whole live window rather than just the shipped table.
    {
        QHash<QString, QString> owners; // key text -> the action already holding it
        for (const QAction* action : window.findChildren<QAction*>()) {
            const QKeySequence keys = action->shortcut();
            if (keys.isEmpty()) {
                continue;
            }
            const QString text = keys.toString();
            INFO("key " << text.toStdString() << " wanted by " << test::plainText(*action).toStdString()
                        << " and " << owners.value(text).toStdString());
            CHECK_FALSE(owners.contains(text));
            owners.insert(text, test::plainText(*action));
        }
    }
}

// The reason the panel keys reveal rather than plain-toggle: panels are tabbed, so a dock Qt calls
// visible may be sitting behind another tab. Toggling would take it away exactly when the user asked
// to see it.
TEST_CASE("A panel action raises a tabbed-behind dock instead of hiding it", "[main_window]") {
    test::MainWindowFixture fixture;
    geck::MainWindow& window = fixture.window;

    auto* mapInfoDock = window.findChild<QDockWidget*>("MapInfoDock");
    auto* selectionDock = window.findChild<QDockWidget*>("SelectionDock");
    auto* selectionAction = fixture.menuAction("Selection");
    REQUIRE(mapInfoDock != nullptr);
    REQUIRE(selectionDock != nullptr);
    REQUIRE(selectionAction != nullptr);

    window.tabifyDockWidget(mapInfoDock, selectionDock);
    mapInfoDock->show();
    selectionDock->show();
    mapInfoDock->raise(); // Map Info is the tab on top, Selection is behind it

    REQUIRE(geck::test::pumpUntil([mapInfoDock]() { return !mapInfoDock->visibleRegion().isEmpty(); }));
    REQUIRE_FALSE(selectionDock->isHidden());          // visible to Qt...
    REQUIRE(selectionDock->visibleRegion().isEmpty()); // ...but not on screen

    selectionAction->trigger();

    CHECK(geck::test::pumpUntil([selectionDock]() { return !selectionDock->visibleRegion().isEmpty(); })); // raised to the front tab
    CHECK_FALSE(selectionDock->isHidden());
    CHECK(selectionAction->isChecked());

    // Now that it is the tab on top, the same action puts it away.
    selectionAction->trigger();

    CHECK(geck::test::pumpUntil([selectionDock]() { return selectionDock->isHidden(); }));
    CHECK_FALSE(selectionAction->isChecked());
}

// File > Close Map returns to the welcome screen. It must exist with the standard Close shortcut,
// and triggering it with nothing open must be a safe no-op (no editor spawned, no crash) — the
// handler guards on hasActiveMap() before touching the teardown path.
TEST_CASE("File menu offers Close Map, and closing with no map open is a safe no-op", "[main_window]") {
    test::MainWindowFixture fixture;
    geck::MainWindow& window = fixture.window;

    QAction* closeMap = test::findActionByText(window, "Close Map");
    REQUIRE(closeMap != nullptr);
    CHECK(closeMap->shortcut() == QKeySequence(QKeySequence::Close));

    // No map loaded -> the welcome screen is showing and there is no editor widget.
    REQUIRE(window.findChildren<geck::EditorWidget*>().isEmpty());

    closeMap->trigger();
    QApplication::processEvents();

    CHECK(window.findChildren<geck::EditorWidget*>().isEmpty());
}

// The editor-navigation keys. Elevation is the everyday one: Ctrl+digit, the counterpart of the
// panels' Alt+digit. They start disabled and updateElevationMenu() enables only what the open map
// has, so the key is a no-op on a map without that elevation rather than a switch to nothing.
TEST_CASE("Elevation actions carry Ctrl+digit and stay no-ops until a map has that elevation", "[main_window]") {
    test::MainWindowFixture fixture(test::editorResources());
    geck::MainWindow& window = fixture.window;

    struct ExpectedShortcut {
        const char* actionText;
        QKeySequence keys;
    };

    const std::array<ExpectedShortcut, 3> expected = { {
        { "Elevation 1", QKeySequence(Qt::CTRL | Qt::Key_1) },
        { "Elevation 2", QKeySequence(Qt::CTRL | Qt::Key_2) },
        { "Elevation 3", QKeySequence(Qt::CTRL | Qt::Key_3) },
    } };

    for (const ExpectedShortcut& entry : expected) {
        QAction* action = test::findActionByText(window, entry.actionText);
        REQUIRE(action != nullptr);
        CHECK(action->shortcut() == entry.keys);
        CHECK_FALSE(action->isEnabled()); // no map open
    }

    // A map with elevations 1 and 2 enables exactly those, and the key switches the editor there.
    auto map = test::mapOf("elevations.map");
    map->getMapFile().header.flags = geck::Map::elevationFlag(geck::ELEVATION_3);
    geck::EditorWidget* editor = &fixture.openMap(std::move(map));

    QAction* elevation1 = test::findActionByText(window, "Elevation 1");
    QAction* elevation2 = test::findActionByText(window, "Elevation 2");
    QAction* elevation3 = test::findActionByText(window, "Elevation 3");
    REQUIRE(elevation1 != nullptr);
    REQUIRE(elevation2 != nullptr);
    REQUIRE(elevation3 != nullptr);
    CHECK(elevation1->isEnabled());
    CHECK(elevation2->isEnabled());
    CHECK_FALSE(elevation3->isEnabled());
    REQUIRE(editor->getCurrentElevation() == geck::ELEVATION_1);

    elevation2->trigger();
    CHECK(editor->getCurrentElevation() == geck::ELEVATION_2);
}

// "Inspect the selection": Return (and numpad Enter, a distinct key code) reveals the Selection
// panel. Both are scoped to the canvas so a Return typed in a panel's filter box is left alone, and
// both must stand down in MarkExits mode, where Enter finalizes the in-progress Draw-edge polyline
// (InputHandler). Constructing an EditorWidget eagerly loads art/misc/HEX.frm, which the bundled
// resources/ ships.
TEST_CASE("Inspect-selection keys live on the canvas and stand down in Draw-edge mode", "[main_window]") {
    test::MainWindowFixture fixture(test::editorResources());
    geck::MainWindow& window = fixture.window;

    geck::EditorWidget* editorWidget = &fixture.openMap(test::mapOf("inspect.map"));
    geck::SFMLWidget* canvas = editorWidget->getSFMLWidget();
    REQUIRE(canvas != nullptr);

    // The keys hang off the canvas, not the window: WidgetWithChildrenShortcut is what keeps them
    // out of the panels' text fields.
    const QList<QShortcut*> canvasShortcuts = canvas->findChildren<QShortcut*>();
    QList<QKeySequence> keys;
    for (const QShortcut* shortcut : canvasShortcuts) {
        CHECK(shortcut->context() == Qt::WidgetWithChildrenShortcut);
        keys.append(shortcut->key());
    }
    for (const QKeySequence& expected : { QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter),
             QKeySequence(Qt::Key_S), QKeySequence(Qt::Key_G), QKeySequence(Qt::Key_B),
             QKeySequence(Qt::Key_R), QKeySequence(Qt::Key_Home), QKeySequence(Qt::Key_F) }) {
        CHECK(keys.contains(expected));
    }

    // Shortcuts are looked up by key rather than swept as a group, so one added later for
    // something else cannot fail this test.
    const auto shortcutFor = [canvas](const QKeySequence& wanted) -> const QShortcut* {
        return test::findChildWhere<QShortcut>(*canvas, [&wanted](const QShortcut& shortcut) { return shortcut.key() == wanted; });
    };

    const QShortcut* inspectReturn = shortcutFor(QKeySequence(Qt::Key_Return));
    const QShortcut* inspectEnter = shortcutFor(QKeySequence(Qt::Key_Enter));
    REQUIRE(inspectReturn != nullptr);
    REQUIRE(inspectEnter != nullptr);
    CHECK(inspectReturn->isEnabled());
    CHECK(inspectEnter->isEnabled());

    // Stamping: R belongs to the viewport, where it cycles the pattern's orientation variants, so
    // the canvas shortcut stands down and the key falls through to InputHandler. The toolbar
    // button stays enabled — only the key steps aside.
    const QShortcut* rotate = shortcutFor(QKeySequence(Qt::Key_R));
    REQUIRE(rotate != nullptr);
    QAction* rotateAction = test::findActionByText(window, "Rotate");
    REQUIRE(rotateAction != nullptr);

    editorWidget->setMode(geck::EditorMode::StampPattern);
    QApplication::processEvents();
    CHECK_FALSE(rotate->isEnabled());
    CHECK(rotateAction->isEnabled());

    editorWidget->setMode(geck::EditorMode::Select);
    QApplication::processEvents();
    CHECK(rotate->isEnabled());

    // Draw edge: Enter belongs to the polyline, so the two reveal keys step aside.
    editorWidget->setMarkExitsMode(true);
    QApplication::processEvents();
    REQUIRE(editorWidget->currentMode() == geck::EditorMode::MarkExits);
    CHECK_FALSE(inspectReturn->isEnabled());
    CHECK_FALSE(inspectEnter->isEnabled());

    // ...and only those two: the navigation keys are unaffected by the mode.
    const QShortcut* centerOnPlayer = shortcutFor(QKeySequence(Qt::Key_Home));
    const QShortcut* fitMap = shortcutFor(QKeySequence(Qt::Key_F));
    REQUIRE(centerOnPlayer != nullptr);
    REQUIRE(fitMap != nullptr);
    CHECK(centerOnPlayer->isEnabled());
    CHECK(fitMap->isEnabled());

    // Leaving the mode hands them back.
    editorWidget->setMode(geck::EditorMode::Select);
    QApplication::processEvents();
    CHECK(inspectReturn->isEnabled());
    CHECK(inspectEnter->isEnabled());

    // Numpad Enter is a companion of the reveal key, not a binding of its own: rebind reveal
    // elsewhere and it clears, so numpad Enter never keeps working a key the user moved away.
    auto* registry = window.findChild<geck::KeyBindingRegistry*>();
    REQUIRE(registry != nullptr);

    registry->setShortcut(geck::actions::PANEL_SELECTION_REVEAL, QKeySequence("Ctrl+Alt+I"));
    CHECK(inspectReturn->key() == QKeySequence("Ctrl+Alt+I"));
    CHECK(inspectEnter->key().isEmpty());

    registry->resetToDefault(geck::actions::PANEL_SELECTION_REVEAL);
    CHECK(inspectReturn->key() == QKeySequence(Qt::Key_Return));
    CHECK(inspectEnter->key() == QKeySequence(Qt::Key_Enter));

    // Standing down goes by key, not by action: move Rotate off R and put Fit Map on it, and it is
    // Fit Map that must step aside while stamping, or the stamp never sees R. A rebind made while
    // already in the mode takes effect at once.
    registry->setShortcut(geck::actions::TOOL_ROTATE, QKeySequence(Qt::Key_T));
    registry->setShortcut(geck::actions::FIT_MAP, QKeySequence(Qt::Key_R));
    editorWidget->setMode(geck::EditorMode::StampPattern);
    QApplication::processEvents();
    CHECK(rotate->isEnabled());
    CHECK_FALSE(fitMap->isEnabled());

    registry->setShortcut(geck::actions::FIT_MAP, QKeySequence(Qt::Key_F));
    CHECK(fitMap->isEnabled());
    registry->setShortcut(geck::actions::FIT_MAP, QKeySequence(Qt::Key_Return));
    CHECK(fitMap->isEnabled()); // Return is only claimed while drawing an edge

    editorWidget->setMarkExitsMode(true);
    QApplication::processEvents();
    CHECK_FALSE(fitMap->isEnabled());

    editorWidget->setMode(geck::EditorMode::Select);
    registry->resetAllToDefaults();
}

// The no-map welcome screen offers New Map / Browse Maps / Preferences buttons; each must emit the
// matching request signal (MainWindow wires these to the File-menu handlers) so the screen is
// actionable.
TEST_CASE("Welcome screen buttons request New Map, Browse Maps and Preferences", "[welcome_widget]") {
    geck::WelcomeWidget welcome;

    QSignalSpy newSpy(&welcome, &geck::WelcomeWidget::newMapRequested);
    QSignalSpy browseSpy(&welcome, &geck::WelcomeWidget::browseMapsRequested);
    QSignalSpy preferencesSpy(&welcome, &geck::WelcomeWidget::preferencesRequested);
    REQUIRE(newSpy.isValid());
    REQUIRE(browseSpy.isValid());
    REQUIRE(preferencesSpy.isValid());

    QAbstractButton* newMapButton = test::findButton(welcome, "New Map");
    QAbstractButton* browseButton = test::findButton(welcome, "Browse Maps…");
    QAbstractButton* preferencesButton = test::findButton(welcome, "Preferences…");
    REQUIRE(newMapButton != nullptr);
    REQUIRE(browseButton != nullptr);
    REQUIRE(preferencesButton != nullptr);

    newMapButton->click();
    CHECK(newSpy.count() == 1);
    CHECK(browseSpy.count() == 0);

    browseButton->click();
    CHECK(browseSpy.count() == 1);
    CHECK(newSpy.count() == 1); // the New Map click above, unchanged

    preferencesButton->click();
    CHECK(preferencesSpy.count() == 1);
    CHECK(newSpy.count() == 1);
    CHECK(browseSpy.count() == 1);
}
