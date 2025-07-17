#include "EditorWidget.h"
#include "SFMLWidget.h"

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include <cmath> // ceil, sqrt, pow
#include <algorithm> // std::sort, std::find, std::max, std::min
#include <limits> // std::numeric_limits
#include "../util/QtDialogs.h"
#include "../util/Constants.h"
#include "../util/ColorUtils.h"
#include "../util/TileUtils.h"

#include "../editor/Object.h"

#include "../reader/frm/FrmReader.h"
#include "../reader/pro/ProReader.h"

#include "../writer/map/MapWriter.h"

#include "../format/frm/Frm.h"
#include "../format/lst/Lst.h"
#include "../format/map/Tile.h"
#include "../format/pro/Pro.h"
#include "../format/map/MapObject.h"

#include "../util/ProHelper.h"

namespace geck {

EditorWidget::EditorWidget(std::unique_ptr<Map> map, QWidget* parent)
    : QWidget(parent)
    , _layout(nullptr)
    , _sfmlWidget(nullptr)
    , _view({ 0.f, 0.f }, sf::Vector2f(800.f, 600.f)) // Default size, will be updated on first resize
    , _map(std::move(map)) {
    
    // Initialize selection management
    initializeSelectionSystem();
    
    setupUI();
    centerViewOnMap();
}

EditorWidget::~EditorWidget() {
    // Qt will handle cleanup of child widgets automatically
}

void EditorWidget::initializeSelectionSystem() {
    // Initialize the selection manager
    _selectionManager = std::make_unique<selection::SelectionManager>(_map.get(), this);
    
    // Setup direct callback for selection changes
    _selectionManager->setSelectionCallback([this](const selection::SelectionState& selection) {
        // First, clear all existing visual selections
        this->clearAllVisualSelections();
        
        // Then apply new visual selections
        for (const auto& item : selection.items) {
            switch (item.type) {
                case selection::SelectionType::OBJECT: {
                    auto object = item.getObject();
                    if (object) {
                        object->select();
                    }
                    break;
                }
                
                case selection::SelectionType::ROOF_TILE: {
                    int tileIndex = item.getTileIndex();
                    if (tileIndex >= 0 && tileIndex < Map::TILES_PER_ELEVATION) {
                        // Check if this is an empty roof tile and we're in ROOF_TILES_ALL mode
                        auto tile = _map->getMapFile().tiles.at(_currentElevation).at(tileIndex);
                        if (tile.getRoof() == Map::EMPTY_TILE && selection.mode == SelectionMode::ROOF_TILES_ALL) {
                            // Create a visual indicator for empty roof tile
                            sf::RectangleShape indicator;
                            indicator.setSize(sf::Vector2f(20, 20)); // Small rectangle
                            indicator.setFillColor(TileColors::errorFill());
                            indicator.setOutlineColor(TileColors::errorOutline());
                            indicator.setOutlineThickness(1);
                            
                            // Calculate tile position using utility function
                            auto screenPos = indexToScreenPosition(tileIndex, true);
                            
                            // Position indicator at calculated screen position
                            indicator.setPosition(static_cast<float>(screenPos.x) - 10, static_cast<float>(screenPos.y) - 10);
                            _emptyRoofTileIndicators.push_back(indicator);
                        } else {
                            // Apply normal highlighting to existing roof sprite
                            this->_roofSprites.at(tileIndex).setColor(geck::ColorUtils::createErrorIndicatorColor());
                        }
                    }
                    break;
                }
                
                case selection::SelectionType::FLOOR_TILE: {
                    int tileIndex = item.getTileIndex();
                    if (tileIndex >= 0 && tileIndex < Map::TILES_PER_ELEVATION) {
                        this->_floorSprites.at(tileIndex).setColor(geck::ColorUtils::createErrorIndicatorColor());
                    }
                    break;
                }
            }
        }
        
        // Emit efficient batched selection update instead of individual signals
        emit selectionChanged(selection, _currentElevation);
        
        // Keep legacy signals for backward compatibility if needed
        if (selection.isEmpty()) {
            emit tileSelectionCleared();
        } else {
            // For single-item selections, emit legacy signals for compatibility
            if (selection.items.size() == 1) {
                const auto& item = selection.items[0];
                switch (item.type) {
                    case selection::SelectionType::OBJECT: {
                        auto object = item.getObject();
                        if (object) {
                            emit objectSelected(object);
                        }
                        break;
                    }
                    case selection::SelectionType::ROOF_TILE:
                    case selection::SelectionType::FLOOR_TILE: {
                        int tileIndex = item.getTileIndex();
                        bool isRoof = (item.type == selection::SelectionType::ROOF_TILE);
                        emit tileSelected(tileIndex, _currentElevation, isRoof);
                        break;
                    }
                }
            }
        }
    });
    
    // Register the observer with the selection manager
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
    
    // Initialize spatial index for O(1) area selection performance
    _selectionManager->initializeSpatialIndex();
    
    // Initialize fake tile sprite for hit detection
    _fakeTileSprite.setTexture(ResourceManager::getInstance().texture("art/tiles/blank.frm"));
    
    // Initialize selection rectangle for drag selection
    _selectionRectangle.setFillColor(TileColors::selectionFill());
    _selectionRectangle.setOutlineColor(TileColors::selectionOutline());
    _selectionRectangle.setOutlineThickness(2.0f);
    
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
        _objects.back()->setDirection(static_cast<ObjectDirection>(object->direction));
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

        // Convert tile number to hex grid coordinates using utility function
        auto coords = indexToCoordinates(static_cast<int>(tileNumber));
        
        // Convert to screen coordinates using utility function
        auto screenPos = coordinatesToScreenPosition(coords);

        const auto& createTileSprite = [&](const uint16_t tile_id, int offset = 0) {
            sf::Sprite tile_sprite(ResourceManager::getInstance().texture("art/tiles/" + lst->at(tile_id)));
            tile_sprite.setPosition(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) - offset);
            return tile_sprite;
        };

        // floor
        uint16_t floorId = tile.getFloor();
        if (floorId == Map::EMPTY_TILE) {
            sf::Sprite tile_sprite(ResourceManager::getInstance().texture("art/tiles/blank.frm"));
            tile_sprite.setPosition(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
            _floorSprites[tileNumber] = tile_sprite;
        } else {
            _floorSprites[tileNumber] = createTileSprite(floorId);
        }

        // roof
        _roofSprites[tileNumber] = createTileSprite(tile.getRoof(), ROOF_OFFSET);
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

    // Rebuild spatial index after sprites are loaded
    _selectionManager->initializeSpatialIndex();
    
    // Load hex grid sprite
    try {
        FrmReader frm_reader{};
        ResourceManager::getInstance().loadResource("art/HEX.frm", frm_reader);

        _hexSprite.setTexture(ResourceManager::getInstance().texture("art/HEX.frm"));
        _hexSprite.setColor(sf::Color(255, 255, 255, 50)); // Very transparent white for grid
        
        // Initialize hex highlight sprite with red color
        _hexHighlightSprite.setTexture(ResourceManager::getInstance().texture("art/HEX.frm"));
        _hexHighlightSprite.setColor(sf::Color(255, 0, 0, 200)); // Bright red for highlighting
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load HEX.frm: {}", e.what());
    }

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

bool EditorWidget::isPointInSpriteBounds(sf::Vector2f worldPos, const sf::Sprite& sprite) const {
    return sprite.getGlobalBounds().contains(worldPos);
}

bool EditorWidget::isPointInSpritePixel(sf::Vector2f worldPos, const sf::Sprite& sprite) const {
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


void EditorWidget::clearAllVisualSelections() {
    // Clear all object selections
    for (auto& object : _objects) {
        if (object) {
            object->unselect();
        }
    }
    
    // Clear all tile colors
    for (auto& floorSprite : _floorSprites) {
        floorSprite.setColor(sf::Color::White);
    }
    
    for (auto& roofSprite : _roofSprites) {
        roofSprite.setColor(sf::Color::White);
    }
    
    // Clear empty roof tile indicators
    _emptyRoofTileIndicators.clear();
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
                
                // Check if we're in tile placement mode
                if (_tilePlacementMode && _tilePlacementIndex >= 0 && !_tilePlacementReplaceMode) {
                    // Check if Shift is held to place roof tiles instead of floor tiles
                    bool placeOnRoof = sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) || 
                                      sf::Keyboard::isKeyPressed(sf::Keyboard::RShift);
                    
                    if (_tilePlacementAreaFill) {
                        // Start area selection for tile filling
                        _currentAction = EditorAction::TILE_PLACING;
                        _dragStartWorldPos = worldPos;
                        _isDragSelecting = false; // Will become true on first mouse move
                        // Update the roof setting for area fill preview
                        _tilePlacementIsRoof = placeOnRoof;
                    } else {
                        // Single tile placement
                        placeTileAtPosition(_tilePlacementIndex, worldPos, placeOnRoof);
                    }
                    return; // Don't process as selection
                }
                
                // Detect modifier keys for multi-selection
                SelectionModifier modifier = SelectionModifier::NONE;
                bool hasModifiers = false;
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || 
                    sf::Keyboard::isKeyPressed(sf::Keyboard::RControl)) {
                    modifier = SelectionModifier::TOGGLE;  // Ctrl+Click toggles items
                    hasModifiers = true;
                } else if (sf::Keyboard::isKeyPressed(sf::Keyboard::LAlt) || 
                          sf::Keyboard::isKeyPressed(sf::Keyboard::RAlt)) {
                    modifier = SelectionModifier::ADD;     // Alt+Click (Option on macOS) adds items
                    hasModifiers = true;
                } else if (sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) || 
                          sf::Keyboard::isKeyPressed(sf::Keyboard::RShift)) {
                    modifier = SelectionModifier::RANGE;   // Shift+Click range selection
                    hasModifiers = true;
                }
                
                // Check if we can start object dragging (no modifiers and clicking on selected object)
                bool canObjectDrag = !hasModifiers && canStartObjectDrag(worldPos);
                
                if (canObjectDrag) {
                    // Start object drag operation
                    if (startObjectDrag(worldPos)) {
                        _currentAction = EditorAction::OBJECT_MOVING;
                        _isDragSelecting = false; // Will become true on first mouse move
                    }
                } else {
                    // Determine if we should start drag selection or do immediate selection
                    bool canDragSelect = !hasModifiers && 
                                       (_currentSelectionMode == SelectionMode::ALL ||
                                        _currentSelectionMode == SelectionMode::FLOOR_TILES || 
                                        _currentSelectionMode == SelectionMode::ROOF_TILES ||
                                        _currentSelectionMode == SelectionMode::ROOF_TILES_ALL ||
                                        _currentSelectionMode == SelectionMode::OBJECTS);
                    
                    if (canDragSelect) {
                        // Start drag selection
                        _currentAction = EditorAction::DRAG_SELECTING;
                        _dragStartWorldPos = worldPos;
                        _isDragSelecting = false; // Will become true on first mouse move
                        _immediateSelectionPerformed = false; // Reset flag
                    } else {
                        // Immediate selection
                        selectAtPosition(worldPos, modifier);
                        _immediateSelectionPerformed = true; // Mark that immediate selection was performed
                    }
                }
            } else if (event.mouseButton.button == sf::Mouse::Right) {
                // Start panning
                _currentAction = EditorAction::PANNING;
                _mouseStartingPosition = sf::Vector2i(event.mouseButton.x, event.mouseButton.y);
                _mouseLastPosition = _mouseStartingPosition;
            }
            break;
            
        case sf::Event::MouseButtonReleased:
            if (event.mouseButton.button == sf::Mouse::Left) {
                if (_currentAction == EditorAction::TILE_PLACING) {
                    // Clear preview visuals first
                    clearDragPreview();
                    
                    if (_isDragSelecting && _tilePlacementMode && _tilePlacementAreaFill) {
                        // Complete tile area fill
                        sf::Vector2f worldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                            sf::Vector2i(event.mouseButton.x, event.mouseButton.y), _view);
                        
                        // Create fill area
                        float left = std::min(_dragStartWorldPos.x, worldPos.x);
                        float top = std::min(_dragStartWorldPos.y, worldPos.y);
                        float width = std::abs(worldPos.x - _dragStartWorldPos.x);
                        float height = std::abs(worldPos.y - _dragStartWorldPos.y);
                        sf::FloatRect fillArea(left, top, width, height);
                        
                        // Fill area with selected tile
                        fillAreaWithTile(_tilePlacementIndex, fillArea, _tilePlacementIsRoof);
                        
                        _isDragSelecting = false;
                    }
                    
                    _currentAction = EditorAction::NONE;
                } else if (_currentAction == EditorAction::DRAG_SELECTING) {
                    // Clear preview visuals first
                    clearDragPreview();
                    
                    if (_isDragSelecting) {
                        // Complete drag selection
                        sf::Vector2f worldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                            sf::Vector2i(event.mouseButton.x, event.mouseButton.y), _view);
                        
                        // Create selection area
                        float left = std::min(_dragStartWorldPos.x, worldPos.x);
                        float top = std::min(_dragStartWorldPos.y, worldPos.y);
                        float width = std::abs(worldPos.x - _dragStartWorldPos.x);
                        float height = std::abs(worldPos.y - _dragStartWorldPos.y);
                        sf::FloatRect selectionArea(left, top, width, height);
                        
                        // Perform area selection - this will trigger observer notification and apply final colors
                        auto result = _selectionManager->selectArea(selectionArea, _currentSelectionMode, _currentElevation);
                        if (result.success) {
                            spdlog::info("Drag selection: {} items selected", _selectionManager->getCurrentSelection().count());
                        }
                        
                        _isDragSelecting = false;
                    } else if (!_immediateSelectionPerformed) {
                        // Was a click, not a drag - do normal selection (only if immediate selection wasn't already performed)
                        selectAtPosition(_dragStartWorldPos, SelectionModifier::NONE);
                    }
                    
                    _currentAction = EditorAction::NONE;
                    _immediateSelectionPerformed = false; // Reset flag when action completes
                } else if (_currentAction == EditorAction::OBJECT_MOVING) {
                    // Complete object drag operation
                    sf::Vector2f worldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                        sf::Vector2i(event.mouseButton.x, event.mouseButton.y), _view);
                    
                    if (_isDraggingObjects) {
                        // Complete the drag operation
                        finishObjectDrag(worldPos);
                    }
                    
                    _currentAction = EditorAction::NONE;
                }
            } else if (event.mouseButton.button == sf::Mouse::Right) {
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
            } else if (_currentAction == EditorAction::DRAG_SELECTING) {
                // Update drag selection rectangle
                sf::Vector2f currentWorldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                    sf::Vector2i(event.mouseMove.x, event.mouseMove.y), _view);
                
                // Mark as actively dragging if we've moved enough
                float distance = std::sqrt(std::pow(currentWorldPos.x - _dragStartWorldPos.x, 2) + 
                                         std::pow(currentWorldPos.y - _dragStartWorldPos.y, 2));
                if (distance > 5.0f) { // Minimum drag distance
                    _isDragSelecting = true;
                }
                
                if (_isDragSelecting) {
                    // Update selection rectangle
                    float left = std::min(_dragStartWorldPos.x, currentWorldPos.x);
                    float top = std::min(_dragStartWorldPos.y, currentWorldPos.y);
                    float width = std::abs(currentWorldPos.x - _dragStartWorldPos.x);
                    float height = std::abs(currentWorldPos.y - _dragStartWorldPos.y);
                    
                    _selectionRectangle.setPosition(left, top);
                    _selectionRectangle.setSize(sf::Vector2f(width, height));
                    
                    // Update preview of items that would be selected
                    updateDragPreview(currentWorldPos);
                }
            } else if (_currentAction == EditorAction::TILE_PLACING) {
                // Handle tile area fill drag
                if (_tilePlacementMode && _tilePlacementAreaFill) {
                    sf::Vector2f currentWorldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                        sf::Vector2i(event.mouseMove.x, event.mouseMove.y), _view);
                    
                    // Mark as actively dragging if we've moved enough
                    float distance = std::sqrt(std::pow(currentWorldPos.x - _dragStartWorldPos.x, 2) + 
                                             std::pow(currentWorldPos.y - _dragStartWorldPos.y, 2));
                    if (distance > 5.0f) { // Minimum drag distance
                        _isDragSelecting = true;
                    }
                    
                    if (_isDragSelecting) {
                        // Update selection rectangle for tile area fill (same as selection mode)
                        float left = std::min(_dragStartWorldPos.x, currentWorldPos.x);
                        float top = std::min(_dragStartWorldPos.y, currentWorldPos.y);
                        float width = std::abs(currentWorldPos.x - _dragStartWorldPos.x);
                        float height = std::abs(currentWorldPos.y - _dragStartWorldPos.y);
                        
                        _selectionRectangle.setPosition(left, top);
                        _selectionRectangle.setSize(sf::Vector2f(width, height));
                        
                        // Update tile area fill preview
                        updateTileAreaFillPreview(currentWorldPos);
                    }
                }
            } else if (_currentAction == EditorAction::OBJECT_MOVING) {
                // Handle object drag movement
                sf::Vector2f currentWorldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                    sf::Vector2i(event.mouseMove.x, event.mouseMove.y), _view);
                
                // Mark as actively dragging if we've moved enough
                float distance = std::sqrt(std::pow(currentWorldPos.x - _dragStartWorldPos.x, 2) + 
                                         std::pow(currentWorldPos.y - _dragStartWorldPos.y, 2));
                if (distance > 5.0f) { // Minimum drag distance
                    _isDraggingObjects = true;
                }
                
                if (_isDraggingObjects) {
                    // Update object positions for visual feedback
                    updateObjectDrag(currentWorldPos);
                }
            }
            
            // Always update hex hover for any mouse movement
            {
                sf::Vector2f currentWorldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                    sf::Vector2i(event.mouseMove.x, event.mouseMove.y), _view);
                updateHoverHex(currentWorldPos);
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
                    // Ctrl+A: Select all items of current type
                    if (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || 
                        sf::Keyboard::isKeyPressed(sf::Keyboard::RControl)) {
                        _selectionManager->selectAll(_currentSelectionMode, _currentElevation);
                        spdlog::info("Select all: {} items", _selectionManager->getCurrentSelection().count());
                    } else {
                        _view.move(-50.0f, 0.0f);
                    }
                    break;
                case sf::Keyboard::Right:
                case sf::Keyboard::D:
                    // Ctrl+D: Deselect all
                    if (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || 
                        sf::Keyboard::isKeyPressed(sf::Keyboard::RControl)) {
                        _selectionManager->clearSelection();
                        spdlog::info("Deselected all items");
                    } else {
                        _view.move(50.0f, 0.0f);
                    }
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
                case sf::Keyboard::Escape:
                    // Cancel any active operation
                    if (_currentAction == EditorAction::OBJECT_MOVING && _isDraggingObjects) {
                        cancelObjectDrag();
                        _currentAction = EditorAction::NONE;
                        spdlog::info("Object drag cancelled with ESC key");
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
    
    // Render drag selection rectangle
    if (_isDragSelecting && (_currentAction == EditorAction::DRAG_SELECTING || _currentAction == EditorAction::TILE_PLACING)) {
        window->draw(_selectionRectangle);
    }
    
    // Render empty roof tile indicators
    for (const auto& indicator : _emptyRoofTileIndicators) {
        window->draw(indicator);
    }
    
    // Render hex grid overlay
    renderHexGrid();
    
    // Render hex highlight if there's a valid hover hex
    if (_currentHoverHex >= 0) {
        // Find the hex with the matching position
        for (const auto& hex : _hexgrid.grid()) {
            if (hex.position() == _currentHoverHex) {
                float spriteX = static_cast<float>(hex.x() - 16);
                float spriteY = static_cast<float>(hex.y() - 8);
                
                _hexHighlightSprite.setPosition(spriteX, spriteY);
                window->draw(_hexHighlightSprite);
                break;
            }
        }
    }
}

bool EditorWidget::selectAtPosition(sf::Vector2f worldPos) {
    // Default to normal selection (no modifier)
    return selectAtPosition(worldPos, SelectionModifier::NONE);
}

bool EditorWidget::selectAtPosition(sf::Vector2f worldPos, SelectionModifier modifier) {
    // Update the observer with current elevation
    
    selection::SelectionResult result;
    
    switch (modifier) {
        case SelectionModifier::NONE:
            // Normal single selection (clear and select one)
            result = _selectionManager->selectAtPosition(worldPos, _currentSelectionMode, _currentElevation);
            break;
            
        case SelectionModifier::ADD:
            // Alt+Click - add to existing selection
            result = _selectionManager->addToSelection(worldPos, _currentSelectionMode, _currentElevation);
            spdlog::debug("Add to selection at ({:.1f}, {:.1f})", worldPos.x, worldPos.y);
            break;
            
        case SelectionModifier::TOGGLE:
            // Ctrl+Click - toggle selection (add if not selected, remove if selected)
            result = _selectionManager->toggleSelection(worldPos, _currentSelectionMode, _currentElevation);
            spdlog::debug("Toggle selection at ({:.1f}, {:.1f})", worldPos.x, worldPos.y);
            break;
            
        case SelectionModifier::RANGE:
            // Shift+Click - range selection for tiles
            result = handleRangeSelection(worldPos);
            spdlog::debug("Range selection at ({:.1f}, {:.1f})", worldPos.x, worldPos.y);
            break;
    }
    
    // Log selection count for multi-selection feedback
    if (result.success && result.selectionChanged) {
        const auto& selection = _selectionManager->getCurrentSelection();
        if (selection.count() > 1) {
            spdlog::info("Multi-selection: {} items selected", selection.count());
        }
    }
    
    return result.success && result.selectionChanged;
}



void EditorWidget::cycleSelectionMode() {
    // Cycle through selection modes
    int currentMode = static_cast<int>(_currentSelectionMode);
    currentMode = (currentMode + 1) % static_cast<int>(SelectionMode::NUM_SELECTION_TYPES);
    _currentSelectionMode = static_cast<SelectionMode>(currentMode);
    
    // Clear current selection when mode changes
    _selectionManager->clearSelection();
    
    spdlog::info("Selection mode changed to: {}", selectionModeToString(_currentSelectionMode));
}

void EditorWidget::rotateSelectedObject() {
    const auto& selection = _selectionManager->getCurrentSelection();
    auto objects = selection.getObjects();
    
    if (!objects.empty()) {
        for (auto& object : objects) {
            object->rotate();
            spdlog::debug("Rotated object to direction {}", object->getMapObject().direction);
        }
        spdlog::info("Rotated {} selected object(s)", objects.size());
    } else {
        spdlog::debug("No objects selected for rotation");
    }
}

void EditorWidget::changeElevation(int elevation) {
    if (elevation >= 0 && elevation < 2) { // Maps have 2 elevations (0 and 1)
        _currentElevation = elevation;
        loadSprites(); // Reload sprites for new elevation
    }
}

void EditorWidget::placeTileAtPosition(int tileIndex, sf::Vector2f worldPos, bool isRoof) {
    if (!_map) {
        spdlog::warn("EditorWidget::placeTileAtPosition: No map loaded");
        return;
    }
    
    // Use the same tile hit detection logic as the selection system
    int hexIndex = -1;
    for (int i = 0; i < Map::TILES_PER_ELEVATION; i++) {
        auto& sprites = isRoof ? _roofSprites : _floorSprites;
        _fakeTileSprite.setPosition(sprites.at(i).getPosition());
        
        if (isSpriteClicked(worldPos, _fakeTileSprite)) {
            hexIndex = i;
            break;
        }
    }
    
    if (hexIndex < 0) {
        spdlog::debug("EditorWidget::placeTileAtPosition: No tile found at worldPos ({:.1f}, {:.1f})", 
                     worldPos.x, worldPos.y);
        return;
    }
    
    // Get tiles for current elevation
    auto& mapFile = _map->getMapFile();
    auto& elevationTiles = mapFile.tiles[_currentElevation];
    
    if (hexIndex >= static_cast<int>(elevationTiles.size())) {
        spdlog::warn("EditorWidget::placeTileAtPosition: Hex index {} out of bounds", hexIndex);
        return;
    }
    
    // Update tile
    if (isRoof) {
        elevationTiles[hexIndex].setRoof(tileIndex);
    } else {
        elevationTiles[hexIndex].setFloor(tileIndex);
    }
    
    // Efficiently update just this tile's sprite
    updateTileSprite(hexIndex, isRoof);
    
    spdlog::debug("EditorWidget::placeTileAtPosition: Placed tile {} at hex {} (roof: {})", 
                  tileIndex, hexIndex, isRoof);
}

void EditorWidget::fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof) {
    if (!_map) {
        spdlog::warn("EditorWidget::fillAreaWithTile: No map loaded");
        return;
    }
    
    // Use the same logic as selection system to get tiles in area
    std::vector<int> tilesToFill = _selectionManager->getTilesInArea(area, isRoof, _currentElevation);
    
    if (tilesToFill.empty()) {
        spdlog::debug("EditorWidget::fillAreaWithTile: No tiles found in area");
        return;
    }
    
    auto& mapFile = _map->getMapFile();
    auto& elevationTiles = mapFile.tiles[_currentElevation];
    
    int tilesPlaced = 0;
    for (int hexIndex : tilesToFill) {
        if (hexIndex >= 0 && hexIndex < static_cast<int>(elevationTiles.size())) {
            if (isRoof) {
                elevationTiles[hexIndex].setRoof(tileIndex);
            } else {
                elevationTiles[hexIndex].setFloor(tileIndex);
            }
            
            // Efficiently update this tile's sprite
            updateTileSprite(hexIndex, isRoof);
            tilesPlaced++;
        }
    }
    
    spdlog::info("EditorWidget::fillAreaWithTile: Filled {} tiles with tile {} (roof: {})", 
                 tilesPlaced, tileIndex, isRoof);
}

void EditorWidget::replaceSelectedTiles(int newTileIndex) {
    if (!_map) {
        spdlog::warn("EditorWidget::replaceSelectedTiles: No map loaded");
        return;
    }
    
    const auto& selection = _selectionManager->getCurrentSelection();
    
    // Replace both floor and roof tiles based on what's actually selected
    std::vector<int> floorTileIndices = selection.getFloorTileIndices();
    std::vector<int> roofTileIndices = selection.getRoofTileIndices();
    
    if (floorTileIndices.empty() && roofTileIndices.empty()) {
        spdlog::debug("EditorWidget::replaceSelectedTiles: No tiles selected for replacement");
        return;
    }
    
    auto& mapFile = _map->getMapFile();
    auto& elevationTiles = mapFile.tiles[_currentElevation];
    
    int tilesReplaced = 0;
    
    // Replace floor tiles
    for (int hexIndex : floorTileIndices) {
        if (hexIndex >= 0 && hexIndex < static_cast<int>(elevationTiles.size())) {
            elevationTiles[hexIndex].setFloor(newTileIndex);
            updateTileSprite(hexIndex, false); // false = floor tile
            tilesReplaced++;
        }
    }
    
    // Replace roof tiles
    for (int hexIndex : roofTileIndices) {
        if (hexIndex >= 0 && hexIndex < static_cast<int>(elevationTiles.size())) {
            elevationTiles[hexIndex].setRoof(newTileIndex);
            updateTileSprite(hexIndex, true); // true = roof tile
            tilesReplaced++;
        }
    }
    
    spdlog::info("EditorWidget::replaceSelectedTiles: Replaced {} tiles with tile {} ({} floor, {} roof)", 
                 tilesReplaced, newTileIndex, floorTileIndices.size(), roofTileIndices.size());
}

void EditorWidget::setTilePlacementMode(bool enabled, int tileIndex, bool isRoof) {
    _tilePlacementMode = enabled;
    _tilePlacementIndex = tileIndex;
    _tilePlacementIsRoof = isRoof;
    
    if (enabled) {
        spdlog::debug("EditorWidget: Enabled tile placement mode - tile {}, roof: {}", tileIndex, isRoof);
    } else {
        spdlog::debug("EditorWidget: Disabled tile placement mode");
    }
}

void EditorWidget::setTilePlacementAreaFill(bool enabled) {
    _tilePlacementAreaFill = enabled;
    spdlog::debug("EditorWidget: Set tile placement area fill mode: {}", enabled);
}

void EditorWidget::setTilePlacementReplaceMode(bool enabled) {
    _tilePlacementReplaceMode = enabled;
    spdlog::debug("EditorWidget: Set tile placement replace mode: {}", enabled);
}

void EditorWidget::updateTileSprite(int hexIndex, bool isRoof) {
    if (!_map || hexIndex < 0 || hexIndex >= static_cast<int>(Map::TILES_PER_ELEVATION)) {
        return;
    }
    
    // Get tiles for current elevation
    const auto& elevationTiles = _map->getMapFile().tiles[_currentElevation];
    if (hexIndex >= static_cast<int>(elevationTiles.size())) {
        return;
    }
    
    // Get the tile data
    const auto& tile = elevationTiles[hexIndex];
    int tileIndex = isRoof ? tile.getRoof() : tile.getFloor();
    
    // Skip if tile is empty
    if (tileIndex == Map::EMPTY_TILE) {
        return;
    }
    
    // Get the sprite array to update
    auto& sprites = isRoof ? _roofSprites : _floorSprites;
    
    // Load the texture for this tile
    try {
        auto& resourceManager = ResourceManager::getInstance();
        const auto* tileList = resourceManager.getResource<Lst, std::string>("art/tiles/tiles.lst");
        if (!tileList || tileIndex >= static_cast<int>(tileList->list().size())) {
            return;
        }
        
        const std::string& tileName = tileList->list()[tileIndex];
        std::string tilePath = "art/tiles/" + tileName;
        const auto& texture = resourceManager.texture(tilePath);
        
        // Update the sprite
        sprites[hexIndex].setTexture(texture);

        // Calculate position using utility function (eliminates duplicate code)
        auto screenPos = indexToScreenPosition(hexIndex, isRoof);
        sprites[hexIndex].setPosition(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
        
        spdlog::debug("EditorWidget::updateTileSprite: Updated sprite for hex {} ({})", hexIndex, tileName);
    } catch (const std::exception& e) {
        spdlog::warn("EditorWidget::updateTileSprite: Failed to update tile sprite: {}", e.what());
    }
}


// Helper methods implementation

int EditorWidget::worldPosToHexIndex(sf::Vector2f worldPos) const {
    // Convert world position to hexagonal grid coordinates
    // Use the same coordinate system as used in tile loading:
    // x = (100 - tileY - 1) * 48 + 32 * tileX
    // y = tileX * 24 + tileY * 12
    
    // Find the closest tile by iterating through all possible positions
    // This is not the most efficient but ensures correctness
    float minDistance = std::numeric_limits<float>::max();
    int closestIndex = -1;
    
    for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); ++i) {
        // Calculate world position for this tile using utility function
        auto screenPos = indexToScreenPosition(i);
        
        // Calculate distance from click position
        float dx = worldPos.x - static_cast<float>(screenPos.x);
        float dy = worldPos.y - static_cast<float>(screenPos.y);
        float distance = dx * dx + dy * dy;
        
        if (distance < minDistance) {
            minDistance = distance;
            closestIndex = i;
        }
    }
    
    // Only return the index if it's reasonably close (within tile bounds)
    if (minDistance < 40.0f * 40.0f) { // Within 40 pixels
        return closestIndex;
    }
    
    return -1;
}

// Tile selection helper implementation

bool EditorWidget::isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite) {
    // Simple bounding box collision check - same as original EditorState
    return sprite.getGlobalBounds().contains(worldPos);
}

// Methods for SelectionManager to access tile and object data

std::optional<int> EditorWidget::getTileAtPosition(sf::Vector2f worldPos, bool isRoof) {
    auto& sprites = isRoof ? _roofSprites : _floorSprites;
    
    for (int i = 0; i < Map::TILES_PER_ELEVATION; i++) {
        _fakeTileSprite.setPosition(sprites.at(i).getPosition());
        
        if (isSpriteClicked(worldPos, _fakeTileSprite)) {
            if (isRoof && _map->getMapFile().tiles.at(_currentElevation).at(i).getRoof() == Map::EMPTY_TILE) {
                return std::nullopt;
            }
            return i;
        }
    }
    return std::nullopt;
}

std::optional<int> EditorWidget::getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos) {
    // This version includes empty roof tiles in the selection
    for (int i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
        sf::FloatRect tileBounds = _roofSprites.at(i).getGlobalBounds();
        
        // Check if world position is within tile bounds
        if (tileBounds.contains(worldPos)) {
            return i;
        }
    }
    
    return std::nullopt;
}

selection::SelectionResult EditorWidget::handleRangeSelection(sf::Vector2f worldPos) {
    // Range selection is primarily for tiles - select a rectangular area of tiles
    const auto& currentSelection = _selectionManager->getCurrentSelection();
    
    // For range selection to work, we need a starting point
    // If nothing is selected, just do a normal selection
    if (currentSelection.isEmpty()) {
        return _selectionManager->selectAtPosition(worldPos, _currentSelectionMode, _currentElevation);
    }
    
    // Get the first selected tile as the starting point for range selection
    sf::Vector2f startPos;
    bool hasStartTile = false;
    
    for (const auto& item : currentSelection.items) {
        if (item.type == selection::SelectionType::FLOOR_TILE || 
            item.type == selection::SelectionType::ROOF_TILE) {
            // Convert tile index to world position
            int tileIndex = item.getTileIndex();
            auto screenPos = indexToScreenPosition(tileIndex);
            startPos = sf::Vector2f(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
            hasStartTile = true;
            break;
        }
    }
    
    // If no tile in current selection, fallback to normal selection
    if (!hasStartTile) {
        return _selectionManager->selectAtPosition(worldPos, _currentSelectionMode, _currentElevation);
    }
    
    // Calculate rectangular area between start and current position
    float left = std::min(startPos.x, worldPos.x);
    float top = std::min(startPos.y, worldPos.y);
    float right = std::max(startPos.x, worldPos.x);
    float bottom = std::max(startPos.y, worldPos.y);
    
    // Add some padding to ensure we catch tiles at the edges
    sf::FloatRect selectionArea(
        left - AREA_SELECTION_X_PADDING, 
        top - AREA_SELECTION_Y_PADDING, 
        (right - left) + AREA_SELECTION_X_TOTAL_PADDING, 
        (bottom - top) + AREA_SELECTION_Y_TOTAL_PADDING
    );
    
    // Determine selection mode based on current mode
    SelectionMode areaMode = _currentSelectionMode;
    if (_currentSelectionMode == SelectionMode::ALL) {
        // For ALL mode, default to floor tiles for range selection
        areaMode = SelectionMode::FLOOR_TILES;
    }
    
    // Use the SelectionManager's area selection functionality
    auto result = _selectionManager->selectArea(selectionArea, areaMode, _currentElevation);
    
    spdlog::info("Range selection: area ({:.1f}, {:.1f}, {:.1f}, {:.1f})", 
                 selectionArea.left, selectionArea.top, selectionArea.width, selectionArea.height);
    
    return result;
}

void EditorWidget::updateDragPreview(sf::Vector2f currentWorldPos) {
    // Clear previous preview
    clearDragPreview();
    
    // Create selection area
    float left = std::min(_dragStartWorldPos.x, currentWorldPos.x);
    float top = std::min(_dragStartWorldPos.y, currentWorldPos.y);
    float width = std::abs(currentWorldPos.x - _dragStartWorldPos.x);
    float height = std::abs(currentWorldPos.y - _dragStartWorldPos.y);
    sf::FloatRect selectionArea(left, top, width, height);
    
    // Get items that would be selected
    switch (_currentSelectionMode) {
        case SelectionMode::FLOOR_TILES: {
            _previewTiles = _selectionManager->getTilesInArea(selectionArea, false, _currentElevation);
            // Apply preview coloring to floor tiles
            for (int tileIndex : _previewTiles) {
                if (tileIndex >= 0 && tileIndex < Map::TILES_PER_ELEVATION) {
                    applyPreviewHighlight(_floorSprites.at(tileIndex));
                }
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES: {
            _previewTiles = _selectionManager->getTilesInArea(selectionArea, true, _currentElevation);
            // Apply preview coloring to roof tiles
            for (int tileIndex : _previewTiles) {
                if (tileIndex >= 0 && tileIndex < Map::TILES_PER_ELEVATION) {
                    applyPreviewHighlight(_roofSprites.at(tileIndex));
                }
            }
            break;
        }
        
        case SelectionMode::ROOF_TILES_ALL: {
            _previewTiles = _selectionManager->getTilesInAreaIncludingEmpty(selectionArea, true, _currentElevation);
            // Apply preview coloring to roof tiles including empty ones
            for (int tileIndex : _previewTiles) {
                if (tileIndex >= 0 && tileIndex < Map::TILES_PER_ELEVATION) {
                    // Check if this roof tile is empty
                    auto tile = _map->getMapFile().tiles.at(_currentElevation).at(tileIndex);
                    if (tile.getRoof() == Map::EMPTY_TILE) {
                        // Create a visual indicator for empty roof tile
                        sf::RectangleShape indicator;
                        indicator.setSize(sf::Vector2f(20, 20)); // Small rectangle
                        indicator.setFillColor(TileColors::previewFill());
                        indicator.setOutlineColor(TileColors::previewOutline());
                        indicator.setOutlineThickness(1);
                        
                        // Calculate tile position using utility function
                        auto screenPos = indexToScreenPosition(tileIndex);
                        
                        // Position indicator at calculated screen position
                        indicator.setPosition(static_cast<float>(screenPos.x) - 10, static_cast<float>(screenPos.y) - 10);
                        _emptyRoofTileIndicators.push_back(indicator);
                    } else {
                        // Apply preview coloring to existing roof sprite
                        applyPreviewHighlight(_roofSprites.at(tileIndex));
                    }
                }
            }
            break;
        }
        
        case SelectionMode::OBJECTS: {
            _previewObjects = _selectionManager->getObjectsInArea(selectionArea, _currentElevation);
            // Apply preview coloring to objects
            for (auto& object : _previewObjects) {
                if (object) {
                    applyPreviewHighlight(object->getSprite());
                }
            }
            break;
        }
        
        case SelectionMode::ALL: {
            // Preview all types of items in ALL mode
            
            // Preview floor tiles
            _previewTiles = _selectionManager->getTilesInArea(selectionArea, false, _currentElevation);
            for (int tileIndex : _previewTiles) {
                if (tileIndex >= 0 && tileIndex < Map::TILES_PER_ELEVATION) {
                    applyPreviewHighlight(_floorSprites.at(tileIndex));
                }
            }
            
            // Preview roof tiles
            auto roofTiles = _selectionManager->getTilesInArea(selectionArea, true, _currentElevation);
            for (int tileIndex : roofTiles) {
                if (tileIndex >= 0 && tileIndex < Map::TILES_PER_ELEVATION) {
                    applyPreviewHighlight(_roofSprites.at(tileIndex));
                }
            }
            _previewTiles.insert(_previewTiles.end(), roofTiles.begin(), roofTiles.end());
            
            // Preview objects
            _previewObjects = _selectionManager->getObjectsInArea(selectionArea, _currentElevation);
            for (auto& object : _previewObjects) {
                if (object) {
                    applyPreviewHighlight(object->getSprite());
                }
            }
            break;
        }
            
        default:
            break;
    }
}

void EditorWidget::updateTileAreaFillPreview(sf::Vector2f currentWorldPos) {
    // Clear previous preview
    clearDragPreview();
    
    // Create selection area
    float left = std::min(_dragStartWorldPos.x, currentWorldPos.x);
    float top = std::min(_dragStartWorldPos.y, currentWorldPos.y);
    float width = std::abs(currentWorldPos.x - _dragStartWorldPos.x);
    float height = std::abs(currentWorldPos.y - _dragStartWorldPos.y);
    sf::FloatRect selectionArea(left, top, width, height);
    
    // Get tiles that would be affected by the area fill (default to floor tiles)
    bool isRoof = _tilePlacementIsRoof;
    _previewTiles = _selectionManager->getTilesInArea(selectionArea, isRoof, _currentElevation);
    
    // Apply preview coloring to tiles (same as selection mode)
    auto& sprites = isRoof ? _roofSprites : _floorSprites;
    for (int tileIndex : _previewTiles) {
        if (tileIndex >= 0 && tileIndex < Map::TILES_PER_ELEVATION) {
            applyPreviewHighlight(sprites.at(tileIndex));
        }
    }
}

void EditorWidget::clearDragPreview() {
    // Clear tile preview coloring
    for (int tileIndex : _previewTiles) {
        if (tileIndex >= 0 && tileIndex < Map::TILES_PER_ELEVATION) {
            removePreviewHighlight(_floorSprites.at(tileIndex));
            removePreviewHighlight(_roofSprites.at(tileIndex));
        }
    }
    
    // Clear object preview coloring
    for (auto& object : _previewObjects) {
        if (object) {
            removePreviewHighlight(object->getSprite());
        }
    }
    
    // Clear empty roof tile indicators
    _emptyRoofTileIndicators.clear();
    
    // Clear the preview arrays
    _previewTiles.clear();
    _previewObjects.clear();
}

// Object drag management methods

bool EditorWidget::canStartObjectDrag(sf::Vector2f worldPos) const {
    // Check if we have selected objects and if the click position is on a selected object
    const auto& selection = _selectionManager->getCurrentSelection();
    auto selectedObjects = selection.getObjects();
    
    if (selectedObjects.empty()) {
        return false;
    }
    
    // Check if the click position is on any selected object
    for (const auto& object : selectedObjects) {
        if (object && isPointInSpritePixel(worldPos, object->getSprite())) {
            return true;
        }
    }
    
    return false;
}

bool EditorWidget::startObjectDrag(sf::Vector2f worldPos) {
    if (!canStartObjectDrag(worldPos)) {
        return false;
    }
    
    // Get selected objects to drag
    const auto& selection = _selectionManager->getCurrentSelection();
    _draggedObjects = selection.getObjects();
    
    if (_draggedObjects.empty()) {
        return false;
    }
    
    // Store original hex center positions for drag calculations (not sprite positions)
    _objectDragStartPositions.clear();
    _objectDragStartPositions.reserve(_draggedObjects.size());
    
    for (const auto& object : _draggedObjects) {
        if (object) {
            // Get the object's current hex position and convert to hex center coordinates
            int currentHexPosition = object->getMapObject().position;
            const Hex* currentHex = _hexgrid.getHexByPosition(static_cast<uint32_t>(currentHexPosition));
            if (currentHex) {
                // Store hex center position, not sprite position
                _objectDragStartPositions.push_back(sf::Vector2f(
                    static_cast<float>(currentHex->x()), 
                    static_cast<float>(currentHex->y())
                ));
                spdlog::debug("Object at hex {} starts drag from hex center ({}, {})", 
                             currentHexPosition, currentHex->x(), currentHex->y());
            } else {
                // Fallback: use sprite position
                _objectDragStartPositions.push_back(object->getSprite().getPosition());
                spdlog::warn("Could not find hex for position {}, using sprite position", currentHexPosition);
            }
        }
    }
    
    // Initialize drag state
    _isDraggingObjects = true;
    _dragStartWorldPos = worldPos;
    _objectDragOffset = sf::Vector2f(0, 0);
    
    spdlog::info("Started dragging {} objects", _draggedObjects.size());
    return true;
}

void EditorWidget::updateObjectDrag(sf::Vector2f currentWorldPos) {
    if (!_isDraggingObjects || _draggedObjects.empty()) {
        return;
    }
    
    // Calculate drag offset
    _objectDragOffset = currentWorldPos - _dragStartWorldPos;
    
    // Apply visual offset to dragged objects (preview)
    for (size_t i = 0; i < _draggedObjects.size(); ++i) {
        if (_draggedObjects[i] && i < _objectDragStartPositions.size()) {
            // Calculate new hex center position
            sf::Vector2f newHexCenter = _objectDragStartPositions[i] + _objectDragOffset;
            
            // Apply FRM offsets to position sprite correctly relative to hex center
            auto& object = _draggedObjects[i];
            float spriteX = newHexCenter.x - (object->width() / 2) + object->shiftX();
            float spriteY = newHexCenter.y - object->height() + object->shiftY();
            
            object->getSprite().setPosition(spriteX, spriteY);
        }
    }
}

void EditorWidget::finishObjectDrag(sf::Vector2f finalWorldPos) {
    if (!_isDraggingObjects || _draggedObjects.empty()) {
        return;
    }
    
    // Calculate final drag offset
    sf::Vector2f finalOffset = finalWorldPos - _dragStartWorldPos;
    
    // Snap each object to the nearest hex grid position
    int movedCount = 0;
    for (size_t i = 0; i < _draggedObjects.size(); ++i) {
        auto& object = _draggedObjects[i];
        if (!object || i >= _objectDragStartPositions.size()) {
            continue;
        }
        
        // Calculate final world position for this object
        sf::Vector2f finalObjectPos = _objectDragStartPositions[i] + finalOffset;
        
        // Snap to nearest hex grid position
        sf::Vector2f snappedPos = snapToHexGrid(finalObjectPos);
        spdlog::debug("Snapping object from ({:.1f}, {:.1f}) to ({:.1f}, {:.1f})", 
                     finalObjectPos.x, finalObjectPos.y, snappedPos.x, snappedPos.y);
        
        // Get the hex position index
        int newHexPosition = worldPosToHexPosition(snappedPos);
        spdlog::debug("World position ({:.1f}, {:.1f}) maps to hex position {}", 
                     snappedPos.x, snappedPos.y, newHexPosition);
        
        if (newHexPosition >= 0) {
            // Get the proper hex object for this position
            const Hex* targetHex = _hexgrid.getHexByPosition(static_cast<uint32_t>(newHexPosition));
            if (targetHex) {
                // Update the map object's position index for saving
                int oldHexPosition = object->getMapObject().position;
                
                // Use setHexPosition to properly position the object with FRM offsets
                object->setHexPosition(*targetHex);
                
                spdlog::info("Hex coordinate update: old position {} -> new position {}", oldHexPosition, newHexPosition);
                
                // Debug: Check the final sprite position after applying FRM offsets
                sf::Vector2f spritePos = object->getSprite().getPosition();
                spdlog::debug("Object positioned at ({:.1f}, {:.1f}) with FRM offsets applied", 
                             spritePos.x, spritePos.y);
                
                movedCount++;
                spdlog::debug("Snapped object to hex position {} with proper FRM offsets", newHexPosition);
            } else {
                spdlog::error("Failed to get hex object for position {}", newHexPosition);
                // Invalid position - restore to original hex position
                int originalHexPosition = object->getMapObject().position;
                const Hex* originalHex = _hexgrid.getHexByPosition(static_cast<uint32_t>(originalHexPosition));
                if (originalHex) {
                    object->setHexPosition(*originalHex);
                } else {
                    // Last resort: use sprite positioning
                    object->getSprite().setPosition(_objectDragStartPositions[i]);
                }
            }
        } else {
            // Invalid position - restore to original hex position
            int originalHexPosition = object->getMapObject().position;
            const Hex* originalHex = _hexgrid.getHexByPosition(static_cast<uint32_t>(originalHexPosition));
            if (originalHex) {
                object->setHexPosition(*originalHex);
                spdlog::debug("Invalid drop position - restored object to original hex {}", originalHexPosition);
            } else {
                // Last resort: use sprite positioning
                object->getSprite().setPosition(_objectDragStartPositions[i]);
                spdlog::debug("Invalid drop position - restored object to original sprite position");
            }
        }
    }
    
    // Clean up drag state
    _isDraggingObjects = false;
    _draggedObjects.clear();
    _objectDragStartPositions.clear();
    _objectDragOffset = sf::Vector2f(0, 0);
    
    // Debug: Check if objects are still in the render list
    spdlog::debug("Total objects in render list: {}", _objects.size());
    
    // Update selection panel to reflect new object positions
    if (movedCount > 0) {
        // Trigger selection update to refresh the UI panels
        const auto& selection = _selectionManager->getCurrentSelection();
        if (!selection.isEmpty()) {
            // Emit selection change to update panels
            emit selectionChanged(selection, _currentElevation);
        }
    }
    
    spdlog::info("Finished dragging: {} objects moved and snapped to hex grid", movedCount);
}

void EditorWidget::cancelObjectDrag() {
    if (!_isDraggingObjects || _draggedObjects.empty()) {
        return;
    }
    
    // Restore original hex positions
    for (size_t i = 0; i < _draggedObjects.size(); ++i) {
        if (_draggedObjects[i] && i < _objectDragStartPositions.size()) {
            // Restore to original hex position
            int originalHexPosition = _draggedObjects[i]->getMapObject().position;
            const Hex* originalHex = _hexgrid.getHexByPosition(static_cast<uint32_t>(originalHexPosition));
            if (originalHex) {
                _draggedObjects[i]->setHexPosition(*originalHex);
            } else {
                // Fallback: use stored position
                _draggedObjects[i]->getSprite().setPosition(_objectDragStartPositions[i]);
            }
        }
    }
    
    // Clean up drag state
    _isDraggingObjects = false;
    _draggedObjects.clear();
    _objectDragStartPositions.clear();
    _objectDragOffset = sf::Vector2f(0, 0);
    
    spdlog::info("Cancelled object drag operation");
}

// Hex grid snapping helper methods

sf::Vector2f EditorWidget::snapToHexGrid(sf::Vector2f worldPos) const {
    // Find the closest hex grid position by iterating through actual hex positions
    float minDistance = std::numeric_limits<float>::max();
    sf::Vector2f closestHexPos = worldPos;
    
    // Iterate through the actual hex grid using proper position indices
    const auto& hexGrid = _hexgrid.grid();
    for (size_t i = 0; i < hexGrid.size(); ++i) {
        const auto& hex = hexGrid[i];
        sf::Vector2f hexWorldPos(static_cast<float>(hex.x()), static_cast<float>(hex.y()));
        
        // Calculate distance from current world position
        float dx = worldPos.x - hexWorldPos.x;
        float dy = worldPos.y - hexWorldPos.y;
        float distance = dx * dx + dy * dy;
        
        if (distance < minDistance) {
            minDistance = distance;
            closestHexPos = hexWorldPos;
        }
    }
    
    return closestHexPos;
}

int EditorWidget::worldPosToHexPosition(sf::Vector2f worldPos) const {
    // Use the hex grid's built-in positionAt method for accurate position lookup
    uint32_t hexPosition = _hexgrid.positionAt(static_cast<uint32_t>(worldPos.x), 
                                               static_cast<uint32_t>(worldPos.y));
    
    if (hexPosition == static_cast<uint32_t>(-1)) {
        return -1;
    }
    
    return static_cast<int>(hexPosition);
}

void EditorWidget::renderHexGrid() {
    if (!_showHexGrid) {
        return;
    }
    
    auto* window = _sfmlWidget->getRenderWindow();
    if (!window) {
        return;
    }
    
    // Get viewport bounds for culling (similar to legacy implementation)
    sf::Vector2f viewCenter = _view.getCenter();
    sf::Vector2f viewSize = _view.getSize();
    
    // Calculate visible area bounds
    int worldX = static_cast<int>(viewCenter.x - viewSize.x / 2);
    int worldY = static_cast<int>(viewCenter.y - viewSize.y / 2);
    int viewWidth = static_cast<int>(viewSize.x);
    int viewHeight = static_cast<int>(viewSize.y);
    
    // Iterate through 200x200 hex grid (based on legacy implementation)
    for (int y = 0; y < 200; y++) {
        for (int x = 0; x < 200; x++) {
            // Convert hex coordinates to world coordinates using existing HexagonGrid
            int hexIndex = y * 200 + x;
            if (hexIndex >= static_cast<int>(_hexgrid.grid().size())) {
                continue;
            }
            
            // Get the hex data for this grid index
            const auto& hex = _hexgrid.grid().at(hexIndex);
            int actualHexPosition = hex.position();
            
            // Skip rendering the hex that's currently being highlighted
            if (actualHexPosition == _currentHoverHex) {
                continue;
            }
            
            int hexWorldX = hex.x();
            int hexWorldY = hex.y();
            
            // Viewport culling - only render visible hex sprites
            // Based on legacy: (nMapX + 32 > WorldX && nMapX < WorldX + w1) && (nMapY + 16 > WorldY && nMapY < WorldY + h1)
            if ((hexWorldX + 32 > worldX && hexWorldX < worldX + viewWidth) &&
                (hexWorldY + 16 > worldY && hexWorldY < worldY + viewHeight)) {
                
                // Position hex sprite - based on legacy: nMapX - WorldX - 16, nMapY - WorldY - 8
                float spriteX = static_cast<float>(hexWorldX - 16);
                float spriteY = static_cast<float>(hexWorldY - 8);
                
                _hexSprite.setPosition(spriteX, spriteY);
                window->draw(_hexSprite);
            }
        }
    }
}

void EditorWidget::updateHoverHex(sf::Vector2f worldPos) {
    int newHoverHex = worldPosToHexPosition(worldPos);
    
    if (newHoverHex != _currentHoverHex) {
        _currentHoverHex = newHoverHex;
        Q_EMIT hexHoverChanged(_currentHoverHex);
    }
}

} // namespace geck