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
#include "../../editor/Object.h"
#include "../../editor/HexagonGrid.h"
#include "../../util/ResourceManager.h"
#include "../../util/Types.h"
#include "../../format/map/Map.h"
#include "../../format/pro/Pro.h"
#include "../../selection/SelectionManager.h"
#include "../../util/Constants.h"

namespace geck {

// Forward declarations
class RenderingEngine;
class InputHandler;
class DragDropManager;
class TilePlacementManager;
class ViewportController;

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
    void setShowLightOverlays(bool show);

    Map* getMap() const { return _map.get(); }

    // Qt6 toolbar actions
    void cycleSelectionMode();
    void rotateSelectedObject();
    void changeElevation(int elevation);
    void toggleScrollBlockerRectangleMode();
    
    // Player position selection
    void enterPlayerPositionSelectionMode();
    void centerViewOnPlayerPosition();

    // Tile placement functionality (delegated to TilePlacementManager)
    void placeTileAtPosition(int tileIndex, sf::Vector2f worldPos, bool isRoof);
    void fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof);
    void replaceSelectedTiles(int newTileIndex);

    // Object placement functionality
    void placeObjectAtPosition(sf::Vector2f worldPos);
    
    // Drag preview functionality (for palette drag and drop) - now delegated to DragDropManager
    void startDragPreview(int objectIndex, int categoryInt, sf::Vector2f worldPos);
    void updateDragPreview(sf::Vector2f worldPos);
    void finishDragPreview(sf::Vector2f worldPos);
    void cancelDragPreview();

    // Efficient tile update
    void updateTileSprite(int hexIndex, bool isRoof);

    // Tile placement mode control (delegated to TilePlacementManager)
    void setTilePlacementMode(bool enabled, int tileIndex = -1, bool isRoof = false);
    void setTilePlacementAreaFill(bool enabled);
    void setTilePlacementReplaceMode(bool enabled);
    void selectAll();
    void clearSelection();
    bool isTilePlacementMode() const;

    // SFML rendering interface (called by SFMLWidget)
    void handleEvent(const sf::Event& event);
    void update(const float dt);
    void render(const float dt);
    void init();

    // Access to SFML widget for main window
    SFMLWidget* getSFMLWidget() const { return _sfmlWidget; }
    
    // Set main window reference for accessing palette panels
    void setMainWindow(class MainWindow* mainWindow) { _mainWindow = mainWindow; }
    class MainWindow* getMainWindow() const { return _mainWindow; }
    
    // Access to internal components for extracted managers
    selection::SelectionManager* getSelectionManager() const { return _selectionManager.get(); }
    TilePlacementManager* getTilePlacementManager() const { return _tilePlacementManager.get(); }
    ViewportController* getViewportController() const { return _viewportController.get(); }
    int& getCurrentHoverHex() { return _currentHoverHex; }

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

    // Helper methods for extracted managers (made public)  
    void clearDragSelectionPreview();

signals:
    void selectionChanged(const selection::SelectionState& selection, int elevation);
    void mapLoadRequested(const std::string& mapPath);
    void hexHoverChanged(int hexIndex);
    void playerPositionSelected(int hexPosition);
    void statusMessageRequested(const QString& message);
    void statusMessageClearRequested();

public slots:
    void onObjectFrmChanged(std::shared_ptr<Object> object, uint32_t newFrmPid);
    void onObjectFrmPathChanged(std::shared_ptr<Object> object, const std::string& newFrmPath);
    
    // Sprite loading methods (public for MainWindow access)
    void loadTileSprites();

private:
    // Object management
    void deleteSelectedObjects();
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
    void initializeSelectionSystem();

    void loadSprites();
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
    void updateDragSelectionPreview(sf::Vector2f startWorldPos, sf::Vector2f currentWorldPos);
    void updateTileAreaFillPreview(sf::Vector2f startWorldPos, sf::Vector2f currentWorldPos);


    
    // Wall blocker overlay management
    void createWallBlockerOverlay(const std::shared_ptr<MapObject>& mapObject, int hexPosition);
    
    // Scroll blocker rectangle functionality
    std::vector<int> calculateRectangleBorderHexes(sf::FloatRect rectangle);
    std::shared_ptr<MapObject> createScrollBlockerObject(int hexPosition);
    void createScrollBlockersFromHexes(const std::vector<int>& borderHexes);
    
    // Input system setup
    void setupInputCallbacks();


    // UI Components
    QVBoxLayout* _layout;
    SFMLWidget* _sfmlWidget;
    class MainWindow* _mainWindow;
    
    // Input, rendering, drag/drop, tile placement, and viewport systems
    std::unique_ptr<InputHandler> _inputHandler;
    std::unique_ptr<RenderingEngine> _renderingEngine;
    std::unique_ptr<DragDropManager> _dragDropManager;
    std::unique_ptr<TilePlacementManager> _tilePlacementManager;
    std::unique_ptr<ViewportController> _viewportController;

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


    int _currentElevation = 0;
    std::unique_ptr<Map> _map;

    bool _showObjects = UI::DEFAULT_SHOW_OBJECTS;
    bool _showCritters = UI::DEFAULT_SHOW_CRITTERS;
    bool _showRoof = UI::DEFAULT_SHOW_ROOF;
    bool _showWalls = UI::DEFAULT_SHOW_WALLS;
    bool _showScrollBlk = UI::DEFAULT_SHOW_SCROLL_BLK;
    bool _showWallBlockers = UI::DEFAULT_SHOW_WALL_BLK;
    bool _showHexGrid = UI::DEFAULT_SHOW_HEX_GRID;
    bool _showLightOverlays = false;  // Toggle for showing light overlays

    const sf::Texture& createBlankTexture();
    const sf::Texture& createHexTexture();
    const sf::Texture& createCursorHexTexture();

    // Double-click detection for object cycling
    sf::Clock _lastClickTime;
    sf::Vector2f _lastClickPosition;
    static constexpr float DOUBLE_CLICK_TIME = 0.5f;      // 500ms
    static constexpr float DOUBLE_CLICK_DISTANCE = 10.0f; // pixels

    // Drag selection state (managed by InputHandler now)
    sf::RectangleShape _selectionRectangle;
    std::vector<int> _previewTiles;                       // Tiles being previewed during drag
    std::vector<std::shared_ptr<Object>> _previewObjects; // Objects being previewed during drag

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
    
    // Player position selection state
    bool _playerPositionSelectionMode = false;
};

} // namespace geck