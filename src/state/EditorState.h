#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <filesystem>
#include <thread>
#include <optional>
#include <array>

#include <QObject>
#include "State.h"
#include "../editor/Object.h"
#include "../editor/HexagonGrid.h"
#include "../util/ResourceManager.h"
#include "../format/map/Map.h"
#include "../format/pro/Pro.h"


namespace geck {

struct AppData;

class EditorState : public QObject, public State {
    Q_OBJECT

private:
    EditorState(const std::shared_ptr<AppData>& appData);

    void centerViewOnMap();

    void loadSprites();
    void loadTileSprites();
    void loadObjectSprites();

    bool selectObject(sf::Vector2f worldPos);
    bool selectFloorTile(sf::Vector2f worldPos);
    bool selectRoofTile(sf::Vector2f worldPos);
    bool selectTile(sf::Vector2f worldPos, std::array<sf::Sprite, Map::TILES_PER_ELEVATION>& sprites, std::vector<int>& selectedIndexes, bool roof);
    
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

    enum class SelectionMode : int {
        ALL,
        FLOOR_TILES,
        ROOF_TILES,
        OBJECTS,

        NUM_SELECTION_TYPES
    };

    enum class EditorAction {
        NONE,
        PANNING
    };

    SelectionMode _currentSelectionMode = SelectionMode::ALL;

    HexagonGrid _hexgrid;
    std::array<sf::Sprite, Map::TILES_PER_ELEVATION> _floorSprites;
    std::array<sf::Sprite, Map::TILES_PER_ELEVATION> _roofSprites;

    std::vector<std::shared_ptr<Object>> _objects;

    std::shared_ptr<AppData> _appData;
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
    
    // Double-click detection for object cycling
    sf::Clock _lastClickTime;
    sf::Vector2f _lastClickPosition;
    static constexpr float DOUBLE_CLICK_TIME = 0.5f; // 500ms
    static constexpr float DOUBLE_CLICK_DISTANCE = 10.0f; // pixels
    
    // Selection type cycling for overlapping elements
    SelectionType _lastSelectionType = SelectionType::OBJECT;
    sf::Vector2f _lastSelectionPosition;

    std::optional<std::shared_ptr<Object>> _selectedObject;

    // TODO: merge 2*Map::TILES_PER_ELEVATION
    std::vector<int> _selectedRoofTileIndexes;
    std::vector<int> _selectedFloorTileIndexes;



public:

    EditorState(const std::shared_ptr<AppData>& appData, std::unique_ptr<Map> map);

    void createNewMap();
    void openMap();
    void saveMap();
    void quit() override;

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

signals:
    void objectSelected(std::shared_ptr<Object> object);

public:
    void init() override;
    void handleEvent(const sf::Event& event) override;
    void update(const float dt) override;
    void render(const float dt) override;
};

} // namespace geck
