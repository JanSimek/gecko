#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <vector>
#include "../../util/Types.h"
#include "../../editor/HexagonGrid.h"
#include "HexRenderer.h"

namespace geck {

namespace resource {
    class GameResources;
}

// Forward declarations
class Object;
class Map;

/**
 * @brief Handles all rendering operations for the editor
 *
 * This class encapsulates all rendering logic that was previously
 * in EditorWidget, providing a clean separation of concerns between
 * UI management and rendering operations.
 */
class RenderingEngine {
public:
    /**
     * @brief Visibility settings for different render layers
     */
    struct VisibilitySettings {
        bool showObjects = true;
        bool showCritters = true;
        bool showWalls = true;
        bool showRoof = true;
        bool showScrollBlockers = true;
        bool showWallBlockers = true;
        bool showHexGrid = false;
        bool showLightOverlays = false;
        bool showExitGrids = false;
    };

    /**
     * @brief Data needed for rendering operations
     */
    struct RenderData {
        // Sprites
        const std::vector<sf::Sprite>* floorSprites = nullptr;
        const std::vector<sf::Sprite>* roofSprites = nullptr;
        const std::vector<std::shared_ptr<Object>>* objects = nullptr;
        const std::vector<sf::Sprite>* wallBlockerOverlays = nullptr;
        const std::vector<sf::Sprite>* selectedRoofTileBackgroundSprites = nullptr;
        const std::vector<int>* selectedHexPositions = nullptr;

        // Drag preview
        const std::shared_ptr<Object>* dragPreviewObject = nullptr;
        bool isDraggingFromPalette = false;

        // Selection rectangle
        const sf::RectangleShape* selectionRectangle = nullptr;
        bool isDragSelecting = false;
        SelectionMode currentSelectionMode = SelectionMode::ALL;

        // Hex grid data
        const HexagonGrid* hexGrid = nullptr;
        int currentHoverHex = -1;
        int playerPositionHex = -1;

        // Map data
        const Map* map = nullptr;
        int currentElevation = 0;
    };

    explicit RenderingEngine(resource::GameResources& resources);
    ~RenderingEngine() = default;

    /**
     * @brief Main render method - draws all visible elements
     * @param window The SFML render window
     * @param view The current camera view
     * @param renderData All data needed for rendering
     * @param visibility Current visibility settings
     */
    void render(sf::RenderTarget& target,
        const sf::View& view,
        const RenderData& renderData,
        const VisibilitySettings& visibility);

    /**
     * @brief Set colors for selection rectangle based on mode
     * @param rectangle The selection rectangle to update
     * @param selectionMode Current selection mode
     */
    static void applySelectionRectangleColors(sf::RectangleShape& rectangle,
        SelectionMode selectionMode);

private:
    /**
     * @brief Render floor tile sprites
     */
    void renderFloorTiles(sf::RenderTarget& target,
        const std::vector<sf::Sprite>& floorSprites);

    /**
     * @brief Render the hex grid overlay
     */
    void renderHexGrid(sf::RenderTarget& target,
        const sf::View& view,
        const RenderData& renderData);

    /**
     * @brief Render all objects with visibility filtering
     */
    void renderObjects(sf::RenderTarget& target,
        const RenderData& renderData,
        const VisibilitySettings& visibility);

    /**
     * @brief Render roof tiles and their selection backgrounds
     */
    void renderRoofTiles(sf::RenderTarget& target,
        const RenderData& renderData,
        bool showRoof);

    /**
     * @brief Render selection-related visuals
     */
    void renderSelectionVisuals(sf::RenderTarget& target,
        const RenderData& renderData);

    /**
     * @brief Render hex highlights and markers
     */
    void renderHexHighlights(sf::RenderTarget& target,
        const RenderData& renderData);

    /**
     * @brief Render exit grid markers
     */
    void renderExitGrids(sf::RenderTarget& target,
        const sf::View& view,
        const RenderData& renderData,
        const Map* map);

    /**
     * @brief Helper method to render exit grids with a loaded sprite
     */
    void renderExitGridsWithSprite(sf::RenderTarget& target,
        const sf::View& view,
        const RenderData& renderData,
        const Map* map,
        sf::Sprite& exitGridSprite);

    /**
     * @brief Check if a hex is within the visible viewport
     */
    bool isHexVisible(int hexWorldX, int hexWorldY,
        const sf::View& view) const;

    resource::GameResources& _resources;
    HexRenderer _hexRenderer;
};

} // namespace geck
