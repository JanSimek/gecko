#include "EditorWidget.h"
#include "SFMLWidget.h"

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include <cmath> // ceil, sqrt, pow
#include <algorithm> // std::sort, std::find, std::max, std::min
#include "../util/QtDialogs.h"

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

#include "../util/ProHelper.h"
#include "../reader/msg/MsgReader.h"

namespace geck {

EditorWidget::EditorWidget(std::unique_ptr<Map> map, QWidget* parent)
    : QWidget(parent)
    , _layout(nullptr)
    , _sfmlWidget(nullptr)
    , _view({ 0.f, 0.f }, sf::Vector2f(800.f, 600.f)) // Default size, will be updated on first resize
    , _map(std::move(map)) {
    
    setupUI();
    centerViewOnMap();
}

EditorWidget::~EditorWidget() {
    // Qt will handle cleanup of child widgets automatically
}

void EditorWidget::setupUI() {
    _layout = new QVBoxLayout(this);
    _layout->setContentsMargins(0, 0, 0, 0);
    
    // Create the SFML widget that will handle rendering
    _sfmlWidget = new SFMLWidget(this);
    
    // Set this EditorWidget as the delegate for SFML event handling
    _sfmlWidget->setEditorWidget(this);
    
    _layout->addWidget(_sfmlWidget);
    
    setLayout(_layout);
}

void EditorWidget::init() {
    loadSprites();
    
    // Initialize fake tile sprite for hit detection
    _fakeTileSprite.setTexture(ResourceManager::getInstance().texture("art/tiles/blank.frm"));
    
    // Ensure view is properly sized to match current window
    if (_sfmlWidget && _sfmlWidget->getRenderWindow()) {
        sf::Vector2u windowSize = _sfmlWidget->getRenderWindow()->getSize();
        if (windowSize.x > 0 && windowSize.y > 0) {
            _view.setSize(static_cast<float>(windowSize.x), static_cast<float>(windowSize.y));
            spdlog::debug("EditorWidget::init() - Set initial view size to {}x{}", windowSize.x, windowSize.y);
        }
    }
    
    // Re-center view after setting size
    centerViewOnMap();
}

void EditorWidget::saveMap() {
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

void EditorWidget::openMap() {
    // Show file dialog to select a new map
    auto mapPath = geck::QtDialogs::openFile("Choose Fallout 2 map to load", "",
        { {"Fallout 2 map (.map)", "*.map"} });

    if (mapPath.empty()) {
        spdlog::info("No map file selected");
        return;
    }

    spdlog::info("User requested to open new map: {}", mapPath);
    
    // Emit signal to request map loading - MainWindow will handle the actual loading
    emit mapLoadRequested(mapPath);
}

void EditorWidget::createNewMap() {
    spdlog::info("Create new map functionality not yet implemented");
    // TODO: Implement new map creation
}

void EditorWidget::loadObjectSprites() {
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
void EditorWidget::loadTileSprites() {
    const auto& lst = ResourceManager::getInstance().getResource<Lst, std::string>("art/tiles/tiles.lst");

    for (auto tileNumber = 0U; tileNumber < Map::TILES_PER_ELEVATION; ++tileNumber) {
        auto tile = _map->getMapFile().tiles.at(_currentElevation).at(tileNumber);

        // Positioning

        // geometry constants
        // const TILE_WIDTH = 80
        // const TILE_HEIGHT = 36
        // const HEX_GRID_SIZE = 200 // hex grid is 200x200 (roof+floor)

        // Convert tile number to hex grid coordinates (matching Fallout 2 CE logic)
        unsigned int tileX = tileNumber / 100;  // Row in the grid
        unsigned int tileY = tileNumber % 100;  // Column in the grid
        
        // Convert to screen coordinates using isometric projection
        // This matches the positioning used in the original engine
        unsigned int x = (100 - tileY - 1) * 48 + 32 * tileX;
        unsigned int y = tileX * 24 + tileY * 12;

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

void EditorWidget::loadSprites() {
    spdlog::stopwatch sw;

    if (_sfmlWidget && _sfmlWidget->getRenderWindow()) {
        _sfmlWidget->getRenderWindow()->setTitle(_map->filename() + " - Gecko");
    }

    _objects.clear();

    // FIXME: causes stack overflow on Windows ?! It is probably not needed anyway because all elements get overwritten on map-reload
    //_floorSprites = {};
    //_roofSprites = {};

    // Data
    loadTileSprites();
    loadObjectSprites();

    spdlog::info("Map sprites loaded in {:.3} seconds", sw);
}

void EditorWidget::centerViewOnMap() {
    // Calculate map center based on how tiles are positioned in loadTileSprites()
    // The map is 100x100 tiles per elevation, positioned with this formula:
    // x = (100 - tileY - 1) * 48 + 32 * (tileX - 1)
    // y = tileX * 24 + (tileY - 1) * 12 + 1
    // Center should be around tile 50,50
    float centerX = (100 - 50 - 1) * 48 + 32 * (50 - 1);  // ≈ 3920
    float centerY = 50 * 24 + (50 - 1) * 12 + 1;          // ≈ 1789
    
    _view.setCenter(centerX, centerY);
    spdlog::debug("EditorWidget::centerViewOnMap() - Set view center to ({:.1f}, {:.1f})", centerX, centerY);
}

// New improved object selection methods
std::vector<std::shared_ptr<Object>> EditorWidget::getObjectsAtPosition(sf::Vector2f worldPos) {
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

bool EditorWidget::isPointInSpriteBounds(sf::Vector2f worldPos, const sf::Sprite& sprite) {
    return sprite.getGlobalBounds().contains(worldPos);
}

bool EditorWidget::isPointInSpritePixel(sf::Vector2f worldPos, const sf::Sprite& sprite) {
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

bool EditorWidget::isDoubleClick(sf::Vector2f worldPos) {
    float timeSinceLastClick = _lastClickTime.getElapsedTime().asSeconds();
    float distance = std::sqrt(std::pow(worldPos.x - _lastClickPosition.x, 2) + 
                              std::pow(worldPos.y - _lastClickPosition.y, 2));
    
    bool isDouble = (timeSinceLastClick < DOUBLE_CLICK_TIME) && (distance < DOUBLE_CLICK_DISTANCE);
    
    // Update last click info
    _lastClickTime.restart();
    _lastClickPosition = worldPos;
    
    return isDouble;
}

void EditorWidget::cycleObjectsAtPosition(sf::Vector2f worldPos) {
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
            emit objectSelected(_selectedObject.value());
        } else {
            // Wrap around to the first object
            spdlog::debug("cycleObjectsAtPosition: Wrapping to first object");
            unselectObject();
            objectsAtPos[0]->select();
            _selectedObject = objectsAtPos[0];
            emit objectSelected(_selectedObject.value());
        }
    }
}

void EditorWidget::unselectAll() {
    unselectObject();
    unselectTiles();
}

void EditorWidget::unselectTiles() {
    // Unselect floor tiles
    for (int index : _selectedFloorTileIndexes) {
        // Reset color to white (no tint)
        _floorSprites.at(index).setColor(sf::Color::White);
    }
    _selectedFloorTileIndexes.clear();

    // Unselect roof tiles  
    for (int index : _selectedRoofTileIndexes) {
        // Reset color to white (no tint)
        _roofSprites.at(index).setColor(sf::Color::White);
    }
    _selectedRoofTileIndexes.clear();
    
    emit tileSelectionCleared();
}

void EditorWidget::unselectObject() {
    if (_selectedObject) {
        _selectedObject.value()->unselect();
        _selectedObject.reset();
    }
}

void EditorWidget::zoomView(float direction) {
    // Ignore zero direction to prevent oscillation
    if (std::abs(direction) < 0.001f) {
        return;
    }
    
    // Calculate zoom factor directly for SFML
    // Positive direction = zoom in (smaller zoom factor)
    // Negative direction = zoom out (larger zoom factor)
    float zoomFactor;
    if (direction > 0) {
        zoomFactor = 1.0f - ZOOM_STEP;  // Zoom in
    } else {
        zoomFactor = 1.0f + ZOOM_STEP;  // Zoom out
    }
    
    // Calculate what the new zoom level would be
    float newZoomLevel = _zoomLevel * zoomFactor;
    
    // Clamp to min/max zoom levels
    if (newZoomLevel < MIN_ZOOM || newZoomLevel > MAX_ZOOM) {
        return; // Don't zoom beyond limits
    }
    
    // Apply the zoom
    _view.zoom(zoomFactor);
    
    // Update our zoom level tracking
    _zoomLevel = newZoomLevel;
    
    spdlog::debug("Zoom level: {:.2f} (direction: {:.1f}, factor: {:.3f})", _zoomLevel, direction, zoomFactor);
}

// SFML Event handling interface (called by SFMLWidget)
void EditorWidget::handleEvent(const sf::Event& event) {
    // Handle SFML events here
    // This is called by the SFMLWidget when it receives events
    
    switch (event.type) {
        case sf::Event::MouseButtonPressed:
            if (event.mouseButton.button == sf::Mouse::Left) {
                sf::Vector2f worldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                    sf::Vector2i(event.mouseButton.x, event.mouseButton.y), _view);
                selectAtPosition(worldPos);
            } else if (event.mouseButton.button == sf::Mouse::Right) {
                // Start panning
                _currentAction = EditorAction::PANNING;
                _mouseStartingPosition = sf::Vector2i(event.mouseButton.x, event.mouseButton.y);
                _mouseLastPosition = _mouseStartingPosition;
            }
            break;
            
        case sf::Event::MouseButtonReleased:
            if (event.mouseButton.button == sf::Mouse::Right) {
                // Stop panning
                _currentAction = EditorAction::NONE;
            }
            break;
            
        case sf::Event::MouseMoved:
            if (_currentAction == EditorAction::PANNING) {
                // Calculate pan delta
                sf::Vector2i currentPos(event.mouseMove.x, event.mouseMove.y);
                sf::Vector2i delta = _mouseLastPosition - currentPos;
                
                // Pan the view
                _view.move(static_cast<float>(delta.x), static_cast<float>(delta.y));
                
                _mouseLastPosition = currentPos;
            }
            break;
            
        case sf::Event::MouseWheelScrolled:
            // Zoom in/out with limits and sensitivity control
            if (event.mouseWheelScroll.wheel == sf::Mouse::VerticalWheel) {
                // Use smaller steps for smoother trackpad experience
                zoomView(event.mouseWheelScroll.delta);
            }
            break;
            
        case sf::Event::KeyPressed:
            // Arrow key movement
            switch (event.key.code) {
                case sf::Keyboard::Left:
                case sf::Keyboard::A:
                    _view.move(-50.0f, 0.0f);
                    break;
                case sf::Keyboard::Right:
                case sf::Keyboard::D:
                    _view.move(50.0f, 0.0f);
                    break;
                case sf::Keyboard::Up:
                case sf::Keyboard::W:
                    _view.move(0.0f, -50.0f);
                    break;
                case sf::Keyboard::Down:
                case sf::Keyboard::S:
                    _view.move(0.0f, 50.0f);
                    break;
                case sf::Keyboard::Home:
                    // Reset to center and normal zoom
                    centerViewOnMap();
                    // Reset zoom to 1.0
                    if (_zoomLevel != 1.0f) {
                        float resetFactor = 1.0f / _zoomLevel;
                        _view.zoom(resetFactor);
                        _zoomLevel = 1.0f;
                        spdlog::debug("Reset zoom to 1.0");
                    }
                    break;
                default:
                    break;
            }
            break;
            
        case sf::Event::Resized:
            // Update view size when window is resized
            {
                float newWidth = static_cast<float>(event.size.width);
                float newHeight = static_cast<float>(event.size.height);
                
                // Preserve the current view center
                sf::Vector2f currentCenter = _view.getCenter();
                
                // Set the view to the new size at 1:1 scale
                _view.setSize(newWidth, newHeight);
                _view.setCenter(currentCenter);
                
                // Reapply the current zoom level by zooming from 1.0 to current level
                if (_zoomLevel != 1.0f) {
                    float zoomFactor = 1.0f / _zoomLevel;  // Factor to get to current zoom from 1.0
                    _view.zoom(zoomFactor);
                }
                
                spdlog::debug("EditorWidget: Window resized to {}x{}, zoom level: {:.2f}", 
                             newWidth, newHeight, _zoomLevel);
            }
            break;
            
        default:
            break;
    }
}

void EditorWidget::update(const float dt) {
    // Update game logic here
    // This is called by the SFMLWidget's update loop
}

void EditorWidget::render(const float dt) {
    // Render the game here
    // This is called by the SFMLWidget's render loop
    
    if (!_sfmlWidget || !_sfmlWidget->getRenderWindow()) {
        return;
    }
    
    auto* window = _sfmlWidget->getRenderWindow();
    window->setView(_view);
    
    // Render floor tiles (10,000 tiles per elevation)
    for (const auto& floor : _floorSprites) {
        window->draw(floor);
    }
    
    // Render objects with visibility filtering
    if (_showObjects) {
        for (const auto& object : _objects) {
            window->draw(object->getSprite());
        }
    }
    
    // Render roof tiles
    if (_showRoof) {
        for (const auto& roof : _roofSprites) {
            window->draw(roof);
        }
    }
}

bool EditorWidget::selectAtPosition(sf::Vector2f worldPos) {
    switch (_currentSelectionMode) {
        case SelectionMode::OBJECTS:
            return selectObject(worldPos);
            
        case SelectionMode::FLOOR_TILES:
            return selectFloorTile(worldPos);
            
        case SelectionMode::ROOF_TILES:
            return selectRoofTile(worldPos);
            
        case SelectionMode::ALL:
            return selectAllAtPosition(worldPos);
            
        default:
            return false;
    }
}

bool EditorWidget::selectAllAtPosition(sf::Vector2f worldPos) {
    // For ALL mode, we need to cycle between all selectable elements at this position
    // Check what's available at this position
    auto objectsAtPos = getObjectsAtPosition(worldPos);
    bool hasFloorTile = false;
    bool hasRoofTile = false;
    
    // Check if there are visible tiles at this position
    bool floorTileAtPos = false;
    bool roofTileAtPos = false;
    
    // Use the same logic as selectTile to detect if tiles are clickable
    for (int i = 0; i < Map::TILES_PER_ELEVATION; i++) {
        _fakeTileSprite.setPosition(_floorSprites.at(i).getPosition());
        if (isSpriteClicked(worldPos, _fakeTileSprite)) {
            floorTileAtPos = true;
            break;
        }
    }
    
    for (int i = 0; i < Map::TILES_PER_ELEVATION; i++) {
        _fakeTileSprite.setPosition(_roofSprites.at(i).getPosition());
        if (isSpriteClicked(worldPos, _fakeTileSprite) && 
            _map->getMapFile().tiles.at(_currentElevation).at(i).getRoof() != Map::EMPTY_TILE) {
            roofTileAtPos = true;
            break;
        }
    }
    
    hasFloorTile = floorTileAtPos;
    hasRoofTile = roofTileAtPos;
    
    // Build list of available element types in priority order: roof → objects → floor
    std::vector<SelectionType> availableTypes;
    if (hasRoofTile) availableTypes.push_back(SelectionType::ROOF_TILE);
    if (!objectsAtPos.empty()) availableTypes.push_back(SelectionType::OBJECT);
    if (hasFloorTile) availableTypes.push_back(SelectionType::FLOOR_TILE);
    
    if (availableTypes.empty()) {
        // No selectable elements at this position
        unselectAll();
        return false;
    }
    
    // Check for double-click to cycle through element types
    //if (isDoubleClick(worldPos) && availableTypes.size() > 1) {
    if ( availableTypes.size() > 1) {
        // Find current selection type and move to next
        auto currentIt = std::find(availableTypes.begin(), availableTypes.end(), _lastSelectionType);
        if (currentIt != availableTypes.end()) {
            auto nextIt = std::next(currentIt);
            if (nextIt != availableTypes.end()) {
                _lastSelectionType = *nextIt;
            } else {
                _lastSelectionType = availableTypes[0]; // Wrap around
            }
        } else {
            _lastSelectionType = availableTypes[0];
        }
    } else {
        // First click - prioritize objects, then roof tiles, then floor tiles
        _lastSelectionType = availableTypes[0];
    }
    
    // Select based on the determined type
    switch (_lastSelectionType) {
        case SelectionType::OBJECT:
            return selectObject(worldPos);
        case SelectionType::FLOOR_TILE:
            return selectFloorTile(worldPos);
        case SelectionType::ROOF_TILE:
            return selectRoofTile(worldPos);
        default:
            return false;
    }
}

bool EditorWidget::selectObject(sf::Vector2f worldPos) {
    auto objectsAtPos = getObjectsAtPosition(worldPos);
    
    if (objectsAtPos.empty()) {
        return false;
    }
    
    // Check for double-click to cycle through objects
    //if (isDoubleClick(worldPos) && objectsAtPos.size() > 1) {
    if (objectsAtPos.size() > 1) {
        cycleObjectsAtPosition(worldPos);
        return true;
    }
    
    // Select the first (topmost) object
    unselectAll();
    auto selectedObj = objectsAtPos[0];
    selectedObj->select();
    _selectedObject = selectedObj;
    emit objectSelected(_selectedObject.value());
    
    return true;
}

bool EditorWidget::selectFloorTile(sf::Vector2f worldPos) {
    return selectTile(worldPos, _floorSprites, _selectedFloorTileIndexes, false);
}

bool EditorWidget::selectRoofTile(sf::Vector2f worldPos) {
    return selectTile(worldPos, _roofSprites, _selectedRoofTileIndexes, true);
}

bool EditorWidget::selectTile(sf::Vector2f worldPos, std::array<sf::Sprite, Map::TILES_PER_ELEVATION>& sprites, std::vector<int>& selectedIndexes, bool selectingRoof) {
    for (int i = 0; i < Map::TILES_PER_ELEVATION; i++) {
        auto& tile = sprites.at(i);
        
        _fakeTileSprite.setPosition(tile.getPosition());
        
        if (isSpriteClicked(worldPos, _fakeTileSprite)) {
            if (selectingRoof && _map->getMapFile().tiles.at(_currentElevation).at(i).getRoof() == Map::EMPTY_TILE) {
                return false;
            }
            
            if (std::count(selectedIndexes.begin(), selectedIndexes.end(), i)) {
                // Tile is already selected - deselect it
                tile.setColor(sf::Color::White);
                selectedIndexes.erase(std::remove(selectedIndexes.begin(), selectedIndexes.end(), i));
                return false;
            } else {
                // Select the tile
                tile.setColor(sf::Color::Red);
                selectedIndexes.push_back(i);
                
                unselectObject();
                emit tileSelected(i, _currentElevation, selectingRoof);
                return true;
            }
        }
    }
    
    return false;
}

void EditorWidget::cycleSelectionMode() {
    // Cycle through selection modes
    int currentMode = static_cast<int>(_currentSelectionMode);
    currentMode = (currentMode + 1) % static_cast<int>(SelectionMode::NUM_SELECTION_TYPES);
    _currentSelectionMode = static_cast<SelectionMode>(currentMode);
    
    // Clear current selection when mode changes
    unselectAll();
    
    spdlog::info("Selection mode changed to: {}", selectionModeToString(_currentSelectionMode));
}

void EditorWidget::rotateSelectedObject() {
    if (_selectedObject) {
        // TODO: Implement object rotation
        spdlog::info("Rotate selected object not yet implemented");
    }
}

void EditorWidget::changeElevation(int elevation) {
    if (elevation >= 0 && elevation < 2) { // Maps have 2 elevations (0 and 1)
        _currentElevation = elevation;
        loadSprites(); // Reload sprites for new elevation
    }
}

bool EditorWidget::isTileVisible(int tileIndex, bool roof) {
    auto tile = _map->getMapFile().tiles.at(_currentElevation).at(tileIndex);

    if (roof) {
        uint16_t roofId = tile.getRoof();
        // Check if roof tile is empty/invisible
        // Allow selection of roof tiles that exist but may be transparent
        return roofId != Map::EMPTY_TILE;
    } else {
        // Floor tiles should always be selectable, even blank.frm tiles
        // The EMPTY_TILE case is handled by loading blank.frm
        return true;
    }
}

// Tile selection helper implementation

bool EditorWidget::isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite) {
    // Simple bounding box collision check - same as original EditorState
    return sprite.getGlobalBounds().contains(worldPos);
}


} // namespace geck