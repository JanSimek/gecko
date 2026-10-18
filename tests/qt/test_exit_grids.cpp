#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QMenu>

#include <memory>
#include <vector>

#include "resource/MapNameResolver.h"
#include "support/DataTree.h"
#include "support/EditorResources.h"
#include "support/MapObjectBuilder.h"
#include "support/QtFind.h"
#include "support/QtMainWindow.h"
#include "ui/core/EditorWidget.h"
#include "ui/core/MainWindow.h"
#include "ui/dialogs/ExitGridPropertiesDialog.h"
#include "ui/tools/ExitGridPlacementManager.h"
#include "util/Constants.h"

namespace test = geck::test;

// The exit-grid dialog's by-name destination-map picker. The fixture mounts a tiny maps.txt with two
// maps (out of alphabetical order, to verify sorting) plus a negative-index section (to verify it is
// skipped), and a matching map.msg so each map resolves a primary (elevation 0) display name.
namespace {

struct ExitGridFixture {
    test::DataTree data{ "exit-grid-maps" };
    std::unique_ptr<geck::resource::MapNameResolver> names;

    ExitGridFixture() {
        // Map 0 -> klamall.map, Map 1 -> arcaves.map; sorted by filename, arcaves precedes klamall.
        data.write("data/maps.txt",
            "[Map 0]\nlookup_name=Klamath\nmap_name=klamall\n"
            "[Map 1]\nlookup_name=Caves\nmap_name=arcaves\n"
            "[Map -1]\nlookup_name=Sentinel\nmap_name=ignored\n");
        // displayName(index, 0) reads map.msg[index*3 + 200]: Map 0 -> 200, Map 1 -> 203.
        data.write("text/english/game/map.msg",
            test::msgLine(200, "Klamath") + test::msgLine(203, "Arroyo Caves"));
        names = std::make_unique<geck::resource::MapNameResolver>(data.mount());
    }
};

} // namespace

TEST_CASE("ExitGridPropertiesDialog picks a destination from the mounted maps", "[qt][exitgrid]") {
    ExitGridFixture fixture;
    geck::ExitGridPropertiesDialog dialog(nullptr, fixture.names.get());

    auto* combo = dialog.findChild<QComboBox*>("destinationMap");
    REQUIRE(combo != nullptr);

    SECTION("mounted maps are listed by name, sorted, with the maps.txt index as data") {
        // Sorted case-insensitively by filename: arcaves before klamall. The negative section is skipped.
        // maps.txt's map_name normalizes to a loadable "<name>.map" filename.
        REQUIRE(combo->count() == 2);
        CHECK(combo->itemText(0) == QStringLiteral("arcaves.map · Arroyo Caves"));
        CHECK(combo->itemData(0).toInt() == 1);
        CHECK(combo->itemText(1) == QStringLiteral("klamall.map · Klamath"));
        CHECK(combo->itemData(1).toInt() == 0);
    }

    SECTION("the selected map's index is returned") {
        combo->setCurrentIndex(combo->findData(0)); // klamall.map (maps.txt index 0)
        CHECK(dialog.getProperties().exitMap == 0u);

        combo->setCurrentIndex(combo->findData(1)); // arcaves.map (maps.txt index 1)
        CHECK(dialog.getProperties().exitMap == 1u);
    }

    SECTION("world/town exits disable the picker and report the sentinel") {
        geck::ExitGridPropertiesDialog::ExitGridProperties world;
        world.exitMap = geck::ExitGrid::WORLD_MAP_EXIT;
        dialog.setProperties(world);
        CHECK_FALSE(combo->isEnabled());
        CHECK(dialog.getProperties().exitMap == geck::ExitGrid::WORLD_MAP_EXIT);

        geck::ExitGridPropertiesDialog::ExitGridProperties town;
        town.exitMap = geck::ExitGrid::TOWN_MAP_EXIT;
        dialog.setProperties(town);
        CHECK_FALSE(combo->isEnabled());
        CHECK(dialog.getProperties().exitMap == geck::ExitGrid::TOWN_MAP_EXIT);
    }

    SECTION("a destination not listed in maps.txt is preserved") {
        geck::ExitGridPropertiesDialog::ExitGridProperties props;
        props.exitMap = 4242; // a map id that isn't in the mounted maps.txt
        dialog.setProperties(props);

        // An unknown id is preserved as a selected "Map <id>" entry and round-trips unchanged.
        CHECK(combo->currentText() == QStringLiteral("Map 4242"));
        CHECK(combo->currentData().toInt() == 4242);
        CHECK(dialog.getProperties().exitMap == 4242u);
    }
}

TEST_CASE("ExitGridPropertiesDialog operates with no resolver and preserves the destination id", "[qt][exitgrid]") {
    // No game data mounted (null resolver): the combo has no map entries but the dialog still works.
    geck::ExitGridPropertiesDialog dialog(nullptr, nullptr);

    auto* combo = dialog.findChild<QComboBox*>("destinationMap");
    REQUIRE(combo != nullptr);

    geck::ExitGridPropertiesDialog::ExitGridProperties props;
    props.exitMap = 7;
    dialog.setProperties(props);
    CHECK(combo->currentData().toInt() == 7);
    CHECK(dialog.getProperties().exitMap == 7u);
}

// The unified Exit-Grids tool is one checkable button plus a dropdown whose two items ("Draw edge" /
// "Place single hex") are exclusive: triggering one ticks it, unticks the other, keeps the button
// checked. (Driving the editor mode needs game data, so that half is covered below against EditorWidget.)
TEST_CASE("Unified Exit-Grids tool exposes one button with an exclusive sub-mode dropdown", "[qt][exitgrids]") {
    test::MainWindowFixture fixture;
    geck::MainWindow& window = fixture.window;

    // The two former separate buttons are gone, replaced by one "Exit Grids" button.
    CHECK(test::findActionByText(window, "Mark Exits") == nullptr);
    CHECK(test::findActionByText(window, "Place Exit Grids") == nullptr);

    auto* exitGridsAction = test::findActionByText(window, "Exit Grids");
    auto* drawEdge = test::findActionByText(window, "Draw edge");
    auto* placeHex = test::findActionByText(window, "Place single hex");
    REQUIRE(exitGridsAction != nullptr);
    REQUIRE(drawEdge != nullptr);
    REQUIRE(placeHex != nullptr);
    CHECK(exitGridsAction->isCheckable());
    CHECK(exitGridsAction->menu() != nullptr); // dropdown attached to the button

    // Default sub-mode is "Draw edge".
    CHECK(drawEdge->isChecked());
    CHECK_FALSE(placeHex->isChecked());

    // Triggering "Place single hex" makes it the (exclusive) checked sub-mode and keeps the tool on.
    placeHex->trigger();
    QApplication::processEvents();
    CHECK(placeHex->isChecked());
    CHECK_FALSE(drawEdge->isChecked());
    CHECK(exitGridsAction->isChecked());

    // Triggering "Draw edge" flips the exclusive selection back.
    drawEdge->trigger();
    QApplication::processEvents();
    CHECK(drawEdge->isChecked());
    CHECK_FALSE(placeHex->isChecked());
    CHECK(exitGridsAction->isChecked());
}

// The editor-mode half: the two sub-modes map to distinct EditorWidget modes (PlaceExitGrid vs
// MarkExits). Constructing an EditorWidget eagerly loads art/misc/HEX.frm, which the bundled
// resources/ ships.
TEST_CASE("EditorWidget switches between the two exit-grid sub-modes", "[qt][exitgrids]") {
    auto resources = geck::test::editorResources();
    auto editor = std::make_unique<geck::EditorWidget>(*resources, test::mapOf("exitgrids.map"));

    CHECK(editor->currentMode() == geck::EditorMode::Select);

    editor->setMarkExitsMode(true); // "Draw edge"
    CHECK(editor->currentMode() == geck::EditorMode::MarkExits);

    editor->setExitGridPlacementMode(true); // "Place single hex"
    CHECK(editor->currentMode() == geck::EditorMode::PlaceExitGrid);

    editor->setMode(geck::EditorMode::Select);
    CHECK(editor->currentMode() == geck::EditorMode::Select);
}

// Regression: a Draw-edge stroke that only PARTIALLY overlaps existing exit grids must still place on
// the empty hexes (a single overlapping hex used to send the whole stroke to bulk-edit, so the
// placement vanished — "Enter + OK, nothing appears"). freshHexesForLine = line hexes minus occupied.
TEST_CASE("Exit-grid stroke creates on the non-overlapping hexes (partial overlap doesn't swallow it)",
    "[exitgrid][placement]") {
    using geck::ExitGridPlacementManager;

    // No overlap -> every hex is fresh.
    CHECK(ExitGridPlacementManager::freshHexesForLine({ 1, 2, 3 }, {}) == std::vector<int>{ 1, 2, 3 });

    // Partial overlap -> only the empty hexes are returned (the stroke still places on those).
    CHECK(ExitGridPlacementManager::freshHexesForLine({ 1, 2, 3, 4 }, { 2, 3 })
        == std::vector<int>{ 1, 4 });

    // Whole stroke already occupied -> empty, so the caller bulk-edits the existing edge instead.
    CHECK(ExitGridPlacementManager::freshHexesForLine({ 5, 6 }, { 5, 6 }).empty());

    // Empty line -> empty.
    CHECK(ExitGridPlacementManager::freshHexesForLine({}, { 1 }).empty());
}
