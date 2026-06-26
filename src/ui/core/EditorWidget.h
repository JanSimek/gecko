#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <filesystem>
#include <thread>
#include <optional>
#include <array>
#include <vector>
#include <utility>
#include <unordered_map>
#include <functional>

#include <QWidget>
#include <QVBoxLayout>
#include "editor/Object.h"
#include "editor/HexagonGrid.h"
#include "util/Types.h"
#include "format/map/Map.h"
#include "format/pro/Pro.h"
#include "selection/SelectionManager.h"
#include "selection/SelectionDataProvider.h"
#include "util/Constants.h"
#include "util/UndoStack.h"
#include "rendering/RenderingEngine.h"
#include "ui/tiles/TilePlacementContext.h"
#include "ui/input/InputHandler.h"
#include "ui/core/EditorMode.h"
#include "ui/core/EditorController.h"
#include "ui/core/EditorSession.h"
#include "pattern/Pattern.h"
#include "ui/tools/ExitGridContext.h"
#include "ui/dragdrop/DragDropContext.h"
#include "editor/TileChange.h"
#include "VisibilitySettings.h"

#ifdef GECK_SCRIPTING_ENABLED
#include "scripting/LuaScriptRuntime.h" // ScriptResult
#endif

namespace geck {

namespace resource {
    class GameResources;
}

// Forward declarations
class RenderingEngine;
class DragDropManager;
class TilePlacementManager;
class ExitGridPlacementManager;
class ViewportController;
class SFMLWidget;
struct ObjectInfo;

class EditorWidget : public QWidget, public selection::SelectionDataProvider, public TilePlacementContext, public ExitGridContext, public DragDropContext {
    Q_OBJECT

    friend class TilePlacementManager;

public:
    EditorWidget(resource::GameResources& resources, std::unique_ptr<Map> map, QWidget* parent = nullptr);
    ~EditorWidget();

    void createNewMap();
    void openMap();
    /// Save the current map. If it already points at a real file (saved before, or opened from one), it
    /// writes straight there with no dialog; otherwise it behaves like Save As, defaulting the dialog to
    /// `defaultDir` (the writable data path's maps/ folder). Returns true on a successful write, false if
    /// the user cancelled or the write failed.
    bool saveMap(const std::filesystem::path& defaultDir = {});
    /// Whether "Save" may write straight to `mapPath` rather than prompting: true only for a real,
    /// existing on-disk file. A VFS path from the game data (e.g. "/maps/arbridge.map") looks absolute
    /// but is not a writable location, and a new map has no file yet — both return false. Static so it is
    /// unit-testable without a live widget.
    static bool canSaveInPlace(const std::filesystem::path& mapPath);
    /// Always prompt for a destination (defaulting to `defaultDir`/<name>), then write and repoint the
    /// map at the chosen file. Returns true on a successful write.
    bool saveMapAs(const std::filesystem::path& defaultDir = {});

#ifdef GECK_SCRIPTING_ENABLED
    // Runs a Luau generation script against the current map at the current elevation. The whole run
    // is one undo entry; returns the script's print() output plus any compile/runtime error.
    ScriptResult runScript(const std::string& source);
#endif

    // Qt6 menu integration - visibility controls
    void setShowObjects(bool show) { _session.visibility().showObjects = show; }
    void setShowCritters(bool show) { _session.visibility().showCritters = show; }
    void setShowWalls(bool show) { _session.visibility().showWalls = show; }
    void setShowRoof(bool show) { _session.visibility().showRoof = show; }
    void setShowScrollBlk(bool show) { _session.visibility().showScrollBlockers = show; }
    void setShowWallBlockers(bool show) { _session.visibility().showWallBlockers = show; }
    void setShowHexGrid(bool show) { _session.visibility().showHexGrid = show; }
    void setShowLightOverlays(bool show);
    void setShowExitGrids(bool show) { _session.visibility().showExitGrids = show; }
    void setMergeSelectionOutlines(bool merge) { _session.visibility().mergeSelectionOutlines = merge; }

    // User-configured selection highlight colours (from preferences); forwarded to the renderer.
    void setSelectionColors(const RenderingEngine::SelectionPalette& colors);

    Map* getMap() const override { return _session.map(); }

    // Qt6 toolbar actions
    void cycleSelectionMode();
    void setSelectionMode(SelectionMode mode);
    SelectionMode getCurrentSelectionMode() const { return _currentSelectionMode; }
    // Combinable-layer selection: pick which layers a mixed (ALL-mode) selection considers
    // (floor / roof / objects). Switches to ALL mode and keeps the current selection intact.
    void setActiveSelectionLayers(SelectionLayers layers);
    SelectionLayers getActiveSelectionLayers() const;
    void rotateSelectedObject();
    void changeElevation(int elevation);
    void toggleScrollBlockerRectangleMode();

    // Player position selection
    void enterPlayerPositionSelectionMode();
    void centerViewOnPlayerPosition();

    // Find the object that owns the script with the given SID (its `map_scripts_pid`), switch to its
    // elevation, select it and center the view on it. Returns false (leaving the current selection
    // untouched) when no object on the map owns that script. Used by the Scripts panel's double-click.
    bool revealScriptObject(int sid);

    // Tile placement
    void placeTileAtPosition(int tileIndex, sf::Vector2f worldPos, bool isRoof);
    void fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof);
    void replaceSelectedTiles(int newTileIndex);

    // Object placement functionality
    void placeObjectAtPosition(sf::Vector2f worldPos) override;

    // Palette drag preview
    void startDragPreview(int objectIndex, int categoryInt, sf::Vector2f worldPos);
    void updateDragPreview(sf::Vector2f worldPos);
    void finishDragPreview(sf::Vector2f worldPos);
    void cancelDragPreview();

    // Efficient tile update
    void updateTileSprite(int hexIndex, bool isRoof) override;
    void updateTileSprite(int hexIndex, bool isRoof, int elevation) override;

    // Single tool-mode entry point: activates `mode` and exits all others.
    void setMode(EditorMode mode, int tileIndex = -1, bool isRoof = false);
    EditorMode currentMode() const { return _mode; }

    // Tile placement mode control
    void setTilePlacementMode(bool enabled, int tileIndex = -1, bool isRoof = false);
    void setTilePlacementAreaFill(bool enabled);
    void setTilePlacementReplaceMode(bool enabled);

    // Exit grid placement mode control
    void setExitGridPlacementMode(bool enabled);
    void setMarkExitsMode(bool enabled);

    // Pattern stamping: enter stamp mode with a loaded prefab; clicks place the current
    // variant, and cycleStampVariant() advances which orientation is placed.
    void beginStampPattern(pattern::Pattern pattern);
    void cycleStampVariant();

    // Object refresh methods
    void refreshObjects() override;
    void selectAll();
    void clearSelection() override;
    bool isTilePlacementMode() const;

    // SFML rendering interface (called by SFMLWidget)
    void handleEvent(const sf::Event& event);
    void update(const float dt);
    void render(sf::RenderTarget& target, float dt);
    void init();

    // Access to SFML widget for main window
    SFMLWidget* getSFMLWidget() const override { return _sfmlWidget; }

    // Set main window reference for accessing palette panels
    void setMainWindow(class MainWindow* mainWindow) { _mainWindow = mainWindow; }
    class MainWindow* getMainWindow() const { return _mainWindow; }

    // Access to internal components for extracted managers
    selection::SelectionManager* getSelectionManager() const override { return _session.selectionManager(); }
    TilePlacementManager* getTilePlacementManager() const { return _tilePlacementManager.get(); }
    ExitGridPlacementManager* getExitGridPlacementManager() const { return _exitGridPlacementManager.get(); }
    ViewportController* getViewportController() const override { return &_controller.viewport(); }
    int& getCurrentHoverHex() override { return _currentHoverHex; }
    void registerObjectMove(const std::vector<std::shared_ptr<Object>>& objects, const std::vector<std::pair<int, int>>& moves) override;
    void moveSelectedTilesForDrag(sf::Vector2f worldTranslation) override;
    void reselectAfterDragMove(sf::Vector2f worldTranslation) override;
    void beginMoveBatch(const std::string& description) override;
    void endMoveBatch() override;
    void beginTileDragPreview() override;
    void previewTileDrag(sf::Vector2f worldOffset) override;
    void endTileDragPreview() override;

    // SelectionManager helpers
    std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f worldPos) override;
    bool isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite);

    // Tile hit testing methods
    std::optional<int> getTileAtPosition(sf::Vector2f worldPos, bool isRoof) override;
    std::optional<int> getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos) override;

    // Access to sprite vectors for SelectionManager
    const std::vector<sf::Sprite>& getFloorSprites() const override { return _session.floorSprites(); }
    const std::vector<sf::Sprite>& getRoofSprites() const override { return _session.roofSprites(); }

    bool isRoofVisible() const override { return _session.visibility().showRoof; }
    bool isObjectSelectable(const std::shared_ptr<Object>& object) const override;

    // Access to current elevation and map data
    int getCurrentElevation() const override { return _session.currentElevation(); }
    Map::MapFile& getMapFile() override { return _session.map()->getMapFile(); }
    const Map::MapFile& getMapFile() const override { return _session.map()->getMapFile(); }

    // Access to objects for SelectionManager
    const std::vector<std::shared_ptr<Object>>& getObjects() const override { return _session.objects(); }

    // Access to hex grid for SelectionManager
    const HexagonGrid* getHexagonGrid() const override { return &_session.hexgrid(); }
    resource::GameResources& resources() const override { return _resources; }

    // Helper methods for extracted managers (made public)
    void clearDragSelectionPreview() override;

    // Parent widget for modal dialogs raised by extracted managers.
    QWidget* getDialogParent() override { return this; }

    // Ensure tile storage exists for an elevation
    std::vector<Tile>& ensureElevationTiles(int elevation) override;
    void registerObjectRotation(const std::vector<std::shared_ptr<Object>>& objects, const std::vector<int>& beforeDirs, const std::vector<int>& afterDirs);
    void registerObjectFrmChange(const std::shared_ptr<Object>& object, uint32_t oldFrmPid, const std::string& oldFrmPath, uint32_t newFrmPid, const std::string& newFrmPath);
    void applyFrmToObject(const std::shared_ptr<Object>& object, uint32_t frmPid, const std::string& frmPath);

    // Exit grid undo support
    using ExitGridState = ExitGridCommandState;
    void registerExitGridCreation(const std::vector<std::shared_ptr<MapObject>>& exitGrids, int elevation) override;
    void registerExitGridEdit(const std::vector<std::shared_ptr<MapObject>>& exitGrids,
        const std::vector<ExitGridState>& beforeStates,
        const std::vector<ExitGridState>& afterStates) override;

    // Per-instance property (flags / light / scenery destination / critter) undo
    // support. The before/after snapshots come from SelectionPanel's editors.
    void registerInstanceEdit(const std::shared_ptr<MapObject>& mapObject,
        const MapObjectInstanceState& before,
        const MapObjectInstanceState& after,
        const std::string& description);

    // Map-wide operations (undoable). The confirmation dialog lives in MapInfoPanel.
    void clearElevationObjects(int elevation);
    void copyElevation(int fromElevation, int toElevation);

    // Inventory edit (undoable). Snapshots come from SelectionPanel.
    void registerInventoryEdit(const std::shared_ptr<MapObject>& container,
        std::vector<std::shared_ptr<MapObject>> before,
        std::vector<std::shared_ptr<MapObject>> after);

    // Script attachment / spatial scripts (undoable).
    void attachScript(const std::shared_ptr<MapObject>& object, int scriptType, uint32_t programIndex);
    void detachScript(const std::shared_ptr<MapObject>& object);
    void addSpatialScript(uint32_t programIndex, int tile, int elevation, int radius);

signals:
    void selectionChanged(const selection::SelectionState& selection, int elevation);
    void mapLoadRequested(const std::string& mapPath);
    void hexHoverChanged(int hexIndex);
    void playerPositionSelected(int hexPosition);
    void statusMessageRequested(const QString& message);
    void statusMessageClearRequested();
    void undoStackChanged();
    /// A Console script changed the map through a path that records no undo command (e.g.
    /// setPlayerStart/newMap), so the host must flag the map modified explicitly.
    void mapModifiedByScript();
    void editorModeChanged(EditorMode mode);

public slots:
    void onObjectFrmChanged(std::shared_ptr<Object> object, uint32_t newFrmPid);
    void onObjectFrmPathChanged(std::shared_ptr<Object> object, const std::string& newFrmPath);

    // Sprite loading methods (public for MainWindow access)
    void loadTileSprites();

    // Undo/redo
    bool undoLastEdit();
    bool redoLastEdit();
    const UndoStack& getUndoStack() const { return _session.undoStack(); }

private:
    // Center the view on a hex position (shared by centerViewOnPlayerPosition and revealScriptObject).
    void centerViewOnHex(uint32_t hexPosition);
    // The elevation whose objects own the script with this SID (map_scripts_pid == sid), or -1 if none.
    int scriptOwnerElevation(int sid) const;
    // The current elevation's visual Object owning the script with this SID, or nullptr.
    std::shared_ptr<Object> visualObjectForSid(int sid) const;

    // Write the current map to `destination` (create-or-overwrite) and repoint the map at it; shared by
    // saveMap (direct write) and saveMapAs (after the dialog). Reports errors and returns success.
    bool writeMapTo(const std::string& destination);

    // One item's new selection entry after a drag-move: objects re-pointed to their refreshed
    // wrapper (by MapObject identity), tiles shifted by the whole-tile delta; nullopt to drop it.
    std::optional<selection::SelectedItem> remapSelectedItemAfterMove(
        const selection::SelectedItem& item,
        const std::unordered_map<const MapObject*, std::shared_ptr<Object>>& objectsByMapObject,
        const std::optional<std::pair<int, int>>& tileDelta) const;

    // Object management
    void deleteSelectedObjects();
    void registerTileEdit(const QString& description, const std::vector<TileChange>& changes) override;
    void applyTileChanges(const std::vector<TileChange>& changes, bool applyAfterState);
    void registerObjectPlacement(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);
    void removePlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);
    void addPlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object);

    void setupUI();
    void initializeSelectionSystem();

    void loadSprites();
    void showLoadingErrorsSummary();

    // Object selection methods (moved to public)
    bool isDoubleClick(sf::Vector2f worldPos);

    // Selection modifiers for multi-selection
    enum class SelectionModifier {
        NONE,   // Normal single selection (clear and select)
        ADD,    // Alt+Click / Alt+Drag - add to selection
        TOGGLE, // Ctrl+Click / Ctrl+Drag - remove from selection
        RANGE   // Shift+Click - range selection for tiles
    };

    bool selectAtPosition(sf::Vector2f worldPos);
    bool selectAtPosition(sf::Vector2f worldPos, SelectionModifier modifier);
    selection::SelectionResult handleRangeSelection(sf::Vector2f worldPos);

    void clearDragPreview();
    // isDeselect (Ctrl+drag): the covered selected items un-highlight live (preview of removal).
    // isAdditive (Alt+drag): keep the existing selection highlighted while the covered area is
    // tinted as an add preview. Plain drag (both false) tints the covered area as a replace.
    void updateDragSelectionPreview(sf::Vector2f startWorldPos, sf::Vector2f currentWorldPos, bool isDeselect, bool isAdditive);
    // Add-preview helpers: tint the covered tiles/objects and record them for clearDragPreview.
    void previewAreaTiles(const sf::FloatRect& area, bool roof, bool includeEmpty);
    void previewAreaObjects(const sf::FloatRect& area);
    // Exit-grid "Draw region" live preview: recompute the prospective interior hexes (hex centers
    // inside the polygon formed by the committed vertices + the live cursor) and the tint from the
    // tool's current destination kind, for the renderer to draw the outline and marked hexes.
    void updateMarkExitsPolygonPreview(const std::vector<sf::Vector2f>& vertices, sf::Vector2f cursor);
    void clearMarkExitsPolygonPreview();
    void updateTileAreaFillPreview(sf::Vector2f startWorldPos, sf::Vector2f currentWorldPos);
    // Commit a finished drag rectangle to the selection (replace/deselect/additive), or build
    // scroll blockers when in that mode. Extracted from the onDragSelection callback.
    void commitDragAreaSelection(sf::Vector2f startPos, sf::Vector2f endPos, bool isDeselect, bool isAdditive);

    // Scroll blocker rectangle functionality
    std::vector<int> calculateRectangleBorderHexes(sf::FloatRect rectangle);
    std::shared_ptr<MapObject> createScrollBlockerObject(int hexPosition);
    void createScrollBlockersFromHexes(const std::vector<int>& borderHexes);

    // Input system setup. setupInputCallbacks() builds the callback struct from these
    // cohesive groups; each populates its slice (lambdas capture this and drive the
    // relevant managers/selection/mode logic).
    void setupInputCallbacks();
    void bindSelectionCallbacks(InputHandler::Callbacks& callbacks);
    void bindInteractionCallbacks(InputHandler::Callbacks& callbacks);
    void bindToolModeCallbacks(InputHandler::Callbacks& callbacks);

    // UI Components
    QVBoxLayout* _layout;
    SFMLWidget* _sfmlWidget;
    class MainWindow* _mainWindow;

    // Owns the editor's per-map state and the Qt-free helpers that act on it (object
    // picking, selection visuals). Declared before the managers below so it outlives
    // them: several hold references into the session (e.g. the undo stack).
    EditorController _controller;
    // Reference into the controller's session so the many _session.x() call sites stay
    // unchanged while the controller owns the storage.
    EditorSession& _session{ _controller.session() };

    // Input + drag/drop + tile/exit-grid placement systems (the Qt-coupled managers).
    std::unique_ptr<InputHandler> _inputHandler;
    // RenderingEngine, MapSpriteLoader, ObjectCommandController and ViewportController now live
    // in _controller (the Qt-free editor core).
    std::unique_ptr<DragDropManager> _dragDropManager;
    std::unique_ptr<TilePlacementManager> _tilePlacementManager;
    std::unique_ptr<ExitGridPlacementManager> _exitGridPlacementManager;
    // ViewportController now lives in _controller.

    // Game/Editor State
    SelectionMode _currentSelectionMode = SelectionMode::ALL;

    resource::GameResources& _resources;

    // Double-click detection for object cycling
    sf::Clock _lastClickTime;
    sf::Vector2f _lastClickPosition;
    static constexpr float DOUBLE_CLICK_TIME = 0.5f;      // 500ms
    static constexpr float DOUBLE_CLICK_DISTANCE = 10.0f; // pixels

    // Drag selection state
    sf::RectangleShape _selectionRectangle;
    std::vector<int> _previewTiles;
    std::vector<std::shared_ptr<Object>> _previewObjects;

    // Drag preview state (for palette drag and drop)
    bool _isDraggingFromPalette = false;
    std::shared_ptr<Object> _dragPreviewObject;     // Preview object being dragged from palette
    int _previewObjectIndex = -1;                   // Index of object in palette
    int _previewObjectCategory = 0;                 // Category of preview object (as int)
    const ObjectInfo* _previewObjectInfo = nullptr; // Object info from palette

    int _currentHoverHex = -1;

    // Base positions of the selected floor/roof sprites captured while a region is being dragged, so
    // the live preview can offset them and restore them when the drag ends.
    struct TileDragPreviewBase {
        bool roof;
        int tileIndex;
        sf::Vector2f basePosition;
    };
    std::vector<TileDragPreviewBase> _tileDragPreviewBases;

    // Active tool mode (single source of truth; see setMode).
    EditorMode _mode = EditorMode::Select;

    // Loaded prefab being stamped (StampPattern mode) and the active variant index.
    std::optional<pattern::Pattern> _stampPattern;
    int _stampVariantIndex = 0;
    void stampPatternAt(sf::Vector2f worldPos);

    // Semi-transparent ghost of the current variant under the cursor (StampPattern mode):
    // floor tiles (drawn under), objects, and roof tiles (drawn over).
    std::vector<sf::Sprite> _stampPreviewFloorTiles;
    std::vector<std::shared_ptr<Object>> _stampPreviewObjects;
    std::vector<sf::Sprite> _stampPreviewRoofTiles;
    int _stampPreviewHex = -1;
    void updateStampPreview(sf::Vector2f worldPos);
    void clearStampPreview();

    // Player position selection state
    bool _playerPositionSelectionMode = false;

    // Exit-grid "Draw region" preview state (MarkExits mode). _exitGridPolygonActive gates the
    // renderer; the vertices/cursor draw the outline; the hexes are the prospective interior hexes
    // (recomputed each mouse move); the tint reflects the tool's current destination kind.
    std::vector<sf::Vector2f> _exitGridPolygonVertices;
    sf::Vector2f _exitGridPolygonCursor;
    bool _exitGridPolygonActive = false;
    std::vector<int> _exitGridPreviewHexes;
    sf::Color _exitGridPreviewTint{ 80, 220, 80, 140 };
};

} // namespace geck
