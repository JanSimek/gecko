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
#include "pattern/FillPlan.h"
#include "scripting/EditArea.h"
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
    void setShowSpatialScripts(bool show) {
        _session.visibility().showSpatialScripts = show;
        // Hiding the overlay drops any spatial-script selection: it can no longer be seen, and a
        // lingering selection would let the map Delete key remove an invisible script.
        if (!show) {
            setSelectedSpatialScript(MapScript::NONE);
        }
    }
    void setShowMapEdges(bool show) {
        _session.visibility().showMapEdges = show;
        // Hiding the overlay drops any zone selection, so Delete can't remove an invisible zone.
        if (!show) {
            _selectedEdgeZone = -1;
            _activeEdgeSide = -1;
            resetEdgeHoverCursor(); // a lingering resize cursor would suggest a still-grabbable side
        }
    }
    // The unreachable-areas overlay recomputes lazily in render() from a cached signature, so the
    // setter only flips the flag (like the light overlay).
    void setShowUnreachableAreas(bool show) { _session.visibility().showUnreachable = show; }
    void setMergeSelectionOutlines(bool merge) { _session.visibility().mergeSelectionOutlines = merge; }

    // Edge scrolling: when enabled, parking the cursor near a viewport edge auto-pans the view that
    // way (parity with the reference mappers). Editor UX only — touches no map data.
    void setEdgeScrollEnabled(bool enabled) { _edgeScrollEnabled = enabled; }
    bool isEdgeScrollEnabled() const { return _edgeScrollEnabled; }

    // User-configured selection highlight colours (from preferences); forwarded to the renderer.
    void setSelectionColors(const RenderingEngine::SelectionPalette& colors);

    Map* getMap() const override { return _session.map(); }

    // Qt6 toolbar actions
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

    // Enter a one-shot "click a hex on the map" mode: the next left-click passes its hex index to
    // `onFinished`, then returns to Select; Escape (or leaving the mode) calls it with std::nullopt.
    // Reuses the player-position pick plumbing so callers (e.g. the Spatial Script dialog) don't
    // duplicate it. The caller stays responsible for any UI (e.g. hiding a non-modal dialog).
    void beginHexPick(std::function<void(std::optional<int>)> onFinished, const QString& prompt);

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

    // Eyedropper (P): sample the topmost object/tile under the cursor and load it into its palette.
    // A tile arms tile painting; an object arms click-to-place via beginObjectPlacement().
    void pickAtCursor(sf::Vector2f worldPos);
    // Enter PlaceObject mode for the given proto: reveal it in the palette and show a cursor ghost
    // whose left-click drops a copy (see EditorMode::PlaceObject).
    void beginObjectPlacement(uint32_t pid, sf::Vector2f worldPos);

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

    // --- Area fill over the current selection (driven by a Luau script) ----------------------------
    // These drive the Fill dialog: it snapshots the area once, previews on each parameter change
    // (a ghost overlay), and commits the previewed plan on Apply — all without a new EditorMode.
    //
    // The current selection projected to a (sorted) EditArea: selected floor/roof tiles, plus the
    // hexes covering them (so object scatter works over a tile selection) and any selected hexes.
    EditArea selectionFillArea() const;
    // True when the selection has any fillable target (floor/roof tiles or hexes), i.e. not a
    // pure-object selection. Gates the Fill action's enabled state.
    bool hasFillableSelection() const;
    // Remove the ghost overlay and drop the retained plan.
    void clearFillPreview();
    // Commit the last previewed plan as one undo entry, then run the post-edit resync (clear the
    // selection, refresh Map Info, flag the map modified). No-op when no plan is pending.
    void applyFillPreview(const QString& description);
    // The plan behind the current preview (for the dialog's tile/object summary). Empty when no
    // preview is active. The Luau preview path populates it.
    const pattern::FillPlan& fillPlan() const { return _fillPlan; }

#ifdef GECK_SCRIPTING_ENABLED
    // --- Procedural fill — a Luau script paints the selection --------------------------------------
    // `source` is a Luau script run (under a wall-clock budget) against the selection via the plan
    // sink: its api:paintFloor/scatter/… calls RECORD into a fresh plan instead of committing, so the
    // result previews as a ghost and applyFillPreview() replays it as one undo entry. Returns the
    // run's ScriptResult (ok/error/print output); on failure it clears the preview and leaves the
    // plan empty.
    ScriptResult previewLuaFill(const EditArea& area, const std::string& source, uint32_t seed);
#endif

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
    // Edit / delete an existing spatial script by SID (undoable). Both refresh the script panels via
    // mapScriptsChanged; delete also clears the selection if it pointed at the removed script.
    void editSpatialScript(uint32_t sid, uint32_t programIndex, int tile, int elevation, int radius);
    void deleteSpatialScript(uint32_t sid);

    // Shared "selected spatial script" state (map click <-> Scripts panel). Setting it emits
    // spatialScriptSelectionChanged so the panel can mirror the row; MapScript::NONE clears it.
    void setSelectedSpatialScript(uint32_t sid);
    [[nodiscard]] uint32_t selectedSpatialScript() const { return _session.selectedSpatialScriptSid(); }

    // Current field values of the spatial script with this SID (for pre-filling an edit dialog),
    // or nullopt if none matches. tile/elevation are the decoded built_tile.
    struct SpatialScriptInfo {
        uint32_t programIndex;
        int tile;
        int elevation;
        int radius;
    };
    [[nodiscard]] std::optional<SpatialScriptInfo> spatialScriptInfo(uint32_t sid) const;

    // Map-edge (.edg) editing (undoable). All operate on the current elevation and emit
    // mapEdgeChanged() so the Map Edges panel refreshes. addEdgeZone seeds a full-grid zone and
    // selects it; deleteSelectedEdgeZone / toggleEdgeClipSide act on the current selection/elevation.
    void addEdgeZone();
    void deleteSelectedEdgeZone();
    void toggleEdgeClipSide(int side); // 0=left,1=top,2=right,3=bottom
    void upgradeMapEdgeToVersion2();
    void resetMapEdgeSquare();
    [[nodiscard]] int selectedEdgeZone() const { return _selectedEdgeZone; }
    [[nodiscard]] int currentElevation() const;
    [[nodiscard]] const std::optional<MapEdge>& mapEdge() const;

signals:
    /// The map-side spatial-script selection changed (marker click or clear). MainWindow mirrors it
    /// onto the Scripts panel row. Carries MapScript::NONE when nothing is selected.
    void spatialScriptSelectionChanged(uint32_t sid);
    /// The user double-clicked a spatial marker on the map: open its editor (same as the panel's
    /// edit request). MainWindow owns the dialog.
    void spatialScriptEditActivated(uint32_t sid);
    /// A spatial script was added / edited / deleted, so the script panels must repopulate.
    void mapScriptsChanged();
    /// The map edge changed (zone added/removed/moved, clip/version/square edit) or the selected
    /// zone changed, so the Map Edges panel must refresh its counts, buttons and clip state.
    void mapEdgeChanged();

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
    /// The contextual status-bar key-hint changed because the mode or the selection changed.
    /// MainWindow shows it on a permanent status-bar label (separate from transient messages).
    void hintChanged(const QString& hint);

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
    // Recompute the contextual key-hint from the current mode + selection and emit hintChanged.
    // Called whenever either changes (mode switch, selection callback) so the status bar stays current.
    void emitHintChanged();

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

    // If the spatial-script overlay is visible and a marker sits on the clicked hex (current
    // elevation), select that script (clearing any object/tile selection) and return true. A second
    // quick click on the same marker also fires spatialScriptEditActivated. A miss clears the spatial
    // selection and returns false so normal object selection proceeds.
    bool trySelectSpatialScriptAt(sf::Vector2f worldPos);

    // If the map-edge overlay is visible and the click lands near a zone's border on the current
    // elevation, select that zone (clearing object/tile/spatial selection) and return true. Selection
    // is by border proximity, not area, so a click in a zone's interior falls through to normal object
    // selection. A miss clears the edge selection and returns false.
    bool trySelectEdgeZoneAt(sf::Vector2f worldPos);

    // Edge side-drag, layered onto the existing object-drag gesture. edgeSideAtForDrag returns the
    // {zone, side} whose side lies within grab range of worldPos (nullopt if none). begin selects that
    // zone and snapshots the edge; preview moves the side live (no undo); commit records the whole
    // gesture as one undo entry; cancel restores the snapshot.
    std::optional<std::pair<int, int>> edgeSideAtForDrag(sf::Vector2f worldPos) const;
    bool beginEdgeSideDrag(sf::Vector2f worldPos);
    void previewEdgeSideDrag(sf::Vector2f worldPos);
    void commitEdgeSideDrag(sf::Vector2f worldPos);
    void cancelEdgeSideDrag();

    // Hover feedback for the side-drag: show a horizontal/vertical resize cursor while the mouse is
    // over a grabbable zone side (and while dragging one); restore the default cursor otherwise.
    void updateEdgeHoverCursor(sf::Vector2f worldPos);
    void resetEdgeHoverCursor();

    // Handles a click in SetPlayerPosition mode: routes to an armed beginHexPick callback (one-shot)
    // or, failing that, emits playerPositionSelected (legacy player-start pick).
    void handlePositionPickClick(sf::Vector2f worldPos);

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
    // Exit-grid "Draw edge" live preview: recompute the prospective on-line hexes (the gap-free hex
    // line through the committed vertices + the live cursor) and the tint from the tool's current
    // destination kind, for the renderer to draw the polyline and marked hexes.
    void updateMarkExitsLinePreview(const std::vector<sf::Vector2f>& vertices, sf::Vector2f cursor, bool flipSide);
    void clearMarkExitsLinePreview();
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

    // Edge-scroll tick: run from update(dt); pans the view while the cursor rests near a viewport
    // edge, then re-fires the cursor's move so hover/drag previews track the scrolled map.
    void updateEdgeScroll(float dt);

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

    // Map-side double-click detection for spatial markers: a second click on the same SID within
    // kSpatialDoubleClickSeconds opens its editor. Self-contained (no InputHandler timing plumbing).
    sf::Clock _spatialClickClock;
    uint32_t _lastSpatialClickSid = 0xFFFFFFFFu; // MapScript::NONE
    static constexpr float kSpatialDoubleClickSeconds = 0.4f;

    // Edge scrolling: auto-pan when the cursor rests near a viewport edge (see setEdgeScrollEnabled).
    // Default on to match both reference mappers; the View menu / Settings persist the user's choice.
    bool _edgeScrollEnabled = true;

    // Map-edge (.edg) overlay editing state. _selectedEdgeZone indexes the current elevation's zones
    // (-1 = none); _activeEdgeSide is the side being dragged (0=left,1=top,2=right,3=bottom; -1 = none).
    int _selectedEdgeZone = -1;
    int _activeEdgeSide = -1;
    // Live side-drag state: while _draggingEdgeSide, mouse moves preview _edgeDragSide of the selected
    // zone; _edgeDragBefore is the pre-drag edge snapshot, committed as one undo entry on release.
    bool _draggingEdgeSide = false;
    int _edgeDragSide = -1;
    std::optional<MapEdge> _edgeDragBefore;
    // The side whose resize cursor is currently applied to the viewport (-1 = default cursor); see
    // updateEdgeHoverCursor.
    int _edgeHoverCursorSide = -1;

    // Cached "unreachable areas" overlay: the walkable hexes on the current elevation stranded from
    // every entry point (player start + exit grids). The flood-fill (geck::reachability) is too heavy
    // to run per frame, so render() recomputes it only when the map pointer, elevation, or object
    // count changes — a cheap signature that catches load/new, elevation switch, and object add/remove.
    std::vector<int> _unreachableHexes;
    const Map* _unreachableCacheMap = nullptr;
    int _unreachableCacheElevation = -1;
    std::size_t _unreachableCacheObjectCount = 0;
    bool _unreachableCacheValid = false;
    // Recompute _unreachableHexes if the signature changed; a no-op when the cache is still current.
    void refreshUnreachableOverlay();

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

    // A3 fill preview: a semi-transparent ghost of the previewed FillPlan (floor under, objects,
    // roof over) drawn through the same RenderData.stampPreview path, plus the pristine plan replayed
    // on Apply. The ghosts are rebuilt from the plan's data so the plan itself stays untouched (its
    // objects render at full opacity once committed). Stamp and fill previews are never live at once.
    std::vector<sf::Sprite> _fillPreviewFloorTiles;
    std::vector<std::shared_ptr<Object>> _fillPreviewObjects;
    std::vector<sf::Sprite> _fillPreviewRoofTiles;
    bool _fillPreviewActive = false;
    pattern::FillPlan _fillPlan;
    void buildFillGhosts();

    // Player position selection state
    bool _playerPositionSelectionMode = false;

    // When set, the SetPlayerPosition mode is being used as a generic one-shot hex picker (see
    // beginHexPick): the next click routes here instead of emitting playerPositionSelected.
    std::function<void(std::optional<int>)> _hexPickCallback;

    // Exit-grid "Draw edge" preview state (MarkExits mode). _exitGridLineActive gates the renderer;
    // the vertices/cursor draw the polyline; the hexes are the prospective on-line hexes (recomputed
    // each mouse move); the tint reflects the tool's current destination kind.
    std::vector<sf::Vector2f> _exitGridLineVertices;
    sf::Vector2f _exitGridLineCursor;
    bool _exitGridLineActive = false;
    std::vector<int> _exitGridPreviewHexes;
    // The directional marker FRM for each prospective preview hex (parallel to _exitGridPreviewHexes),
    // so the preview shows the same directional art the commit will place (honouring the override).
    std::vector<uint32_t> _exitGridPreviewFrmPids;
    sf::Color _exitGridPreviewTint{ 80, 220, 80, 140 };
};

} // namespace geck
