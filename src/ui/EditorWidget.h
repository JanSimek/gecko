#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <filesystem>
#include <thread>
#include <optional>
#include <array>
#include <set>
#include <vector>
#include <utility>

#include <QWidget>
#include <QVBoxLayout>
#include "../editor/Object.h"
#include "../editor/HexagonGrid.h"
#include "../util/ResourceManager.h"
#include "../util/Types.h"
#include "../format/map/Map.h"
#include "../format/pro/Pro.h"
#include "../selection/SelectionManager.h"
#include "../util/Constants.h"

namespace geck {

class SFMLWidget;
struct ObjectInfo;

class EditorWidget : public QWidget {
    Q_OBJECT

public:
    EditorWidget(std::unique_ptr<Map> map, QWidget* parent = nullptr);
    ~EditorWidget();

    void createNewMap();
    void openMap();
    void saveMap();

    // Qt6 menu integration - visibility controls
    void setShowObjects(bool show) { _showObjects = show; }
    void setShowCritters(bool show) { _showCritters = show; }
    void setShowWalls(bool show) { _showWalls = show; }
    void setShowRoof(bool show) { _showRoof = show; }
    void setShowScrollBlk(bool show) { _showScrollBlk = show; }
    void setShowWallBlockers(bool show) { _showWallBlockers = show; }
    void setShowHexGrid(bool show) { _showHexGrid = show; }

    Map* getMap() const { return _map.get(); }

    // Qt6 toolbar actions
    void cycleSelectionMode();
    void rotateSelectedObject();
    void changeElevation(int elevation);
    
    // Player position selection
    void enterPlayerPositionSelectionMode();

    // Tile placement functionality
    void placeTileAtPosition(int tileIndex, sf::Vector2f worldPos, bool isRoof);
    void fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof);
    void replaceSelectedTiles(int newTileIndex);

    // Object placement functionality
    void placeObjectAtPosition(sf::Vector2f worldPos);
    
    // Drag preview functionality (for palette drag and drop)
    void startDragPreview(int objectIndex, int categoryInt, sf::Vector2f worldPos);
    void updateDragPreview(sf::Vector2f worldPos);
    void finishDragPreview(sf::Vector2f worldPos);
    void cancelDragPreview();

    // Efficient tile update
    void updateTileSprite(int hexIndex, bool isRoof);

    // Tile placement mode control
    void setTilePlacementMode(bool enabled, int tileIndex = -1, bool isRoof = false);
    void setTilePlacementAreaFill(bool enabled);
    void setTilePlacementReplaceMode(bool enabled);
    void selectAll();
    void clearSelection();
    bool isTilePlacementMode() const { return _tilePlacementMode; }

    // SFML rendering interface (called by SFMLWidget)
    void handleEvent(const sf::Event& event);
    void update(const float dt);
    void render(const float dt);
    void init();

    // Access to SFML widget for main window
    SFMLWidget* getSFMLWidget() const { return _sfmlWidget; }
    
    // Set main window reference for accessing palette panels
    void setMainWindow(class MainWindow* mainWindow) { _mainWindow = mainWindow; }

    // Methods for SelectionManager (moved from private)
    std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f worldPos);
    bool isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite);

    // Tile hit testing methods
    std::optional<int> getTileAtPosition(sf::Vector2f worldPos, bool isRoof);
    std::optional<int> getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos);

    // Access to sprite vectors for SelectionManager
    const std::vector<sf::Sprite>& getFloorSprites() const { return _floorSprites; }
    const std::vector<sf::Sprite>& getRoofSprites() const { return _roofSprites; }

    // Access to current elevation and map data
    int getCurrentElevation() const { return _currentElevation; }
    Map::MapFile& getMapFile() { return _map->getMapFile(); }
    const Map::MapFile& getMapFile() const { return _map->getMapFile(); }

    // Access to objects for SelectionManager
    const std::vector<std::shared_ptr<Object>>& getObjects() const { return _objects; }

    // Access to hex grid for SelectionManager
    const HexagonGrid* getHexagonGrid() const { return &_hexgrid; }

    // Hex grid utilities for SelectionManager
    int worldPosToHexPosition(sf::Vector2f worldPos) const;

signals:
    void objectSelected(std::shared_ptr<Object> object);
    void tileSelected(int tileIndex, int elevation, bool isRoof);
    void tileSelectionCleared();
    void selectionChanged(const selection::SelectionState& selection, int elevation);
    void mapLoadRequested(const std::string& mapPath);
    void hexHoverChanged(int hexIndex);
    void playerPositionSelected(int hexPosition);

private:
    // Error tracking for sprite loading
    struct LoadingErrors {
        size_t objectsSkipped = 0;
        std::set<std::string> failedFrmNames;
        std::vector<std::pair<std::string, int>> failedObjects; // FRM name, position
        
        void clear() {
            objectsSkipped = 0;
            failedFrmNames.clear();
            failedObjects.clear();
        }
        
        bool hasErrors() const {
            return objectsSkipped > 0;
        }
    };
    
    void setupUI();
    void centerViewOnMap();
    void initializeSelectionSystem();

    void loadSprites();
    void loadTileSprites();
    void loadObjectSprites();
    void showLoadingErrorsSummary();

    // Object selection methods (moved to public)
    bool isPointInSpritePixel(sf::Vector2f worldPos, const sf::Sprite& sprite) const;
    bool isPointInSpriteBounds(sf::Vector2f worldPos, const sf::Sprite& sprite) const;
    bool isDoubleClick(sf::Vector2f worldPos);

    // Selection modifiers for multi-selection
    enum class SelectionModifier {
        NONE,   // Normal single selection (clear and select)
        ADD,    // Ctrl+Click - add to selection
        TOGGLE, // Alt+Click - toggle selection
        RANGE   // Shift+Click - range selection for tiles
    };

    bool selectAtPosition(sf::Vector2f worldPos);
    bool selectAtPosition(sf::Vector2f worldPos, SelectionModifier modifier);
    selection::SelectionResult handleRangeSelection(sf::Vector2f worldPos);

    void clearAllVisualSelections();
    void clearDragPreview();
    void updateDragSelectionPreview(sf::Vector2f currentWorldPos);
    void updateTileAreaFillPreview(sf::Vector2f currentWorldPos);

    // Object drag management
    bool startObjectDrag(sf::Vector2f worldPos);
    void updateObjectDrag(sf::Vector2f currentWorldPos);
    void finishObjectDrag(sf::Vector2f finalWorldPos);
    void cancelObjectDrag();
    bool canStartObjectDrag(sf::Vector2f worldPos) const;

    // Hex grid snapping helpers
    sf::Vector2f snapToHexGrid(sf::Vector2f worldPos) const;

    // Hex grid visualization
    void renderHexGrid();
    void updateHoverHex(sf::Vector2f worldPos);

    // Zoom management
    void zoomView(float direction);

    // Helper methods
    int worldPosToHexIndex(sf::Vector2f worldPos) const;
    
    // Wall blocker overlay management
    void createWallBlockerOverlay(const std::shared_ptr<MapObject>& mapObject, int hexPosition);

    enum class EditorAction {
        NONE,
        PANNING,
        DRAG_SELECTING,
        TILE_PLACING,
        OBJECT_MOVING
    };

    // UI Components
    QVBoxLayout* _layout;
    SFMLWidget* _sfmlWidget;
    class MainWindow* _mainWindow;

    // Game/Editor State
    SelectionMode _currentSelectionMode = SelectionMode::ALL;

    HexagonGrid _hexgrid;
    // Note: Using std::vector instead of std::array because SFML 3 sf::Sprite
    // requires a texture in constructor and is not default-constructible
    std::vector<sf::Sprite> _floorSprites;
    std::vector<sf::Sprite> _roofSprites;

    std::vector<std::shared_ptr<Object>> _objects;
    
    // Wall blocker overlay sprites (rendered on top of regular objects)
    std::vector<sf::Sprite> _wallBlockerOverlays;
    
    // Error tracking for last sprite loading operation
    LoadingErrors _lastLoadErrors;

    sf::View _view;

    int _currentElevation = 0;
    std::unique_ptr<Map> _map;

    bool _showObjects = UI::DEFAULT_SHOW_OBJECTS;
    bool _showCritters = UI::DEFAULT_SHOW_CRITTERS;
    bool _showRoof = UI::DEFAULT_SHOW_ROOF;
    bool _showWalls = UI::DEFAULT_SHOW_WALLS;
    bool _showScrollBlk = UI::DEFAULT_SHOW_SCROLL_BLK;
    bool _showWallBlockers = UI::DEFAULT_SHOW_WALL_BLK;
    bool _showHexGrid = UI::DEFAULT_SHOW_HEX_GRID;

    sf::Vector2i _mouseStartingPosition{ 0, 0 }; // panning started
    sf::Vector2i _mouseLastPosition{ 0, 0 };     // current panning position
    EditorAction _currentAction = EditorAction::NONE;
    // sf::Cursor _cursor; // TODO: Fix cursor initialization for SFML 3
    const sf::Texture& createBlankTexture();
    const sf::Texture& createHexTexture();
    const sf::Texture& createCursorHexTexture();

    // Zoom level tracking and limits
    float _zoomLevel = 1.0f;
    static constexpr float MIN_ZOOM = 0.1f;   // Can zoom out to 10% of original size
    static constexpr float MAX_ZOOM = 5.0f;   // Can zoom in to 500% of original size
    static constexpr float ZOOM_STEP = 0.05f; // 5% zoom steps for smooth zooming

    // Double-click detection for object cycling
    sf::Clock _lastClickTime;
    sf::Vector2f _lastClickPosition;
    static constexpr float DOUBLE_CLICK_TIME = 0.5f;      // 500ms
    static constexpr float DOUBLE_CLICK_DISTANCE = 10.0f; // pixels

    // Drag selection state
    sf::Vector2f _dragStartWorldPos;
    sf::RectangleShape _selectionRectangle;
    bool _isDragSelecting = false;
    bool _immediateSelectionPerformed = false;            // Track if immediate selection was performed on mouse press
    std::vector<int> _previewTiles;                       // Tiles being previewed during drag
    std::vector<std::shared_ptr<Object>> _previewObjects; // Objects being previewed during drag

    // Object drag state
    bool _isDraggingObjects = false;
    std::vector<std::shared_ptr<Object>> _draggedObjects; // Objects being dragged
    std::vector<sf::Vector2f> _objectDragStartPositions;  // Original positions for cancel/revert
    sf::Vector2f _objectDragOffset;                       // Current drag offset from start position
    
    // Drag preview state (for palette drag and drop)
    bool _isDraggingFromPalette = false;
    std::shared_ptr<Object> _dragPreviewObject;           // Preview object being dragged from palette
    int _previewObjectIndex = -1;                         // Index of object in palette
    int _previewObjectCategory = 0;                       // Category of preview object (as int)
    const ObjectInfo* _previewObjectInfo = nullptr;      // Object info from palette

    // Hex grid visualization
    sf::Sprite _hexSprite;          // Hex grid sprite from HEX.frm
    sf::Sprite _hexHighlightSprite; // Red highlight sprite for mouse hover
    sf::Sprite _playerPositionSprite; // Blue marker sprite for player default position
    int _currentHoverHex = -1;      // Current hex index under mouse cursor

    // Selection management
    std::unique_ptr<selection::SelectionManager> _selectionManager;

    // Selected roof tile background sprites (blank.frm tiles for transparent pixel visibility)
    std::vector<sf::Sprite> _selectedRoofTileBackgroundSprites;

    // Selected hex sprites for visual feedback
    std::vector<sf::Sprite> _selectedHexSprites;

    // Tile placement state
    bool _tilePlacementMode = false;
    bool _tilePlacementAreaFill = false;
    bool _tilePlacementReplaceMode = false;
    int _tilePlacementIndex = -1;
    bool _tilePlacementIsRoof = false;
    
    // Player position selection state
    bool _playerPositionSelectionMode = false;
};

} // namespace geck