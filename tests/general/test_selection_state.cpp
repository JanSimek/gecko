#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

#include "selection/SelectionState.h"
#include "util/Types.h"

using namespace geck;
using namespace geck::selection;

//==============================================================================
// SECTION: SelectionState Tests
//==============================================================================

TEST_CASE("SelectionState basic functionality", "[selection_state]") {
    SelectionState state;

    SECTION("Initial state is empty") {
        REQUIRE(state.isEmpty());
        REQUIRE(state.count() == 0);
        REQUIRE_FALSE(state.isDragging);
        REQUIRE_FALSE(state.isAreaSelecting());
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

    SECTION("Hex selection") {
        SelectedItem hexItem{ SelectionType::HEX, 2500 };
        state.addItem(hexItem);

        REQUIRE(state.count() == 1);
        REQUIRE(state.hasItem(hexItem));
        REQUIRE(hexItem.isHex());
        REQUIRE(hexItem.getHexIndex() == 2500);
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
        REQUIRE_FALSE(state.isDragging);
        REQUIRE_FALSE(state.isAreaSelecting());
    }
}

TEST_CASE("SelectionState area selection", "[selection_state]") {
    SelectionState state;

    SECTION("Area selection lifecycle") {
        sf::Vector2f startPos(100.0f, 150.0f);

        // Start area selection
        state.startAreaSelection(startPos, SelectionMode::FLOOR_TILES);
        REQUIRE(state.isAreaSelecting());
        REQUIRE(state.mode == SelectionMode::FLOOR_TILES);
        REQUIRE(state.dragStartPosition == startPos);

        // Update area selection
        sf::Vector2f currentPos(200.0f, 250.0f);
        state.updateAreaSelection(currentPos);
        REQUIRE(state.isAreaSelecting());

        if (state.selectionArea.has_value()) {
            const auto& area = state.selectionArea.value();
            REQUIRE(area.position.x == 100.0f);
            REQUIRE(area.position.y == 150.0f);
            REQUIRE(area.size.x == 100.0f); // 200 - 100
            REQUIRE(area.size.y == 100.0f); // 250 - 150
        }

        // Finish area selection
        state.finishAreaSelection();
        REQUIRE_FALSE(state.isAreaSelecting());
    }

    SECTION("Cancel area selection") {
        sf::Vector2f startPos(50.0f, 75.0f);

        state.startAreaSelection(startPos, SelectionMode::ROOF_TILES);
        REQUIRE(state.isAreaSelecting());

        state.cancelAreaSelection();
        REQUIRE_FALSE(state.isAreaSelecting());
        REQUIRE_FALSE(state.selectionArea.has_value());
    }
}

TEST_CASE("SelectionState drag operations", "[selection_state]") {
    SelectionState state;

    SECTION("Drag lifecycle") {
        sf::Vector2f startPos(300.0f, 400.0f);

        // Start drag
        state.startDrag(startPos);
        REQUIRE(state.isDragging);
        REQUIRE(state.dragStartPosition == startPos);

        // Update drag position
        sf::Vector2f currentPos(350.0f, 450.0f);
        state.updateDrag(currentPos);
        REQUIRE(state.isDragging);

        // Finish drag
        state.finishDrag();
        REQUIRE_FALSE(state.isDragging);
    }

    SECTION("Cancel drag") {
        sf::Vector2f startPos(100.0f, 200.0f);

        state.startDrag(startPos);
        REQUIRE(state.isDragging);

        state.cancelDrag();
        REQUIRE_FALSE(state.isDragging);
    }
}

//==============================================================================
// SECTION: Selection Mode Tests
//==============================================================================

TEST_CASE("Selection mode enumeration", "[selection_modes]") {
    SECTION("All selection modes are defined") {
        // Test that all expected selection modes exist
        std::vector<SelectionMode> allModes = {
            SelectionMode::ALL,
            SelectionMode::FLOOR_TILES,
            SelectionMode::ROOF_TILES,
            SelectionMode::ROOF_TILES_ALL,
            SelectionMode::OBJECTS,
            SelectionMode::HEXES,
            SelectionMode::SCROLL_BLOCKER_RECTANGLE
        };

        // Verify string conversion works for all modes
        for (auto mode : allModes) {
            const std::string_view modeStr = selectionModeToString(mode);
            REQUIRE_FALSE(modeStr.empty());
            REQUIRE(modeStr != "Unknown");
        }
    }

    SECTION("Selection mode string representations") {
        CHECK(std::string_view(selectionModeToString(SelectionMode::ALL)) == "All");
        CHECK(std::string_view(selectionModeToString(SelectionMode::FLOOR_TILES)) == "Floor Tiles");
        CHECK(std::string_view(selectionModeToString(SelectionMode::ROOF_TILES)) == "Roof Tiles");
        CHECK(std::string_view(selectionModeToString(SelectionMode::ROOF_TILES_ALL)) == "Roof Tiles + Empty");
        CHECK(std::string_view(selectionModeToString(SelectionMode::OBJECTS)) == "Objects");
        CHECK(std::string_view(selectionModeToString(SelectionMode::HEXES)) == "Hexes");
        CHECK(std::string_view(selectionModeToString(SelectionMode::SCROLL_BLOCKER_RECTANGLE)) == "Scroll Blocker");
    }
}

//==============================================================================
// SECTION: SelectedItem Tests
//==============================================================================

TEST_CASE("SelectedItem functionality", "[selection_state]") {
    SECTION("Item type checking") {
        SelectedItem floorTile{ SelectionType::FLOOR_TILE, 100 };
        SelectedItem roofTile{ SelectionType::ROOF_TILE, 200 };
        SelectedItem hexItem{ SelectionType::HEX, 300 };

        // Type checking methods
        REQUIRE(floorTile.isTile());
        REQUIRE_FALSE(floorTile.isObject());
        REQUIRE_FALSE(floorTile.isHex());

        REQUIRE(roofTile.isTile());
        REQUIRE_FALSE(roofTile.isObject());
        REQUIRE_FALSE(roofTile.isHex());

        REQUIRE_FALSE(hexItem.isTile());
        REQUIRE_FALSE(hexItem.isObject());
        REQUIRE(hexItem.isHex());
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
