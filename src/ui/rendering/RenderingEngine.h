#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <vector>
#include "../../util/Types.h"
#include "../../editor/HexagonGrid.h"

namespace geck {

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
        const std::vector<sf::Sprite>* selectedHexSprites = nullptr;
        
        // Drag preview
        const std::shared_ptr<Object>* dragPreviewObject = nullptr;
        bool isDraggingFromPalette = false;
        
        // Selection rectangle
        const sf::RectangleShape* selectionRectangle = nullptr;
        bool isDragSelecting = false;
        SelectionMode currentSelectionMode = SelectionMode::ALL;
        
        // Hex grid data
        const HexagonGrid* hexGrid = nullptr;
        const sf::Sprite* hexSprite = nullptr;
        const sf::Sprite* hexHighlightSprite = nullptr;
        const sf::Sprite* playerPositionSprite = nullptr;
        int currentHoverHex = -1;
        
        // Map data
        const Map* map = nullptr;
    };

    explicit RenderingEngine();
    ~RenderingEngine() = default;

    /**
     * @brief Main render method - draws all visible elements
     * @param window The SFML render window
     * @param view The current camera view
     * @param renderData All data needed for rendering
     * @param visibility Current visibility settings
     */
    void render(sf::RenderWindow* window, 
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
    void renderFloorTiles(sf::RenderWindow* window, 
                         const std::vector<sf::Sprite>& floorSprites);

    /**
     * @brief Render the hex grid overlay
     */
    void renderHexGrid(sf::RenderWindow* window,
                      const sf::View& view,
                      const RenderData& renderData);

    /**
     * @brief Render all objects with visibility filtering
     */
    void renderObjects(sf::RenderWindow* window,
                      const RenderData& renderData,
                      const VisibilitySettings& visibility);

    /**
     * @brief Render roof tiles and their selection backgrounds
     */
    void renderRoofTiles(sf::RenderWindow* window,
                        const RenderData& renderData,
                        bool showRoof);

    /**
     * @brief Render selection-related visuals
     */
    void renderSelectionVisuals(sf::RenderWindow* window,
                               const RenderData& renderData);

    /**
     * @brief Render hex highlights and markers
     */
    void renderHexHighlights(sf::RenderWindow* window,
                            const RenderData& renderData);

    /**
     * @brief Check if a hex is within the visible viewport
     */
    bool isHexVisible(int hexWorldX, int hexWorldY, 
                     const sf::View& view) const;
};

} // namespace geck