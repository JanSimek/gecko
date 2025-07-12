#define QT_NO_EMIT
#include "EditorState.h"

// TODO: Migrate to Qt6 - ImGui includes removed temporarily
// #include <imgui.h>
// #include <imgui_stdlib.h>
// #include <imgui-SFML.h>

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include <cmath> // ceil
#include "../util/QtDialogs.h"
#include "../ui/MainWindow.h"

// TODO: Migrate to Qt6 - UI panels removed temporarily
// #include "../ui/util.h"
// #include "../ui/panel/MainMenuPanel.h"
// #include "../ui/panel/TileSelectionPanel.h"
// #include "../ui/panel/MapInfoPanel.h"
// #include "../ui/panel/SelectedObjectPanel.h"
// #include "../ui/panel/Toolbar.h"

#include "../editor/Object.h"

#include "../reader/dat/DatReader.h"
#include "../reader/frm/FrmReader.h"
#include "../reader/lst/LstReader.h"
#include "../reader/gam/GamReader.h"
#include "../reader/pro/ProReader.h"

#include "../writer/map/MapWriter.h"

#include "../format/frm/Direction.h"
#include "../format/frm/Frame.h"
#include "../format/frm/Frm.h"
#include "../format/lst/Lst.h"
#include "../format/gam/Gam.h"
#include "../format/map/Tile.h"
#include "../format/msg/Msg.h"
#include "../format/pro/Pro.h"
#include "../format/map/MapObject.h"

#include "loader/MapLoader.h"

#include "../util/ProHelper.h"

#include "LoadingState.h"
#include "StateMachine.h"
#include "reader/msg/MsgReader.h"

namespace geck {

EditorState::EditorState(const std::shared_ptr<AppData>& appData, std::unique_ptr<Map> map)
    : _appData(appData)
    , _view({ 0.f, 0.f }, sf::Vector2f(800.f, 600.f)) // Default size, will be updated on first resize
    , _map(std::move(map))
    , _fakeTileSprite(ResourceManager::getInstance().texture("art/tiles/blank.frm")) {
    centerViewOnMap();
}


void EditorState::setUpPanels() {
    // TODO: Migrate to Qt6 - TileSelectionPanel temporarily disabled
    /*
    auto tile_selection_panel = std::make_shared<TileSelectionPanel>();

    tile_selection_panel->tileClicked.connect([this](int newTileId) {
        const auto& lst = ResourceManager::getInstance().getResource<Lst, std::string>("art/tiles/tiles.lst");

        for (auto selectedTileId : _selectedFloorTileIndexes) {

            _map->getMapFile().tiles.at(_currentElevation).at(selectedTileId).setFloor(newTileId);

            sf::Sprite tile_sprite;
            std::string texture_path = "art/tiles/" + lst->at(newTileId);

            auto selectedTile = _floorSprites.at(selectedTileId);
            tile_sprite.setTexture(ResourceManager::getInstance().texture(texture_path));
            tile_sprite.setPosition(selectedTile.getPosition().x, selectedTile.getPosition().y);
            _floorSprites.at(selectedTileId) = tile_sprite;
        }
        for (auto selectedTileId : _selectedRoofTileIndexes) {

            _map->getMapFile().tiles.at(_currentElevation).at(selectedTileId).setRoof(newTileId);

            sf::Sprite tile_sprite;
            std::string texture_path = "art/tiles/" + lst->at(newTileId);

            auto selectedTile = _roofSprites.at(selectedTileId);
            tile_sprite.setTexture(ResourceManager::getInstance().texture(texture_path));
            tile_sprite.setPosition(selectedTile.getPosition().x, selectedTile.getPosition().y);
            _roofSprites.at(selectedTileId) = tile_sprite;
        }
        unselectTiles();
    });
    */

    // TODO: Migrate to Qt6 - UI panels temporarily disabled
    /*
    auto toolbar = std::make_shared<Toolbar>("Toolbar");

    Signal<> selectionMode;
    selectionMode.connect([this]() {
        _currentSelectionMode = static_cast<SelectionMode>((static_cast<int>(_currentSelectionMode) + 1) % static_cast<int>(SelectionMode::NUM_SELECTION_TYPES));
    });
    toolbar->addButton([this]() {
        switch(_currentSelectionMode) {
        case SelectionMode::OBJECTS:
            return ICON_FA_OBJECT_GROUP " Objects";
        case SelectionMode::FLOOR_TILES:
            return ICON_FA_OBJECT_GROUP " Floor";
        case SelectionMode::ROOF_TILES:
            return ICON_FA_OBJECT_GROUP " Roof";
        case SelectionMode::ALL:
        default:
            return ICON_FA_OBJECT_GROUP " All";
        }
    }, std::move(selectionMode), "Selection mode\t(toggle: m)");

    Signal<> rotate;
    rotate.connect([this]() {
        if (_selectedObject) {
            _selectedObject->get()->rotate();
        }
    });
    toolbar->addButton([]() { return ICON_FA_ROTATE; }, std::move(rotate), "Rotate selected object\t(Ctrl+R)");

    _panels.emplace_back(std::move(toolbar));

    _panels.emplace_back(std::move(tile_selection_panel));

    _panels.emplace_back(std::make_unique<MapInfoPanel>(_map.get()));

    auto selected_object_panel = std::make_shared<SelectedObjectPanel>();
    objectSelected.connect_member(selected_object_panel.get(), &SelectedObjectPanel::selectObject);
    _panels.push_back(std::move(selected_object_panel));
    */
}

void EditorState::setUpMainMenu() {
    // TODO: Migrate to Qt6 - Main menu temporarily disabled
    /*
    auto main_menu = std::make_shared<MainMenuPanel>(_map.get(), _currentElevation);
    main_menu->menuNewMapClicked.connect_member(this, &EditorState::createNewMap);
    main_menu->menuSaveMapClicked.connect_member(this, &EditorState::saveMap);
    main_menu->menuLoadMapClicked.connect_member(this, &EditorState::openMap);
    main_menu->menuQuitMapClicked.connect_member(this, &EditorState::quit);

    main_menu->menuShowObjectsClicked.connect([this](const bool selected) {
        _showObjects = selected;
    });
    main_menu->menuShowCrittersClicked.connect([this](const bool selected) {
        _showCritters = selected;
    });
    main_menu->menuShowWallsClicked.connect([this](const bool selected) {
        _showWalls = selected;
    });
    main_menu->menuShowRoofsClicked.connect([this](const bool selected) {
        _showRoof = selected;
    });
    main_menu->menuShowScrollBlkClicked.connect([this](const bool selected) {
        _showScrollBlk = selected;
    });

    main_menu->menuElevationClicked.connect([this](int elevation) {
        _currentElevation = elevation;
        spdlog::info("Loading elevation " + std::to_string(_currentElevation));

        auto loading_state = std::make_unique<LoadingState>(_appData);
        loading_state->addLoader(std::make_unique<MapLoader>(_map->path(), _currentElevation, [&](std::unique_ptr<Map> map) {
            _map = std::move(map);
            init();
            _appData->stateMachine->pop();
        }));

        _appData->stateMachine->push(std::move(loading_state));
    });

    _panels.emplace_back(std::move(main_menu));
    */
}

void EditorState::init() {
    loadSprites();
    setUpPanels();
    
    // Connect Qt6 menus to EditorState functionality
    if (_appData->mainWindow) {
        _appData->mainWindow->connectToEditorState();
    }
}

void EditorState::saveMap() {

    auto destination = geck::QtDialogs::saveFile("Select a file", ".",
        { {"Map Files", "*.map"} },
        true);

    MapWriter map_writer{ [](int32_t PID) {
                             ProReader pro_reader{};
                             return ResourceManager::getInstance().loadResource(ProHelper::basePath(PID), pro_reader);
                         } };

    map_writer.openFile(destination);
    if (map_writer.write(_map->getMapFile())) {
        spdlog::info("Saved map {}", destination);
    } else {
        spdlog::error("Failed to save map {}", destination);
    }
}

void EditorState::openMap() {
    // Show file dialog on main thread BEFORE creating the background loader
    auto mapPath = geck::QtDialogs::openFile("Choose Fallout 2 map to load", "",
        { {"Fallout 2 map (.map)", "*.map"} });

    if (mapPath.empty()) {
        spdlog::info("No map file selected");
        return;
    }

    auto loading_state = std::make_unique<LoadingState>(_appData);
    loading_state->addLoader(std::make_unique<MapLoader>(mapPath, -1, [&](auto map) {
        _appData->stateMachine->push(std::make_unique<EditorState>(_appData, std::move(map)), true);
    }));

    _appData->stateMachine->push(std::move(loading_state));
}

void EditorState::loadObjectSprites() {
    // Objects
    if (_map->objects().empty())
        return;

    for (const auto& object : _map->objects().at(_currentElevation)) {
        if (object->position == -1)
            continue; // object inside an inventory/container

        const std::string frm_name = ResourceManager::getInstance().FIDtoFrmName(object->frm_pid);

        spdlog::debug("Loading sprite {}", frm_name);

        const auto& frm = ResourceManager::getInstance().getResource<Frm>(frm_name);

        // TODO: use _objects.insert(v.begin(), Object{ frm }); for flat objects
        //       which should be rendered first

        _objects.emplace_back(std::make_shared<Object>(frm));
        sf::Sprite object_sprite{ ResourceManager::getInstance().texture(frm_name) };
        _objects.back()->setSprite(std::move(object_sprite));
        _objects.back()->setHexPosition(_hexgrid.grid().at(object->position));
        _objects.back()->setMapObject(object);
        _objects.back()->setDirection(object->direction);
    }
}

// Tiles
// TODO: create tile atlas like in Falltergeist Tilemap.cpp
void EditorState::loadTileSprites() {
    const auto& lst = ResourceManager::getInstance().getResource<Lst, std::string>("art/tiles/tiles.lst");

    for (auto tileNumber = 0U; tileNumber < Map::TILES_PER_ELEVATION; ++tileNumber) {
        auto tile = _map->getMapFile().tiles.at(_currentElevation).at(tileNumber);

        // Positioning

        // geometry constants
        // const TILE_WIDTH = 80
        // const TILE_HEIGHT = 36
        // const HEX_GRID_SIZE = 200 // hex grid is 200x200 (roof+floor)

        unsigned int tileX = static_cast<unsigned>(ceil(((double)tileNumber) / 100));
        unsigned int tileY = tileNumber % 100;
        unsigned int x = (100 - tileY - 1) * 48 + 32 * (tileX - 1);
        unsigned int y = tileX * 24 + (tileY - 1) * 12 + 1;

        const auto& createTileSprite = [&](const uint16_t tile_id, int offset = 0) {
            sf::Sprite tile_sprite(ResourceManager::getInstance().texture("art/tiles/" + lst->at(tile_id)));
            tile_sprite.setPosition(x, y - offset);
            return tile_sprite;
        };

        // floor
        uint16_t floorId = tile.getFloor();
        if (floorId == Map::EMPTY_TILE) {
            sf::Sprite tile_sprite(ResourceManager::getInstance().texture("art/tiles/blank.frm"));
            tile_sprite.setPosition(x, y);
            _floorSprites[tileNumber] = tile_sprite;
        } else {
            _floorSprites[tileNumber] = createTileSprite(floorId);
        }

        // roof
        _roofSprites[tileNumber] = createTileSprite(tile.getRoof(), Tile::ROOF_OFFSET);
    }
}

void EditorState::loadSprites() {

    spdlog::stopwatch sw;

    _appData->window->setTitle(_map->filename() + " - Gecko");

    _objects.clear();

    // FIXME: causes stack overflow on Windows ?! It is probably not needed anyway because all elements get overwritten on map-reload
    //_floorSprites = {};
    //_roofSprites = {};

    // Data
    loadTileSprites();
    loadObjectSprites();

    spdlog::info("Map sprites loaded in {:.3} seconds", sw);
}

std::vector<bool> EditorState::calculateBitset(const sf::Image& img) {
    sf::Vector2u imgSize = img.getSize();

    std::vector<bool> retVal(imgSize.x * imgSize.y); // allocate directly

    for (unsigned int x = 0; x < imgSize.x; ++x)     // unsigned,  pre-increment (not crucial
        for (unsigned int y = 0; y < imgSize.y; ++y) // here, but generally good practice)
        {
            const auto& pixel = img.getPixel(x, y);
            retVal[imgSize.x * y + x] = (pixel.r == 0 && pixel.g == 0 && pixel.b == 0 && pixel.a == 0);
        }

    return retVal;
}

bool EditorState::selectObject(sf::Vector2f worldPos) {

    // TODO: use ranges ?
    // filter and then max_element using the position property
    std::optional<std::shared_ptr<Object>> newly_selected_object;

    // we are moving selected object to a different location
    if (_selectedObject && !isSpriteClicked(worldPos, _selectedObject->get()->getSprite())) {
        return false;
    }

    // we are clicking on the same area where the selected object is located, so select the one behind or unselect it
    for (int i = 0; i < _objects.size(); i++) {
        auto& iterated_object = _objects.at(i);

        if (isSpriteClicked(worldPos, iterated_object->getSprite())) {

            if (iterated_object->isSelected()) {
                unselectObject();
                break;
            }

            if (newly_selected_object) {

                const int position1 = newly_selected_object->get()->getMapObject().position;
                const int position2 = iterated_object->getMapObject().position;

                // select the foremost object
                if (position1 < position2) {
                    newly_selected_object = iterated_object;
                }
            } else {
                newly_selected_object = iterated_object;
            }
        }
    }
    if (newly_selected_object) {
        unselectAll();

        _selectedObject = newly_selected_object;
        _selectedObject->get()->select();
        objectSelected(_selectedObject.value());

        // TODO: show in the panel

        return true;
    }

    return false;
}

bool EditorState::selectFloorTile(sf::Vector2f worldPos) {
    return selectTile(worldPos, _floorSprites, _selectedFloorTileIndexes, false);
}

bool EditorState::selectRoofTile(sf::Vector2f worldPos) {

    return selectTile(worldPos, _roofSprites, _selectedRoofTileIndexes, true);
}

bool EditorState::selectTile(sf::Vector2f worldPos, std::array<sf::Sprite, Map::TILES_PER_ELEVATION>& sprites, std::vector<int>& selectedIndexes, bool selectingRoof) {

    for (int i = 0; i < Map::TILES_PER_ELEVATION; i++) {

        auto& tile = sprites.at(i);

        _fakeTileSprite.setPosition(tile.getPosition());

        if (isSpriteClicked(worldPos, _fakeTileSprite)) {
            if (selectingRoof && _map->getMapFile().tiles.at(_currentElevation).at(i).getRoof() == Map::EMPTY_TILE) {
                return false;
            }
            if (std::count(selectedIndexes.begin(), selectedIndexes.end(), i)) {

                tile.setColor(sf::Color::White);
                selectedIndexes.erase(std::remove(selectedIndexes.begin(), selectedIndexes.end(), i));
                return false;

            } else {
                tile.setColor(sf::Color::Red);
                selectedIndexes.push_back(i);

                unselectObject();
                return true;
            }
        }
    }

    return false;
}

bool EditorState::isSpriteClicked(const sf::Vector2f& worldPos, const sf::Sprite& sprite) {
    if (sprite.getGlobalBounds().contains(worldPos)) {

        const auto& image = sprite.getTexture()->copyToImage();
        const auto& bitsets = calculateBitset(image);

        const auto& x = worldPos.x - sprite.getGlobalBounds().left;
        const auto& y = worldPos.y - sprite.getGlobalBounds().top;

        int w = image.getSize().x;
        if (!bitsets[w * y + x]) {
            return true;
        }
    }
    return false;
}

void EditorState::handleEvent(const sf::Event& event) {
    if (event.type == sf::Event::KeyPressed) {
        switch (event.key.code) {
            case sf::Keyboard::N: // Ctrl+N
                if (event.key.control)
                    createNewMap();
                break;
            case sf::Keyboard::Q: // Ctrl+Q
                if (event.key.control)
                    quit();
                break;
            case sf::Keyboard::S: // Ctrl+S
                if (event.key.control)
                    saveMap();
                break;
            case sf::Keyboard::O: // Ctrl+O
                if (event.key.control)
                    openMap();
                break;
            case sf::Keyboard::R: // R
                if (_selectedObject) {
                    _selectedObject->get()->rotate();
                }
                break;
            case sf::Keyboard::Escape:
                quit();
                break;
            case sf::Keyboard::Left:
                _view.move(-50.f, 0.f);
                break;
            case sf::Keyboard::Right:
                _view.move(50.f, 0.f);
                break;
            case sf::Keyboard::Up:
                _view.move(0.f, -50.f);
                break;
            case sf::Keyboard::Down:
                _view.move(0.f, 50.f);
                break;
            default:
                break;
        }
    }

    // Zoom
    if (event.type == sf::Event::MouseWheelScrolled && event.mouseWheelScroll.wheel == sf::Mouse::Wheel::VerticalWheel) {
        float delta = event.mouseWheelScroll.delta;
        _view.zoom(1.0f - delta * 0.05f);
    }

    // Left mouse click
    if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Button::Left) {

        sf::Vector2i mouse_pos = sf::Mouse::getPosition(*_appData->window);

        // convert it to world coordinates
        sf::Vector2f world_pos = _appData->window->mapPixelToCoords(mouse_pos);

        bool object_already_selected = _selectedObject.has_value(); // some object was already selected

        // FIXME: disable selecting objects through roof
        bool roof_selected = false;
        if (!object_already_selected && _showRoof && (_currentSelectionMode == SelectionMode::ALL || _currentSelectionMode == SelectionMode::ROOF_TILES)) {
            roof_selected = selectRoofTile(world_pos);
        }

        if (!roof_selected && _currentSelectionMode != SelectionMode::ROOF_TILES) {
            bool moving_object = false;
            if (_currentSelectionMode != SelectionMode::ALL && _currentSelectionMode != SelectionMode::OBJECTS) {
                unselectObject();
            } else {
                bool selected_different_object = selectObject(world_pos); // another object was selected instead

                // still have the original selected and clicked on an empty space
                moving_object = object_already_selected && !selected_different_object && _selectedObject.has_value();

                auto position_clicked = _hexgrid.positionAt(world_pos.x, world_pos.y);
                if (moving_object && position_clicked != Hex::HEX_OUT_OF_MAP) {
                    _selectedObject->get()->setHexPosition(_hexgrid.grid().at(position_clicked));

                    std::stable_sort(_objects.begin(), _objects.end(), [](const auto obj1, const auto obj2) {
                        return obj1->getMapObject().position < obj2->getMapObject().position;
                    });
                }
            }

            if (!_selectedObject && !moving_object && !object_already_selected) {
                if (_currentSelectionMode == SelectionMode::ALL || _currentSelectionMode == SelectionMode::FLOOR_TILES) {
                    selectFloorTile(world_pos);
                }
            }
        }
    }

    if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Button::Right) {

        // Initialize panning
        _mouseStartingPosition = sf::Mouse::getPosition(*_appData->window);
        _mouseLastPosition = _mouseStartingPosition;
    }

    if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Right)) {

        if (_currentAction == EditorAction::PANNING) {

            sf::Vector2f mousePositionDiff = sf::Vector2f{ sf::Mouse::getPosition(*_appData->window) - _mouseLastPosition };
            _view.move(-1.0f * mousePositionDiff);

        } else {
            constexpr int panningThreshold = 5;

            int mouseMovedX = std::abs(_mouseStartingPosition.x - _mouseLastPosition.x);
            int mouseMovedY = std::abs(_mouseStartingPosition.y - _mouseLastPosition.y);

            if (mouseMovedX > panningThreshold || mouseMovedY > panningThreshold) {
                _currentAction = EditorAction::PANNING;
                if (_cursor.loadFromSystem(sf::Cursor::SizeAll)) {
                    _appData->window->setMouseCursor(_cursor);
                }
            }
        }
        _mouseLastPosition = sf::Mouse::getPosition(*_appData->window);
    }

    if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Button::Right) {

        if (_currentAction != EditorAction::PANNING) {
            unselectAll();
        }

        _currentAction = EditorAction::NONE;

        if (_cursor.loadFromSystem(sf::Cursor::Arrow)) {
            _appData->window->setMouseCursor(_cursor);
        }
    }

    // Window resizing
    if (event.type == sf::Event::Resized) {
        // Update the view size while preserving center and zoom
        sf::Vector2f currentCenter = _view.getCenter();
        sf::Vector2f currentSize = _view.getSize();
        
        // Calculate the aspect ratio change to maintain proper scaling
        float newWidth = static_cast<float>(event.size.width);
        float newHeight = static_cast<float>(event.size.height);
        
        spdlog::debug("EditorState resize: {}x{} -> {}x{}", 
                      currentSize.x, currentSize.y, newWidth, newHeight);
        
        // Update view size to match new window dimensions
        _view.setSize(newWidth, newHeight);
        
        // Keep the same center point
        _view.setCenter(currentCenter);
    }
}

void EditorState::update(const float dt) { }

void EditorState::unselectAll() {
    unselectObject();
    unselectTiles();
}

void EditorState::unselectTiles() {
    // clear selected tiles
    for (int tile_index : _selectedFloorTileIndexes) {
        _floorSprites.at(tile_index).setColor(sf::Color::White);
    }
    for (int tile_index : _selectedRoofTileIndexes) {
        _roofSprites.at(tile_index).setColor(sf::Color::White);
    }
    _selectedFloorTileIndexes.clear();
    _selectedRoofTileIndexes.clear();
}

void EditorState::unselectObject() {
    if (_selectedObject) {
        _selectedObject->get()->unselect();
        _selectedObject = {};
        objectSelected(nullptr);
    }
}

void EditorState::render(const float dt) {

    _appData->window->setView(_view);

    // TODO: Migrate to Qt6 - UI panels temporarily disabled
    /*
    for (auto& panel : _panels) {
        panel->render(dt);
    }
    */

    for (const auto& floor : _floorSprites) {
        _appData->window->draw(floor);
    }

    if (_showObjects) {
        for (const auto& object : _objects) {
            if (!_showScrollBlk && object->getMapObject().isBlocker()) {
                continue;
            }
            _appData->window->draw(object->getSprite());
        }
    }

    if (_showRoof) {
        for (const auto& roof : _roofSprites) {
            _appData->window->draw(roof);
        }
    }
}

void EditorState::centerViewOnMap() {
    constexpr float center_x = Tile::TILE_WIDTH * 50;
    constexpr float center_y = Tile::TILE_HEIGHT * 50;
    _view.setCenter(center_x, center_y);
}

void EditorState::createNewMap() {
    int elevations = 1;
    int floorTileIndex = 192; // edg5000.frm
    int roofTileIndex = 1;    // grid000.frm

    _map = std::make_unique<Map>("test.map");
    auto map_file = std::make_unique<Map::MapFile>();
    _map->setMapFile(std::move(map_file));

    LstReader lst_reader;
    auto lst = ResourceManager::getInstance().loadResource("art/tiles/tiles.lst", lst_reader);

    std::string texture_path = "art/tiles/" + lst->at(floorTileIndex);
    ResourceManager::getInstance().insertTexture(texture_path);

    texture_path = "art/tiles/" + lst->at(roofTileIndex);
    ResourceManager::getInstance().insertTexture(texture_path);

    std::map<int, std::vector<Tile>> tiles;
    for (auto elevation = 0; elevation < elevations; ++elevation) {
        for (auto i = 0U; i < Map::TILES_PER_ELEVATION; ++i) {
            uint16_t roof = roofTileIndex;
            uint16_t floor = floorTileIndex;

            tiles[elevation].emplace_back(floor, roof);
        }
    }
    _map->getMapFile().tiles = tiles;

    loadSprites();
}

void EditorState::quit() {
    State::quit();
}

void EditorState::cycleSelectionMode() {
    _currentSelectionMode = static_cast<SelectionMode>((static_cast<int>(_currentSelectionMode) + 1) % static_cast<int>(SelectionMode::NUM_SELECTION_TYPES));
    
    // Log the new selection mode for debugging
    std::string modeStr;
    switch(_currentSelectionMode) {
        case SelectionMode::OBJECTS: modeStr = "Objects"; break;
        case SelectionMode::FLOOR_TILES: modeStr = "Floor Tiles"; break;
        case SelectionMode::ROOF_TILES: modeStr = "Roof Tiles"; break;
        case SelectionMode::ALL: 
        default: modeStr = "All"; break;
    }
    spdlog::info("Selection mode changed to: {}", modeStr);
}

void EditorState::rotateSelectedObject() {
    if (_selectedObject) {
        _selectedObject->get()->rotate();
        spdlog::info("Rotated selected object");
    } else {
        spdlog::info("No object selected to rotate");
    }
}

void EditorState::changeElevation(int elevation) {
    if (elevation == _currentElevation) {
        return; // Already on this elevation
    }
    
    spdlog::info("Changing elevation from {} to {}", _currentElevation, elevation);
    
    auto loading_state = std::make_unique<LoadingState>(_appData);
    loading_state->addLoader(std::make_unique<MapLoader>(_map->path(), elevation, [&](std::unique_ptr<Map> map) {
        _map = std::move(map);
        _currentElevation = elevation;
        init();
        _appData->stateMachine->pop();
    }));
    
    _appData->stateMachine->push(std::move(loading_state));
}

} // namespace geck
