#pragma once

#include <SFML/Graphics.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>
#include "util/Types.h"
#include "editor/HexagonGrid.h"
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
        // Merge touching selected objects of the same category into one union outline (true), or
        // outline every selected object individually so shared edges show too (false).
        bool mergeSelectionOutlines = true;
    };

    /**
     * @brief User-configurable selection highlight colours (set from preferences).
     *
     * Object/wall/critter colour the object outline by category; tile colours the
     * floor/roof selection outline and its translucent fill. Defaults are deliberately
     * distinct hues so the categories — and tiles vs objects — read apart.
     */
    struct SelectionPalette {
        sf::Color object{ 140, 110, 220 }; // violet
        sf::Color wall{ 74, 206, 168 };    // teal
        sf::Color critter{ 224, 180, 96 }; // warm amber
        sf::Color tile{ 74, 144, 226 };    // blue accent
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
        const std::vector<int>* selectedHexPositions = nullptr;
        // Selected floor/roof tile indices, outlined as a union boundary (not a tint).
        const std::vector<int>* selectedFloorTiles = nullptr;
        const std::vector<int>* selectedRoofTiles = nullptr;

        // Drag preview
        const std::shared_ptr<Object>* dragPreviewObject = nullptr;
        bool isDraggingFromPalette = false;

        // Pattern stamp ghost preview (semi-transparent) under the cursor: floor tiles
        // (under), objects, roof tiles (over). Grouped so RenderData stays small.
        struct StampPreview {
            const std::vector<sf::Sprite>* floorTiles = nullptr;
            const std::vector<std::shared_ptr<Object>>* objects = nullptr;
            const std::vector<sf::Sprite>* roofTiles = nullptr;
        };
        StampPreview stampPreview;

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

        // Exit-grid "Draw edge" live preview (MarkExits mode), grouped so RenderData stays small.
        // When `active`, `lineVertices` holds the committed polyline vertices and `lineCursor` the
        // live cursor, so the line is drawn vertex->vertex and last->cursor. `hexes` lists the
        // prospective on-line hex indices; `frmPids` (parallel to `hexes`) is each hex's directional
        // marker FRM, drawn as a transient Object so the preview shows the right directional art per
        // hex; `tint` colours them by destination kind (green inter-map, brown world/town map).
        struct ExitGridPreview {
            const std::vector<sf::Vector2f>* lineVertices = nullptr;
            sf::Vector2f lineCursor;
            bool active = false;
            const std::vector<int>* hexes = nullptr;
            const std::vector<std::uint32_t>* frmPids = nullptr;
            sf::Color tint{ 80, 220, 80, 140 };
        };
        ExitGridPreview exitGridPreview;
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

    /** @brief Set the user-configured selection highlight colours. */
    void setSelectionColors(const SelectionPalette& colors) { _selectionColors = colors; }

private:
    /** @brief Selection outline colour for an object, by its category. */
    sf::Color objectOutlineColor(const Object& object) const;

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
     * @brief Outline every selected object on top of the scene, grouped by category colour.
     *
     * Renders each colour group's selected sprites into an offscreen mask, then edge-detects the
     * union silhouette so the artwork keeps its real colours and only gains a clean 1px coloured
     * border on every side — the way the Fallout engine outlines objects. Falls back to per-object
     * bounding boxes when shaders are unavailable on the current GL context.
     */
    void drawSelectedObjectOutlines(sf::RenderTarget& target,
        const RenderData& renderData,
        const VisibilitySettings& visibility);
    // Collect selected, visible objects grouped by their outline colour (keyed by RGBA integer).
    std::map<std::uint32_t, std::vector<const Object*>> collectSelectedOutlineGroups(
        const RenderData& renderData,
        const VisibilitySettings& visibility) const;
    // Shaderless fallback: a per-object bounding-box outline in each group's colour.
    void drawOutlineFallbackBoxes(sf::RenderTarget& target,
        const std::map<std::uint32_t, std::vector<const Object*>>& groups) const;
    // Render one batch of sprites into the offscreen mask and stroke its union silhouette in colour.
    void strokeOutlineGroup(sf::RenderTarget& target,
        const sf::View& sceneView,
        const sf::View& screenView,
        sf::Color color,
        const std::vector<const Object*>& objects);
    void ensureOutlineShader();

    /**
     * @brief Outline the outer boundary of a set of selected tiles.
     *
     * Each tile is a sheared parallelogram; an edge is drawn only where the tile across it is
     * not also selected, so a multi-tile selection reads as one clean union outline rather than
     * a per-cell grid.
     */
    void renderTileSelectionOutline(sf::RenderTarget& target, const std::vector<int>& selectedTiles, bool roof);

    /**
     * @brief Render roof tiles
     */
    void renderRoofTiles(sf::RenderTarget& target,
        const RenderData& renderData,
        bool showRoof);

    /**
     * @brief Render selection-related visuals. The roof tile selection outline is drawn only when
     * the roof layer is visible, so hiding the roof also hides its selection outline (the floor
     * outline always shows, since the floor layer is always drawn).
     */
    void renderSelectionVisuals(sf::RenderTarget& target,
        const RenderData& renderData,
        bool showRoof);

    /**
     * @brief Render hex highlights and markers
     */
    void renderHexHighlights(sf::RenderTarget& target,
        const RenderData& renderData);

    /**
     * @brief Editor-only "Show exit grids" overlay (Ctrl+E).
     *
     * Draws a distinct high-contrast marker on every exit-grid hex, on top of the player-visible
     * directional art that renderObjects already drew. This is purely an editor cue (so exit grids
     * are easy to spot) and is deliberately separate from the real exitgrd / ext2grd sprites - it
     * does not depend on any FRM (art/misc/exitgrid.frm does not exist).
     */
    void renderExitGrids(sf::RenderTarget& target,
        const sf::View& view,
        const RenderData& renderData,
        const Map* map);
    // Draw one editor-only exit-grid overlay marker (a magenta hex diamond) at a hex screen centre.
    static void drawExitGridOverlayMarker(sf::RenderTarget& target, int centerX, int centerY);

    /**
     * @brief Render the in-progress exit-grid "Draw edge" preview: the polyline plus each
     * prospective on-line hex marked with the (tinted) exit-grid marker sprite.
     */
    void renderExitGridEdgePreview(sf::RenderTarget& target,
        const sf::View& view,
        const RenderData& renderData);
    // Split out of renderExitGridEdgePreview to keep its complexity down.
    void drawExitGridPreviewMarkers(sf::RenderTarget& target, const sf::View& view, const RenderData& renderData);
    void drawExitGridPreviewLine(sf::RenderTarget& target, const RenderData& renderData);
    // Build the transient directional exit-grid marker Object for one prospective preview hex,
    // anchored exactly like a committed object (Object::setHexPosition / setDirection). Returns
    // null if the hex is off-grid or its directional FRM is unavailable.
    std::shared_ptr<Object> buildExitGridPreviewObject(const RenderData& renderData, int hexIndex,
        uint32_t frmPid) const;

    /**
     * @brief Check if a hex is within the visible viewport
     */
    bool isHexVisible(int hexWorldX, int hexWorldY,
        const sf::View& view) const;

    resource::GameResources& _resources;
    HexRenderer _hexRenderer;
    SelectionPalette _selectionColors;

    // Lazily-loaded silhouette outline shader for selected objects (needs a live GL context,
    // so it is loaded on first use during rendering rather than in the constructor).
    sf::Shader _outlineShader;
    bool _outlineShaderTried = false;
    bool _outlineShaderOk = false;

    // Offscreen screen-resolution mask the selected sprites are drawn into before edge detection,
    // resized to match the render target. Reused across frames to avoid per-frame allocation.
    sf::RenderTexture _outlineMask;
};

} // namespace geck
