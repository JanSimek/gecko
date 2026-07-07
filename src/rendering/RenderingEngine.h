#pragma once

#include "HexRenderer.h"
#include "editor/HexagonGrid.h"
#include "util/Types.h"
#include <SFML/Graphics.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

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
        bool showSpatialScripts = false;
        bool showMapEdges = false;
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

        // SID (MapScript::pid) of the spatial script to highlight as selected in the
        // spatial-script overlay, or MapScript::NONE for no selection.
        uint32_t selectedSpatialScriptSid = 0xFFFFFFFFu;

        // Index (into the current elevation's zones) of the map-edge zone to highlight as selected
        // in the map-edge overlay, or -1 for no selection.
        int selectedEdgeZone = -1;
        // While dragging a zone side (0=left,1=top,2=right,3=bottom), that side of the selected zone
        // is drawn highlighted; -1 when not moving a side.
        int activeEdgeSide = -1;

        // Exit-grid "Draw edge" live preview (MarkExits mode), grouped so RenderData stays small. When
        // `active`: `lineVertices`/`lineCursor` draw the polyline (vertex->vertex, last->cursor);
        // `hexes` are the prospective on-line hexes; `frmPids` (parallel) each hex's directional marker
        // FRM; `tint` colours them by destination kind (green inter-map, brown world/town).
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
     * @brief Editor-only "Show light overlays" cue: tint every visible light-source object's illuminated
     * hexes using Fallout 2 CE's per-ring linear falloff (fallout2-ce `_obj_adjust_light`) — brightest on
     * the source hex, fading out to the light's radius. Honours the same per-layer visibility rule as
     * renderObjects, so a light on a hidden layer shows no overlay. Illustrative only: no wall shadowing
     * and no ambient darkness simulation, it just shows which hexes each light reaches and how strongly.
     */
    void renderLightOverlays(sf::RenderTarget& target,
        const sf::View& view,
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
     * @brief Editor-only "Show exit grids" overlay (Ctrl+E): a high-contrast marker on every exit-grid
     * hex, atop the player-visible directional art renderObjects already drew. Purely an editor cue,
     * deliberately separate from the real exitgrd / ext2grd sprites.
     */
    void renderExitGrids(sf::RenderTarget& target,
        const sf::View& view,
        const RenderData& renderData,
        const Map* map);

    /**
     * @brief Editor-only "Show spatial scripts" overlay: for each spatial script on the current
     * elevation, a translucent hex-distance radius disc plus the engine's green marker
     * (art/intrface/msef001.frm) at its centre hex. Spatial scripts have no MapObject, so this is
     * driven from the map's script list, not renderData.objects.
     */
    void renderSpatialScripts(sf::RenderTarget& target,
        const sf::View& view,
        const RenderData& renderData);

    /**
     * @brief Editor-only "Show map edges" overlay: the `.edg` scroll-boundary zones for the current
     * elevation (each drawn as a red world-space rectangle from its four hex corners), the selected
     * zone highlighted amber and a dragged side green, plus the v2 square clip rect with per-side
     * clip colours. Driven from the Map's loaded MapEdge sidecar, not renderData.objects.
     */
    void renderMapEdges(sf::RenderTarget& target,
        const sf::View& view,
        const RenderData& renderData);

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
