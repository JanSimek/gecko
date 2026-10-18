#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

#include "editor/Object.h"
#include "format/frm/Frm.h"
#include "format/map/MapObject.h"
#include "selection/SelectionState.h"
#include "util/Types.h"

#include "support/GraphicsTestEnv.h"

using namespace geck;
using namespace geck::selection;

//==============================================================================
// SECTION: SelectionState Tests
//==============================================================================

TEST_CASE("SelectionState adds, removes and clears typed items", "[selection_state]") {
    SelectionState state;

    SECTION("Initial state is empty") {
        REQUIRE(state.isEmpty());
        REQUIRE(state.count() == 0);
    }

    SECTION("Adding and removing tile items") {
        // Test floor tile selection
        SelectedItem floorTile{ SelectionType::FLOOR_TILE, 150 };
        state.addItem(floorTile);

        REQUIRE_FALSE(state.isEmpty());
        REQUIRE(state.count() == 1);
        REQUIRE(state.hasItem(floorTile));

        // Test roof tile selection
        SelectedItem roofTile{ SelectionType::ROOF_TILE, 250 };
        state.addItem(roofTile);

        REQUIRE(state.count() == 2);
        REQUIRE(state.hasItem(floorTile));
        REQUIRE(state.hasItem(roofTile));

        // Test removing items
        state.removeItem(floorTile);
        REQUIRE(state.count() == 1);
        REQUIRE_FALSE(state.hasItem(floorTile));
        REQUIRE(state.hasItem(roofTile));
    }

    SECTION("Clear functionality") {
        // Add multiple items
        state.addItem({ SelectionType::FLOOR_TILE, 100 });
        state.addItem({ SelectionType::ROOF_TILE, 200 });
        state.addItem({ SelectionType::HEX, 300 });

        REQUIRE(state.count() == 3);

        state.clear();
        REQUIRE(state.isEmpty());
        REQUIRE(state.count() == 0);
    }

    SECTION("floor, roof and hex items stay apart by type") {
        // A floor and a roof tile share an index; the hex lies past the tile range (hexes run to 39,999).
        state.addItem({ SelectionType::FLOOR_TILE, 1500 });
        state.addItem({ SelectionType::ROOF_TILE, 1500 });
        state.addItem({ SelectionType::HEX, 15000 });

        REQUIRE(state.count() == 3);
        CHECK(state.getFloorTileIndices() == std::vector<int>{ 1500 });
        CHECK(state.getRoofTileIndices() == std::vector<int>{ 1500 });
        CHECK(state.getHexIndices() == std::vector<int>{ 15000 });
        CHECK(state.hasItem({ SelectionType::HEX, 15000 }));
        CHECK_FALSE(state.hasItem({ SelectionType::HEX, 1500 }));
    }
}

//==============================================================================
// SECTION: Selection Mode Tests
//==============================================================================

TEST_CASE("selectionModeToString names every selection mode", "[selection_modes]") {
    CHECK(std::string_view(selectionModeToString(SelectionMode::ALL)) == "All");
    CHECK(std::string_view(selectionModeToString(SelectionMode::FLOOR_TILES)) == "Floor Tiles");
    CHECK(std::string_view(selectionModeToString(SelectionMode::ROOF_TILES)) == "Roof Tiles");
    CHECK(std::string_view(selectionModeToString(SelectionMode::ROOF_TILES_ALL)) == "Roof Tiles + Empty");
    CHECK(std::string_view(selectionModeToString(SelectionMode::OBJECTS)) == "Objects");
    CHECK(std::string_view(selectionModeToString(SelectionMode::HEXES)) == "Hexes");
    CHECK(std::string_view(selectionModeToString(SelectionMode::SCROLL_BLOCKER_RECTANGLE)) == "Scroll Blocker");
}

//==============================================================================
// SECTION: SelectedItem Tests
//==============================================================================

TEST_CASE("SelectedItem reports its type and data and compares by both", "[selection_state]") {
    SECTION("Item type checking") {
        SelectedItem floorTile{ SelectionType::FLOOR_TILE, 100 };
        SelectedItem roofTile{ SelectionType::ROOF_TILE, 200 };
        SelectedItem hexItem{ SelectionType::HEX, 300 };

        // Type checking methods
        REQUIRE_FALSE(floorTile.isObject());
        REQUIRE_FALSE(roofTile.isObject());
        REQUIRE_FALSE(hexItem.isObject());
    }

    SECTION("Data retrieval") {
        SelectedItem tileItem{ SelectionType::FLOOR_TILE, 150 };
        SelectedItem hexItem{ SelectionType::HEX, 2500 };

        // Data retrieval methods
        REQUIRE(tileItem.getTileIndex() == 150);
        REQUIRE(hexItem.getHexIndex() == 2500);
    }

    SECTION("Equality comparison") {
        SelectedItem tile1{ SelectionType::FLOOR_TILE, 100 };
        SelectedItem tile2{ SelectionType::FLOOR_TILE, 100 };
        SelectedItem tile3{ SelectionType::FLOOR_TILE, 200 };
        SelectedItem roof1{ SelectionType::ROOF_TILE, 100 };

        // Same type, same data should be equal
        REQUIRE(tile1 == tile2);

        // Same type, different data should not be equal
        REQUIRE_FALSE(tile1 == tile3);

        // Different type, same data should not be equal
        REQUIRE_FALSE(tile1 == roof1);
    }
}

TEST_CASE("remapSelectedItem carries objects across a wrapper rebuild and drops removed ones", "[selection]") {
    GECK_SKIP_IF_HEADLESS_CI(); // an Object owns an sf::Texture
    geck::Frm frm("test.frm");
    geck::Direction direction;
    direction.setFrames({ geck::Frame(1, 1) });
    frm.setDirections({ direction });

    auto kept = std::make_shared<geck::MapObject>();
    auto removed = std::make_shared<geck::MapObject>();
    auto oldKept = std::make_shared<geck::Object>(&frm);
    oldKept->setMapObject(kept);
    auto oldRemoved = std::make_shared<geck::Object>(&frm);
    oldRemoved->setMapObject(removed);

    // After an undo the map still draws `kept`, through a new wrapper, and no longer holds `removed`.
    auto newKept = std::make_shared<geck::Object>(&frm);
    newKept->setMapObject(kept);
    const ObjectsByMapObject current{ { kept.get(), newKept } };

    const auto remappedKept = remapSelectedItem(SelectedItem{ SelectionType::OBJECT, oldKept }, current, std::nullopt);
    REQUIRE(remappedKept.has_value());
    CHECK(remappedKept->getObject() == newKept);
    CHECK_FALSE(remapSelectedItem(SelectedItem{ SelectionType::OBJECT, oldRemoved }, current, std::nullopt).has_value());

    // Tiles stay put without a delta and shift with one.
    CHECK(remapSelectedItem(SelectedItem{ SelectionType::FLOOR_TILE, 5 }, current, std::nullopt)->getTileIndex() == 5);
    CHECK(remapSelectedItem(SelectedItem{ SelectionType::FLOOR_TILE, 5 }, current, std::pair{ 0, 1 })->getTileIndex() != 5);
}
