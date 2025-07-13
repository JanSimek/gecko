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

namespace geck {

// Tile geometry constants for isometric tiles
constexpr int TILE_WIDTH = 80;
constexpr int TILE_HEIGHT = 36;

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
    
    Map* getMap() const { return _map.get(); }
    
    // Qt6 toolbar actions
    void cycleSelectionMode();
    void rotateSelectedObject();
    void changeElevation(int elevation);

    // SFML rendering interface (called by SFMLWidget)
    void handleEvent(const sf::Event& event);
    void update(const float dt);
    void render(const float dt);
    void init();

    // Access to SFML widget for main window
    SFMLWidget* getSFMLWidget() const { return _sfmlWidget; }

signals:
    void objectSelected(std::shared_ptr<Object> object);
    void tileSelected(int tileIndex, int elevation, bool isRoof);
    void tileSelectionCleared();
    void mapLoadRequested(const std::string& mapPath);

private:
    void setupUI();
    void centerViewOnMap();

    void loadSprites();
    void loadTileSprites();
    void loadObjectSprites();

    bool selectObject(sf::Vector2f worldPos);
    bool selectFloorTile(sf::Vector2f worldPos);
    bool selectRoofTile(sf::Vector2f worldPos);
    bool selectAllAtPosition(sf::Vector2f worldPos);
    bool selectTile(sf::Vector2f worldPos, std::array<sf::Sprite, Map::TILES_PER_ELEVATION>& sprites, std::vector<int>& selectedIndexes, bool roof);
    bool isTileVisible(int tileIndex, bool roof);
    
    // Tile selection helper
    bool isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite);
    
    // New improved object selection methods
    std::vector<std::shared_ptr<Object>> getObjectsAtPosition(sf::Vector2f worldPos);
    bool isPointInSpritePixel(sf::Vector2f worldPos, const sf::Sprite& sprite);
    bool isPointInSpriteBounds(sf::Vector2f worldPos, const sf::Sprite& sprite);
    bool isDoubleClick(sf::Vector2f worldPos);
    void cycleObjectsAtPosition(sf::Vector2f worldPos);
    
    // Selection type cycling for overlapping elements
    enum class SelectionType { OBJECT, ROOF_TILE, FLOOR_TILE };
    bool selectAtPosition(sf::Vector2f worldPos);

    void unselectAll();
    void unselectTiles();
    void unselectObject();
    
    // Zoom management
    void zoomView(float direction);

    enum class EditorAction {
        NONE,
        PANNING
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
    
    // Selection type cycling for overlapping elements
    SelectionType _lastSelectionType = SelectionType::OBJECT;
    sf::Vector2f _lastSelectionPosition;

    std::optional<std::shared_ptr<Object>> _selectedObject;

    // TODO: merge 2*Map::TILES_PER_ELEVATION
    std::vector<int> _selectedRoofTileIndexes;
    std::vector<int> _selectedFloorTileIndexes;
};

} // namespace geck