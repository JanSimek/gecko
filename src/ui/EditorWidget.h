#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <filesystem>
#include <thread>
#include <optional>
#include <array>

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
    void setShowHexGrid(bool show) { _showHexGrid = show; }
    
    Map* getMap() const { return _map.get(); }
    
    // Qt6 toolbar actions
    void cycleSelectionMode();
    void rotateSelectedObject();
    void changeElevation(int elevation);
    
    // Tile placement functionality
    void placeTileAtPosition(int tileIndex, sf::Vector2f worldPos, bool isRoof);
    void fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof);
    void replaceSelectedTiles(int newTileIndex);
    
    // Efficient tile update
    void updateTileSprite(int hexIndex, bool isRoof);
    
    // Tile placement mode control
    void setTilePlacementMode(bool enabled, int tileIndex = -1, bool isRoof = false);
    void setTilePlacementAreaFill(bool enabled);
    void setTilePlacementReplaceMode(bool enabled);
    bool isTilePlacementMode() const { return _tilePlacementMode; }

    // SFML rendering interface (called by SFMLWidget)
    void handleEvent(const sf::Event& event);
    void update(const float dt);
    void render(const float dt);
    void init();

    // Access to SFML widget for main window
    SFMLWidget* getSFMLWidget() const { return _sfmlWidget; }
    
    // Methods for SelectionManager (moved from private)
    std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f worldPos);
    bool isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite);
    
    // Tile hit testing methods
    std::optional<int> getTileAtPosition(sf::Vector2f worldPos, bool isRoof);
    std::optional<int> getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos);
    
    // Access to sprite arrays for SelectionManager
    const std::array<sf::Sprite, Map::TILES_PER_ELEVATION>& getFloorSprites() const { return _floorSprites; }
    const std::array<sf::Sprite, Map::TILES_PER_ELEVATION>& getRoofSprites() const { return _roofSprites; }
    
    // Access to current elevation and map data
    int getCurrentElevation() const { return _currentElevation; }
    Map::MapFile& getMapFile() { return _map->getMapFile(); }
    const Map::MapFile& getMapFile() const { return _map->getMapFile(); }
    
    // Access to objects for SelectionManager
    const std::vector<std::shared_ptr<Object>>& getObjects() const { return _objects; }

signals:
    void objectSelected(std::shared_ptr<Object> object);
    void tileSelected(int tileIndex, int elevation, bool isRoof);
    void tileSelectionCleared();
    void selectionChanged(const selection::SelectionState& selection, int elevation);
    void mapLoadRequested(const std::string& mapPath);

private:
    void setupUI();
    void centerViewOnMap();
    void initializeSelectionSystem();

    void loadSprites();
    void loadTileSprites();
    void loadObjectSprites();

    // Object selection methods (moved to public)
    bool isPointInSpritePixel(sf::Vector2f worldPos, const sf::Sprite& sprite) const;
    bool isPointInSpriteBounds(sf::Vector2f worldPos, const sf::Sprite& sprite) const;
    bool isDoubleClick(sf::Vector2f worldPos);
    
    
    // Selection modifiers for multi-selection
    enum class SelectionModifier {
        NONE,        // Normal single selection (clear and select)
        ADD,         // Ctrl+Click - add to selection
        TOGGLE,      // Alt+Click - toggle selection
        RANGE        // Shift+Click - range selection for tiles
    };
    
    bool selectAtPosition(sf::Vector2f worldPos);
    bool selectAtPosition(sf::Vector2f worldPos, SelectionModifier modifier);
    selection::SelectionResult handleRangeSelection(sf::Vector2f worldPos);

    void clearAllVisualSelections();
    void updateDragPreview(sf::Vector2f currentWorldPos);
    void clearDragPreview();
    void updateTileAreaFillPreview(sf::Vector2f currentWorldPos);
    
    // Object drag management
    bool startObjectDrag(sf::Vector2f worldPos);
    void updateObjectDrag(sf::Vector2f currentWorldPos);
    void finishObjectDrag(sf::Vector2f finalWorldPos);
    void cancelObjectDrag();
    bool canStartObjectDrag(sf::Vector2f worldPos) const;
    
    // Hex grid snapping helpers
    sf::Vector2f snapToHexGrid(sf::Vector2f worldPos) const;
    int worldPosToHexPosition(sf::Vector2f worldPos) const;
    
    // Hex grid visualization
    void renderHexGrid();
    
    // Zoom management
    void zoomView(float direction);
    
    // Helper methods
    int worldPosToHexIndex(sf::Vector2f worldPos) const;

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

    // Game/Editor State
    SelectionMode _currentSelectionMode = SelectionMode::ALL;

    HexagonGrid _hexgrid;
    std::array<sf::Sprite, Map::TILES_PER_ELEVATION> _floorSprites;
    std::array<sf::Sprite, Map::TILES_PER_ELEVATION> _roofSprites;

    std::vector<std::shared_ptr<Object>> _objects;

    sf::View _view;

    int _currentElevation = 0;
    std::unique_ptr<Map> _map;

    bool _showObjects = true;
    bool _showCritters = true;
    bool _showRoof = true;
    bool _showWalls = true;
    bool _showScrollBlk = true;
    bool _showHexGrid = false;

    sf::Vector2i _mouseStartingPosition{ 0, 0 }; // panning started
    sf::Vector2i _mouseLastPosition{ 0, 0 };     // current panning position
    EditorAction _currentAction = EditorAction::NONE;
    sf::Cursor _cursor;
    
    // Zoom level tracking and limits
    float _zoomLevel = 1.0f;
    static constexpr float MIN_ZOOM = 0.1f;   // Can zoom out to 10% of original size
    static constexpr float MAX_ZOOM = 5.0f;   // Can zoom in to 500% of original size
    static constexpr float ZOOM_STEP = 0.05f; // 5% zoom steps for smooth zooming
    
    // Double-click detection for object cycling
    sf::Clock _lastClickTime;
    sf::Vector2f _lastClickPosition;
    static constexpr float DOUBLE_CLICK_TIME = 0.5f; // 500ms
    static constexpr float DOUBLE_CLICK_DISTANCE = 10.0f; // pixels
    
    // Fake sprite for tile hit detection
    sf::Sprite _fakeTileSprite;
    
    // Drag selection state
    sf::Vector2f _dragStartWorldPos;
    sf::RectangleShape _selectionRectangle;
    bool _isDragSelecting = false;
    std::vector<int> _previewTiles; // Tiles being previewed during drag
    std::vector<std::shared_ptr<Object>> _previewObjects; // Objects being previewed during drag
    
    // Object drag state
    bool _isDraggingObjects = false;
    std::vector<std::shared_ptr<Object>> _draggedObjects; // Objects being dragged
    std::vector<sf::Vector2f> _objectDragStartPositions; // Original positions for cancel/revert
    sf::Vector2f _objectDragOffset; // Current drag offset from start position
    
    // Empty roof tile highlighting
    std::vector<sf::RectangleShape> _emptyRoofTileIndicators; // Visual indicators for empty roof tiles
    
    // Hex grid visualization
    sf::Sprite _hexSprite; // Hex grid sprite from HEX.frm
    
    // Selection management
    std::unique_ptr<selection::SelectionManager> _selectionManager;
    
    // Tile placement state
    bool _tilePlacementMode = false;
    bool _tilePlacementAreaFill = false;
    bool _tilePlacementReplaceMode = false;
    int _tilePlacementIndex = -1;
    bool _tilePlacementIsRoof = false;
};

} // namespace geck