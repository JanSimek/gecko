#include <catch2/catch_test_macros.hpp>

#include "util/TileUtils.h"
#include "util/Constants.h"
#include <cstdlib>

using namespace geck;

TEST_CASE("Tile coordinate conversion", "[tile_utils]") {
    SECTION("indexToCoordinates conversion") {
        // Test corner cases
        auto coords_0 = indexToCoordinates(0);
        REQUIRE(coords_0.x == 0);
        REQUIRE(coords_0.y == 0);

        auto coords_99 = indexToCoordinates(99);
        REQUIRE(coords_99.x == 0);
        REQUIRE(coords_99.y == 99);

        auto coords_100 = indexToCoordinates(100);
        REQUIRE(coords_100.x == 1);
        REQUIRE(coords_100.y == 0);

        auto coords_150 = indexToCoordinates(150);
        REQUIRE(coords_150.x == 1);
        REQUIRE(coords_150.y == 50);

        // Test middle of map
        auto coords_5050 = indexToCoordinates(5050);
        REQUIRE(coords_5050.x == 50);
        REQUIRE(coords_5050.y == 50);

        // Test last tile
        auto coords_last = indexToCoordinates(TILES_PER_ELEVATION - 1);
        REQUIRE(coords_last.x == MAP_HEIGHT - 1);
        REQUIRE(coords_last.y == MAP_WIDTH - 1);
    }

    SECTION("coordinatesToIndex conversion") {
        // Test corner cases
        REQUIRE(coordinatesToIndex(TileCoordinates(0, 0)) == 0);
        REQUIRE(coordinatesToIndex(TileCoordinates(0, 99)) == 99);
        REQUIRE(coordinatesToIndex(TileCoordinates(1, 0)) == 100);
        REQUIRE(coordinatesToIndex(TileCoordinates(1, 50)) == 150);

        // Test middle of map
        REQUIRE(coordinatesToIndex(TileCoordinates(50, 50)) == 5050);

        // Test last tile
        REQUIRE(coordinatesToIndex(TileCoordinates(MAP_HEIGHT - 1, MAP_WIDTH - 1)) == TILES_PER_ELEVATION - 1);
    }

    SECTION("Round-trip conversion consistency") {
        // Test that converting index->coords->index gives same result
        for (int index : { 0, 99, 100, 150, 5050, TILES_PER_ELEVATION - 1 }) {
            auto coords = indexToCoordinates(index);
            auto back_to_index = coordinatesToIndex(coords);
            REQUIRE(back_to_index == index);
        }

        // Test that converting coords->index->coords gives same result
        std::vector<TileCoordinates> test_coords = {
            TileCoordinates(0, 0),
            TileCoordinates(0, 99),
            TileCoordinates(1, 0),
            TileCoordinates(50, 50),
            TileCoordinates(MAP_HEIGHT - 1, MAP_WIDTH - 1)
        };

        for (const auto& coords : test_coords) {
            auto index = coordinatesToIndex(coords);
            auto back_to_coords = indexToCoordinates(index);
            REQUIRE(back_to_coords.x == coords.x);
            REQUIRE(back_to_coords.y == coords.y);
        }
    }
}

TEST_CASE("Screen position calculation", "[tile_utils]") {
    SECTION("coordinatesToScreenPosition - floor tiles") {
        // Test origin (0,0) - bottom-right corner of map in isometric view
        auto screen_00 = coordinatesToScreenPosition(TileCoordinates(0, 0), false);
        unsigned int expected_x_00 = (MAP_WIDTH - 0 - 1) * TILE_X_OFFSET + TILE_Y_OFFSET_LARGE * 0;
        unsigned int expected_y_00 = 0 * TILE_Y_OFFSET_SMALL + 0 * TILE_Y_OFFSET_TINY;
        REQUIRE(screen_00.x == expected_x_00);
        REQUIRE(screen_00.y == expected_y_00);

        // Test (1,1)
        auto screen_11 = coordinatesToScreenPosition(TileCoordinates(1, 1), false);
        unsigned int expected_x_11 = (MAP_WIDTH - 1 - 1) * TILE_X_OFFSET + TILE_Y_OFFSET_LARGE * 1;
        unsigned int expected_y_11 = 1 * TILE_Y_OFFSET_SMALL + 1 * TILE_Y_OFFSET_TINY;
        REQUIRE(screen_11.x == expected_x_11);
        REQUIRE(screen_11.y == expected_y_11);

        // Test corner case (99, 99) - top-left corner of map in isometric view
        auto screen_9999 = coordinatesToScreenPosition(TileCoordinates(99, 99), false);
        unsigned int expected_x_9999 = (MAP_WIDTH - 99 - 1) * TILE_X_OFFSET + TILE_Y_OFFSET_LARGE * 99;
        unsigned int expected_y_9999 = 99 * TILE_Y_OFFSET_SMALL + 99 * TILE_Y_OFFSET_TINY;
        REQUIRE(screen_9999.x == expected_x_9999);
        REQUIRE(screen_9999.y == expected_y_9999);
    }

    SECTION("coordinatesToScreenPosition - roof tiles") {
        // Test that roof tiles have roof offset applied
        auto screen_floor = coordinatesToScreenPosition(TileCoordinates(10, 10), false);
        auto screen_roof = coordinatesToScreenPosition(TileCoordinates(10, 10), true);

        REQUIRE(screen_roof.x == screen_floor.x);               // X should be same
        REQUIRE(screen_roof.y == screen_floor.y - ROOF_OFFSET); // Y should be offset by roof height
    }

    SECTION("indexToScreenPosition convenience function") {
        // Test that convenience function gives same result as step-by-step conversion
        int test_index = 1550; // Arbitrary test index

        auto coords = indexToCoordinates(test_index);
        auto screen_step_by_step = coordinatesToScreenPosition(coords, false);
        auto screen_direct = indexToScreenPosition(test_index, false);

        REQUIRE(screen_direct.x == screen_step_by_step.x);
        REQUIRE(screen_direct.y == screen_step_by_step.y);

        // Test with roof tiles
        auto screen_step_by_step_roof = coordinatesToScreenPosition(coords, true);
        auto screen_direct_roof = indexToScreenPosition(test_index, true);

        REQUIRE(screen_direct_roof.x == screen_step_by_step_roof.x);
        REQUIRE(screen_direct_roof.y == screen_step_by_step_roof.y);
    }
}

TEST_CASE("Color utilities", "[tile_utils]") {
    SECTION("Preview colors") {
        auto preview_fill = TileColors::previewFill();
        REQUIRE(preview_fill.r == Colors::PREVIEW_R);
        REQUIRE(preview_fill.g == Colors::PREVIEW_G);
        REQUIRE(preview_fill.b == Colors::PREVIEW_B);
        REQUIRE(preview_fill.a == Colors::PREVIEW_ALPHA);

        auto preview_outline = TileColors::previewOutline();
        REQUIRE(preview_outline.r == Colors::PREVIEW_R);
        REQUIRE(preview_outline.g == Colors::PREVIEW_G);
        REQUIRE(preview_outline.b == Colors::PREVIEW_B);
        REQUIRE(preview_outline.a == Colors::PREVIEW_OUTLINE_ALPHA);
    }

    SECTION("Error colors") {
        auto error_fill = TileColors::errorFill();
        REQUIRE(error_fill.r == Colors::ERROR_R);
        REQUIRE(error_fill.g == Colors::ERROR_G);
        REQUIRE(error_fill.b == Colors::ERROR_B);
        REQUIRE(error_fill.a == Colors::ERROR_ALPHA);

        auto error_outline = TileColors::errorOutline();
        REQUIRE(error_outline.r == Colors::ERROR_R);
        REQUIRE(error_outline.g == Colors::ERROR_G);
        REQUIRE(error_outline.b == Colors::ERROR_B);
        REQUIRE(error_outline.a == Colors::ERROR_OUTLINE_ALPHA);
    }

    SECTION("Selection colors") {
        auto selection_fill = TileColors::selectionFill();
        REQUIRE(selection_fill.r == Colors::SELECTION_RECT_R);
        REQUIRE(selection_fill.g == Colors::SELECTION_RECT_G);
        REQUIRE(selection_fill.b == Colors::SELECTION_RECT_B);
        REQUIRE(selection_fill.a == Colors::SELECTION_RECT_FILL_ALPHA);

        auto selection_outline = TileColors::selectionOutline();
        REQUIRE(selection_outline.r == Colors::SELECTION_RECT_R);
        REQUIRE(selection_outline.g == Colors::SELECTION_RECT_G);
        REQUIRE(selection_outline.b == Colors::SELECTION_RECT_B);
        REQUIRE(selection_outline.a == Colors::SELECTION_RECT_OUTLINE_ALPHA);
    }

    SECTION("White color") {
        auto white = TileColors::white();
        REQUIRE(white == sf::Color::White);
    }

    SECTION("Transparent color") {
        auto transparent = TileColors::transparent();
        REQUIRE(transparent.r == 255);
        REQUIRE(transparent.g == 255);
        REQUIRE(transparent.b == 255);
        REQUIRE(transparent.a == 0);
    }
}

TEST_CASE("Sprite highlight functions", "[tile_utils]") {
    // Skip graphics tests in CI/headless environments
    if (std::getenv("CI") != nullptr || std::getenv("GITHUB_ACTIONS") != nullptr) {
        SKIP("Graphics tests skipped in CI environment");
    }
    
    SECTION("Apply and remove preview highlight") {
        // Create a minimal 1x1 texture for testing
        sf::Texture texture;
        sf::Image image{ sf::Vector2u{ 1, 1 }, sf::Color::White };
        if (!texture.loadFromImage(image)) {
            FAIL("Failed to create test texture");
        }

        sf::Sprite sprite(texture);

        // Initially white
        sprite.setColor(sf::Color::White);
        REQUIRE(sprite.getColor() == sf::Color::White);

        // Apply preview highlight
        applyPreviewHighlight(sprite);
        auto expected_preview = TileColors::previewFill();
        REQUIRE(sprite.getColor() == expected_preview);

        // Remove preview highlight
        removePreviewHighlight(sprite);
        REQUIRE(sprite.getColor() == sf::Color::White);
    }
}

TEST_CASE("Mathematical property validation", "[tile_utils]") {
    SECTION("Ensure coordinates stay within bounds") {
        // Test that all valid tile indices produce valid coordinates
        for (int index = 0; index < TILES_PER_ELEVATION; ++index) {
            auto coords = indexToCoordinates(index);
            REQUIRE(coords.x < MAP_HEIGHT);
            REQUIRE(coords.y < MAP_WIDTH);
        }
    }

    SECTION("Isometric projection consistency") {
        // Test that coordinates that differ by 1 in x have predictable screen position differences
        auto screen_00 = coordinatesToScreenPosition(TileCoordinates(0, 0));
        auto screen_10 = coordinatesToScreenPosition(TileCoordinates(1, 0));

        // Moving one row down should add TILE_Y_OFFSET_LARGE to x and TILE_Y_OFFSET_SMALL to y
        REQUIRE(screen_10.x == screen_00.x + TILE_Y_OFFSET_LARGE);
        REQUIRE(screen_10.y == screen_00.y + TILE_Y_OFFSET_SMALL);

        // Test that coordinates that differ by 1 in y have predictable differences
        auto screen_01 = coordinatesToScreenPosition(TileCoordinates(0, 1));

        // Moving one column right should subtract TILE_X_OFFSET from x and add TILE_Y_OFFSET_TINY to y
        REQUIRE(screen_01.x == screen_00.x - TILE_X_OFFSET);
        REQUIRE(screen_01.y == screen_00.y + TILE_Y_OFFSET_TINY);
    }
}