#define QT_NO_EMIT
#include "EditorState.h"


#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include <cmath> // ceil, sqrt, pow
#include <algorithm> // std::sort, std::find
#include "../util/QtDialogs.h"
#include "../ui/MainWindow.h"


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
    , _map(std::move(map)) {
    centerViewOnMap();
}




void EditorState::init() {
    loadSprites();
    
    // Connect Qt6 menus to EditorState functionality
    if (_appData->mainWindow) {
        _appData->mainWindow->connectToEditorState();
    }
    
    // Ensure view is properly sized to match current window
    if (_appData->window) {
        sf::Vector2u windowSize = _appData->window->getSize();
        if (windowSize.x > 0 && windowSize.y > 0) {
            _view.setSize(static_cast<float>(windowSize.x), static_cast<float>(windowSize.y));
            spdlog::debug("EditorState::init() - Set initial view size to {}x{}", windowSize.x, windowSize.y);
        }
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

// New improved object selection methods
std::vector<std::shared_ptr<Object>> EditorState::getObjectsAtPosition(sf::Vector2f worldPos) {
    std::vector<std::shared_ptr<Object>> objectsAtPos;
    
    // Check all objects for collision at the world position
    for (auto& object : _objects) {
        if (isPointInSpritePixel(worldPos, object->getSprite())) {
            objectsAtPos.push_back(object);
        }
    }
    
    // Sort by map position (z-order) - higher positions are "in front"
    // For objects with same position, prioritize by object type (scenery > wall > others)
    std::sort(objectsAtPos.begin(), objectsAtPos.end(), 
        [](const std::shared_ptr<Object>& a, const std::shared_ptr<Object>& b) {
            auto posA = a->getMapObject().position;
            auto posB = b->getMapObject().position;
            
            if (posA != posB) {
                return posA > posB;  // Higher position = front
            }
            
            // Same position - use object type priority
            auto getTypePriority = [](uint32_t pid) -> int {
                unsigned int typeId = pid >> 24;
                switch (static_cast<Pro::OBJECT_TYPE>(typeId)) {
                    case Pro::OBJECT_TYPE::SCENERY: return 3;  // Highest priority
                    case Pro::OBJECT_TYPE::WALL:    return 2;
                    case Pro::OBJECT_TYPE::ITEM:    return 1;
                    case Pro::OBJECT_TYPE::CRITTER: return 1;
                    case Pro::OBJECT_TYPE::TILE:    return 1;
                    case Pro::OBJECT_TYPE::MISC:    return 1;
                    default: return 0;
                }
            };
            
            return getTypePriority(a->getMapObject().pro_pid) > getTypePriority(b->getMapObject().pro_pid);
        });
    
    // Debug: Log object positions and PIDs for debugging
    if (objectsAtPos.size() > 1) {
        spdlog::debug("getObjectsAtPosition: Found {} overlapping objects:", objectsAtPos.size());
        for (size_t i = 0; i < objectsAtPos.size(); i++) {
            uint32_t pid = objectsAtPos[i]->getMapObject().pro_pid;
            unsigned int typeId = pid >> 24;
            const char* typeName = "UNKNOWN";
            switch (static_cast<Pro::OBJECT_TYPE>(typeId)) {
                case Pro::OBJECT_TYPE::ITEM:    typeName = "ITEM"; break;
                case Pro::OBJECT_TYPE::CRITTER: typeName = "CRITTER"; break;
                case Pro::OBJECT_TYPE::SCENERY: typeName = "SCENERY"; break;
                case Pro::OBJECT_TYPE::WALL:    typeName = "WALL"; break;
                case Pro::OBJECT_TYPE::TILE:    typeName = "TILE"; break;
                case Pro::OBJECT_TYPE::MISC:    typeName = "MISC"; break;
            }
            spdlog::debug("  [{}] PID: {}, Position: {}, Type: {}", i, 
                         pid, objectsAtPos[i]->getMapObject().position, typeName);
        }
    }
    
    return objectsAtPos;
}

bool EditorState::isPointInSpriteBounds(sf::Vector2f worldPos, const sf::Sprite& sprite) {
    return sprite.getGlobalBounds().contains(worldPos);
}

bool EditorState::isPointInSpritePixel(sf::Vector2f worldPos, const sf::Sprite& sprite) {
    // First check if point is within sprite bounds
    if (!isPointInSpriteBounds(worldPos, sprite)) {
        return false;
    }
    
    // Get sprite bounds and texture
    const auto bounds = sprite.getGlobalBounds();
    const auto* texture = sprite.getTexture();
    if (!texture) {
        return false;
    }
    
    // Convert world position to sprite-local coordinates
    sf::Vector2f localPos = worldPos - sf::Vector2f(bounds.left, bounds.top);
    
    // Apply sprite's texture rectangle and scale
    const auto textureRect = sprite.getTextureRect();
    const auto scale = sprite.getScale();
    
    // Convert to texture coordinates
    int texX = static_cast<int>((localPos.x / scale.x) + textureRect.left);
    int texY = static_cast<int>((localPos.y / scale.y) + textureRect.top);
    
    // Get texture size
    const auto texSize = texture->getSize();
    
    // Bounds check
    if (texX < 0 || texY < 0 || texX >= static_cast<int>(texSize.x) || texY >= static_cast<int>(texSize.y)) {
        return false;
    }
    
    // Get the image and check pixel transparency
    const auto image = texture->copyToImage();
    const auto pixel = image.getPixel(texX, texY);
    
    bool isHit = pixel.a > 0;
    if (isHit) {
        spdlog::debug("Hit detected: world({:.2f},{:.2f}) -> local({:.2f},{:.2f}) -> tex({},{}) alpha={}", 
                     worldPos.x, worldPos.y, localPos.x, localPos.y, texX, texY, pixel.a);
    }
    
    // Return true if pixel is not fully transparent
    return isHit;
}

bool EditorState::isDoubleClick(sf::Vector2f worldPos) {
    float timeSinceLastClick = _lastClickTime.getElapsedTime().asSeconds();
    float distance = std::sqrt(std::pow(worldPos.x - _lastClickPosition.x, 2) + 
                              std::pow(worldPos.y - _lastClickPosition.y, 2));
    
    bool isDouble = (timeSinceLastClick < DOUBLE_CLICK_TIME) && (distance < DOUBLE_CLICK_DISTANCE);
    
    // Update last click info
    _lastClickTime.restart();
    _lastClickPosition = worldPos;
    
    return isDouble;
}

void EditorState::cycleObjectsAtPosition(sf::Vector2f worldPos) {
    auto objectsAtPos = getObjectsAtPosition(worldPos);
    
    if (objectsAtPos.size() <= 1) {
        return; // Nothing to cycle
    }
    
    if (!_selectedObject) {
        return; // No current selection to cycle from
    }
    
    // Find current object in the list
    auto currentIt = std::find(objectsAtPos.begin(), objectsAtPos.end(), _selectedObject.value());
    if (currentIt != objectsAtPos.end()) {
        // Move to next object in the list
        auto nextIt = std::next(currentIt);
        if (nextIt != objectsAtPos.end()) {
            spdlog::debug("cycleObjectsAtPosition: Cycling to next object");
            unselectObject();
            (*nextIt)->select();
            _selectedObject = *nextIt;
            objectSelected(_selectedObject.value());
        } else {
            // Wrap around to the first object
            spdlog::debug("cycleObjectsAtPosition: Wrapping to first object");
            unselectObject();
            objectsAtPos[0]->select();
            _selectedObject = objectsAtPos[0];
            objectSelected(_selectedObject.value());
        }
    }
}

bool EditorState::selectObject(sf::Vector2f worldPos) {
    // Get all objects at the clicked position, sorted by z-order (front to back)
    auto objectsAtPos = getObjectsAtPosition(worldPos);
    
    spdlog::debug("selectObject: Found {} objects at position ({:.2f}, {:.2f})", 
                 objectsAtPos.size(), worldPos.x, worldPos.y);
    
    if (objectsAtPos.empty()) {
        // No objects at this position
        spdlog::debug("selectObject: No objects found at position");
        return false;
    }
    
    // If we have multiple objects and currently have a selected object at this position, cycle to next
    if (objectsAtPos.size() > 1 && _selectedObject) {
        auto currentIt = std::find(objectsAtPos.begin(), objectsAtPos.end(), _selectedObject.value());
        if (currentIt != objectsAtPos.end()) {
            // The currently selected object is at this position, so cycle to the next one
            spdlog::debug("selectObject: Clicking on position with currently selected object, cycling to next object");
            cycleObjectsAtPosition(worldPos);
            _lastClickPosition = worldPos;
            _lastClickTime.restart();
            return true;
        }
    }
    
    // Select the frontmost object at this position
    spdlog::debug("selectObject: Selecting frontmost object");
    
    // Clear any existing object selection
    unselectObject();
    
    // Only clear tiles if not in ALL mode (to allow mixed selection)
    //if (_currentSelectionMode != SelectionMode::ALL) {
        unselectTiles();
    //}
    
    objectsAtPos[0]->select();
    _selectedObject = objectsAtPos[0];
    objectSelected(_selectedObject.value());
    
    // Update click tracking for cycling
    _lastClickPosition = worldPos;
    _lastClickTime.restart();
    return true;
}

bool EditorState::selectFloorTile(sf::Vector2f worldPos) {
    return selectTile(worldPos, _floorSprites, _selectedFloorTileIndexes, false);
}

bool EditorState::selectRoofTile(sf::Vector2f worldPos) {

    return selectTile(worldPos, _roofSprites, _selectedRoofTileIndexes, true);
}

bool EditorState::selectTile(sf::Vector2f worldPos, std::array<sf::Sprite, Map::TILES_PER_ELEVATION>& sprites, std::vector<int>& selectedIndexes, bool selectingRoof) {
    // First, check all tiles to find the one(s) that contain the click point
    std::vector<int> candidateTiles;
    
    for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); i++) {
        auto& tile = sprites.at(i);
        
        // Use bounds check first for efficiency
        if (isPointInSpriteBounds(worldPos, tile)) {
            candidateTiles.push_back(i);
        }
    }
    
    // Now check pixel-perfect collision only on candidate tiles
    for (int i : candidateTiles) {
        auto& tile = sprites.at(i);
        
        if (isPointInSpritePixel(worldPos, tile)) {
            if (selectingRoof && _map->getMapFile().tiles.at(_currentElevation).at(i).getRoof() == Map::EMPTY_TILE) {
                continue; // Skip empty roof tiles, try next candidate
            }
            
            // Check if this tile is already selected
            if (std::count(selectedIndexes.begin(), selectedIndexes.end(), i)) {
                // Deselect this tile
                tile.setColor(sf::Color::White);
                selectedIndexes.erase(std::remove(selectedIndexes.begin(), selectedIndexes.end(), i));
                spdlog::debug("selectTile: Deselected tile {}", i);
                return false;
            } else {
                // Clear all previously selected tiles before selecting the new one
                for (int prevIndex : selectedIndexes) {
                    sprites.at(prevIndex).setColor(sf::Color::White);
                }
                selectedIndexes.clear();
                
                // Clear any existing object selection (mutual exclusion)
                unselectObject();
                
                // Select the new tile
                tile.setColor(sf::Color::Red);
                selectedIndexes.push_back(i);
                spdlog::debug("selectTile: Selected tile {}", i);
                
                // Emit signal for tile selection
                tileSelected(i, _currentElevation, selectingRoof);
                return true;
            }
        }
    }

    return false;
}

bool EditorState::selectAtPosition(sf::Vector2f worldPos) {
    // Check what types of selectable items are available at this position
    auto objectsAtPos = getObjectsAtPosition(worldPos);
    bool hasObjects = !objectsAtPos.empty();
    
    // Check if there are tiles at this position
    bool hasRoofTile = false;
    bool hasFloorTile = false;
    
    // Quick check for tiles by looking at bounds
    for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); i++) {
        if (isPointInSpriteBounds(worldPos, _roofSprites.at(i))) {
            if (_map->getMapFile().tiles.at(_currentElevation).at(i).getRoof() != Map::EMPTY_TILE) {
                hasRoofTile = true;
            }
        }
        if (isPointInSpriteBounds(worldPos, _floorSprites.at(i))) {
            hasFloorTile = true;
        }
    }
    
    // Determine if this is a new position or a cycling click
    float distance = std::sqrt(std::pow(worldPos.x - _lastSelectionPosition.x, 2) + 
                              std::pow(worldPos.y - _lastSelectionPosition.y, 2));
    bool isSamePosition = distance < DOUBLE_CLICK_DISTANCE;
    
    if (!isSamePosition) {
        // New position - start with the highest priority item (roof tiles first)
        _lastSelectionPosition = worldPos;
        
        if (hasRoofTile) {
            unselectAll();
            _lastSelectionType = SelectionType::ROOF_TILE;
            return selectRoofTile(worldPos);
        } else if (hasObjects) {
            unselectTiles();
            _lastSelectionType = SelectionType::OBJECT;
            return selectObject(worldPos);
        } else if (hasFloorTile) {
            unselectAll();
            _lastSelectionType = SelectionType::FLOOR_TILE;
            return selectFloorTile(worldPos);
        }
        return false;
    } else {
        // Same position - cycle to next available type (Roof -> Objects -> Floor -> Roof)
        switch (_lastSelectionType) {
            case SelectionType::ROOF_TILE:
                if (hasObjects) {
                    _lastSelectionType = SelectionType::OBJECT;
                    return selectObject(worldPos);
                } else if (hasFloorTile) {
                    unselectAll();
                    _lastSelectionType = SelectionType::FLOOR_TILE;
                    return selectFloorTile(worldPos);
                } else if (hasRoofTile) {
                    unselectAll();
                    // Stay on roof tile
                    return selectRoofTile(worldPos);
                }
                break;
                
            case SelectionType::OBJECT:
                if (hasFloorTile) {
                    unselectAll();
                    _lastSelectionType = SelectionType::FLOOR_TILE;
                    return selectFloorTile(worldPos);
                } else if (hasRoofTile) {
                    unselectAll();
                    _lastSelectionType = SelectionType::ROOF_TILE;
                    return selectRoofTile(worldPos);
                } else if (hasObjects) {
                    // Stay on objects
                    return selectObject(worldPos);
                }
                break;
                
            case SelectionType::FLOOR_TILE:
                if (hasRoofTile) {
                    unselectAll();
                    _lastSelectionType = SelectionType::ROOF_TILE;
                    return selectRoofTile(worldPos);
                } else if (hasObjects) {
                    _lastSelectionType = SelectionType::OBJECT;
                    return selectObject(worldPos);
                } else if (hasFloorTile) {
                    unselectAll();
                    // Stay on floor tile
                    return selectFloorTile(worldPos);
                }
                break;
        }
        return false;
    }
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

        // Use the mouse coordinates from the event directly (already in widget coordinates)
        sf::Vector2i mouse_pos(event.mouseButton.x, event.mouseButton.y);

        // convert it to world coordinates using the current view
        sf::Vector2f world_pos = _appData->window->mapPixelToCoords(mouse_pos, _view);

        spdlog::debug("Mouse click at widget coords: ({}, {}), world coords: ({:.2f}, {:.2f})", 
                     mouse_pos.x, mouse_pos.y, world_pos.x, world_pos.y);
        
        // Selection logic based on mode
        switch (_currentSelectionMode) {
            case SelectionMode::ALL: {
                spdlog::info("SelectionMode::ALL");
                // In ALL mode, allow cycling between objects, roof tiles, and floor tiles at the same position
                selectAtPosition(world_pos);
                break;
            }
                
            case SelectionMode::OBJECTS:
                spdlog::info("SelectionMode::OBJECTS");
                // Only select objects - clear everything else first
                unselectTiles();
                unselectObject();
                selectObject(world_pos);
                break;
                
            case SelectionMode::ROOF_TILES:
                spdlog::info("SelectionMode::ROOF_TILES");
                // Only select roof tiles - clear everything else first
                unselectObject();
                unselectTiles();
                selectRoofTile(world_pos);
                break;
                
            case SelectionMode::FLOOR_TILES:
                spdlog::info("SelectionMode::FLOOR_TILES");
                // Only select floor tiles - clear everything else first
                unselectObject();
                unselectTiles();
                selectFloorTile(world_pos);
                break;
                
            default:
                break;
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
    
    // Emit signal for tile deselection
    tileSelectionCleared();
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
    spdlog::info("Selection mode changed to: {} (mode = {})", 
                 selectionModeToString(_currentSelectionMode), 
                 static_cast<int>(_currentSelectionMode));
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
