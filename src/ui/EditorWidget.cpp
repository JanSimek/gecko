#include "EditorWidget.h"
#include "SFMLWidget.h"

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include <cmath>     // ceil, sqrt, pow
#include <algorithm> // std::sort, std::find, std::max, std::min
#include <limits>    // std::numeric_limits
#include <cstdlib>   // std::abs
#include "../util/Constants.h"
#include "../util/ColorUtils.h"
#include "../editor/HexagonGrid.h"
#include "../util/TileUtils.h"
#include "../util/QtDialogs.h"

#include "../editor/Object.h"


#include "../writer/map/MapWriter.h"

#include "../format/frm/Frm.h"
#include "../format/lst/Lst.h"
#include "../format/map/Tile.h"
#include "../format/pro/Pro.h"
#include "../format/map/MapObject.h"
#include "ObjectPalettePanel.h"
#include "MainWindow.h"

#include "../util/ProHelper.h"

namespace geck {

EditorWidget::EditorWidget(std::unique_ptr<Map> map, QWidget* parent)
    : QWidget(parent)
    , _layout(nullptr)
    , _sfmlWidget(nullptr)
    , _mainWindow(nullptr)
    , _view({ 0.f, 0.f }, sf::Vector2f(800.f, 600.f)) // Default size, will be updated on first resize
    , _map(std::move(map))
    , _hexSprite(createHexTexture())
    , _hexHighlightSprite(createCursorHexTexture()) {

    // Set texture rectangle to show only half of HEX.frm (right half for highlighting)
    sf::Vector2u textureSize = _hexHighlightSprite.getTexture().getSize();
    _hexHighlightSprite.setTextureRect(
        sf::IntRect(
            sf::Vector2i(static_cast<int>(textureSize.x / 2), 0),
            sf::Vector2i(static_cast<int>(textureSize.x / 2), static_cast<int>(textureSize.y))));
    _hexHighlightSprite.setColor(sf::Color(Colors::ERROR_R, Colors::ERROR_G, Colors::ERROR_B, 255));

    // Initialize sprite vectors with blank texture
    const sf::Texture& blankTexture = createBlankTexture();
    _floorSprites.reserve(Map::TILES_PER_ELEVATION);
    _roofSprites.reserve(Map::TILES_PER_ELEVATION);

    for (size_t i = 0; i < Map::TILES_PER_ELEVATION; ++i) {
        _floorSprites.emplace_back(blankTexture);
        _roofSprites.emplace_back(blankTexture);
    }

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
                    if (tileIndex >= 0 && tileIndex < static_cast<int>(Map::TILES_PER_ELEVATION)) {
                        // Check if this is an empty roof tile and we're in ROOF_TILES_ALL mode
                        [[maybe_unused]] auto tile = _map->getMapFile().tiles.at(_currentElevation).at(tileIndex);
                        // Apply highlighting to roof sprite with higher visibility for better contrast
                        this->_roofSprites.at(tileIndex).setColor(geck::ColorUtils::createRoofTileSelectionColor());

                        // Create a blank.frm background sprite for better visibility of tiles with transparent pixels
                        auto screenPos = geck::indexToScreenPosition(tileIndex, true); // true for roof offset
                        sf::Sprite backgroundSprite(ResourceManager::getInstance().texture("art/tiles/blank.frm"));
                        backgroundSprite.setPosition({ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) });
                        // Set semi-transparent red color to show through transparent areas of the roof tile
                        backgroundSprite.setColor(sf::Color(Colors::ERROR_R, Colors::ERROR_G, Colors::ERROR_B, 128)); // 50% transparency
                        this->_selectedRoofTileBackgroundSprites.push_back(backgroundSprite);
                    }
                    break;
                }

                case selection::SelectionType::FLOOR_TILE: {
                    int tileIndex = item.getTileIndex();
                    if (tileIndex >= 0 && tileIndex < static_cast<int>(Map::TILES_PER_ELEVATION)) {
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

    _layout->addWidget(_sfmlWidget, 1); // Stretch factor 1 - take all available space

    setLayout(_layout);
}

void EditorWidget::init() {
    loadSprites();

    // Initialize spatial index for O(1) area selection performance
    _selectionManager->initializeSpatialIndex();

    // Initialize selection rectangle for drag selection
    _selectionRectangle.setFillColor(TileColors::selectionFill());
    _selectionRectangle.setOutlineColor(TileColors::selectionOutline());
    _selectionRectangle.setOutlineThickness(2.0f);

    // Ensure view is properly sized to match current window
    if (_sfmlWidget && _sfmlWidget->getRenderWindow()) {
        sf::Vector2u windowSize = _sfmlWidget->getRenderWindow()->getSize();
        if (windowSize.x > 0 && windowSize.y > 0) {
            _view.setSize({ static_cast<float>(windowSize.x), static_cast<float>(windowSize.y) });
            spdlog::debug("EditorWidget::init() - Set initial view size to {}x{}", windowSize.x, windowSize.y);
        }
    }

    // Re-center view after setting size
    centerViewOnMap();
}

void EditorWidget::saveMap() {
    QString destinationQString = QtDialogs::saveFile(this, "Select a file", "Map Files (*.map);;All Files (*.*)");
    
    if (destinationQString.isEmpty()) {
        return; // User cancelled
    }
    
    std::string destination = destinationQString.toStdString();

    try {
        MapWriter map_writer{ [](int32_t PID) {
            return ResourceManager::getInstance().loadResource<Pro>(ProHelper::basePath(PID));
        } };

        map_writer.openFile(destination);
        if (map_writer.write(_map->getMapFile())) {
            spdlog::info("Saved map {} ({} bytes)", destination, map_writer.getBytesWritten());
        } else {
            spdlog::error("Failed to save map {}", destination);
        }
    } catch (const geck::FileWriterException& e) {
        spdlog::error("Failed to save map {}: {}", destination, e.what());
        // Could show error dialog to user here
    } catch (const std::exception& e) {
        spdlog::error("Unexpected error saving map {}: {}", destination, e.what());
    }
}

void EditorWidget::openMap() {
    // Show file dialog to select a new map
    QString mapPathQString = QtDialogs::openMapFile(this, "Choose Fallout 2 map to load");

    if (mapPathQString.isEmpty()) {
        spdlog::info("No map file selected");
        return;
    }

    std::string mapPath = mapPathQString.toStdString();
    spdlog::info("User requested to open new map: {}", mapPath);

    // Emit signal to request map loading - MainWindow will handle the actual loading
    emit mapLoadRequested(mapPath);
}

void EditorWidget::createNewMap() {
    spdlog::info("Create new map functionality not yet implemented");
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

        _objects.emplace_back(std::make_shared<Object>(frm));
        sf::Sprite object_sprite{ ResourceManager::getInstance().texture(frm_name) };
        _objects.back()->setSprite(std::move(object_sprite));
        _objects.back()->setHexPosition(_hexgrid.grid().at(object->position));
        _objects.back()->setMapObject(object);
        _objects.back()->setDirection(static_cast<ObjectDirection>(object->direction));
    }
}

// Tiles
void EditorWidget::loadTileSprites() {
    const auto& lst = ResourceManager::getInstance().getResource<Lst, std::string>("art/tiles/tiles.lst");

    for (auto tileNumber = 0U; tileNumber < Map::TILES_PER_ELEVATION; ++tileNumber) {
        auto tile = _map->getMapFile().tiles.at(_currentElevation).at(tileNumber);

        // Convert tile number to hex grid coordinates using utility function
        auto coords = indexToCoordinates(static_cast<int>(tileNumber));

        // Convert to screen coordinates using utility function
        auto screenPos = coordinatesToScreenPosition(coords);

        const auto& createTileSprite = [&](const uint16_t tile_id, int offset = 0) {
            sf::Sprite tile_sprite(ResourceManager::getInstance().texture("art/tiles/" + lst->at(tile_id)));
            tile_sprite.setPosition({ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) - offset });
            return tile_sprite;
        };

        // floor
        uint16_t floorId = tile.getFloor();
        if (floorId == Map::EMPTY_TILE) {
            sf::Sprite tile_sprite(ResourceManager::getInstance().texture("art/tiles/blank.frm"));
            tile_sprite.setPosition({ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) });
            _floorSprites[tileNumber] = tile_sprite;
        } else {
            _floorSprites[tileNumber] = createTileSprite(floorId);
        }

        // roof
        uint16_t roofId = tile.getRoof();
        if (roofId == Map::EMPTY_TILE) {
            sf::Sprite tile_sprite(ResourceManager::getInstance().texture("art/tiles/blank.frm"));
            tile_sprite.setPosition({ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) - ROOF_OFFSET });
            // Make empty roof tiles fully transparent by default
            tile_sprite.setColor(geck::TileColors::transparent());
            _roofSprites[tileNumber] = tile_sprite;
        } else {
            _roofSprites[tileNumber] = createTileSprite(roofId, ROOF_OFFSET);
        }
    }
}

void EditorWidget::loadSprites() {
    spdlog::stopwatch sw;

    if (_sfmlWidget && _sfmlWidget->getRenderWindow()) {
        _sfmlWidget->getRenderWindow()->setTitle(_map->filename() + " - Gecko");
    }

    _objects.clear();

    // Data
    loadTileSprites();
    loadObjectSprites();

    // Rebuild spatial index after sprites are loaded
    _selectionManager->initializeSpatialIndex();

    spdlog::info("Map sprites loaded in {:.3} seconds", sw);
}

void EditorWidget::centerViewOnMap() {
    // Calculate map center based on how tiles are positioned in loadTileSprites()
    constexpr int centerTileX = MAP_WIDTH / 2;
    constexpr int centerTileY = MAP_HEIGHT / 2;

    float centerX = (MAP_WIDTH - centerTileY - 1) * TILE_X_OFFSET + TILE_Y_OFFSET_LARGE * (centerTileX - 1);
    float centerY = centerTileX * TILE_Y_OFFSET_SMALL + (centerTileY - 1) * TILE_Y_OFFSET_TINY + 1;

    _view.setCenter({ centerX, centerY });
    spdlog::debug("EditorWidget::centerViewOnMap() - Set view center to ({:.1f}, {:.1f})", centerX, centerY);
}

// New improved object selection methods
std::vector<std::shared_ptr<Object>> EditorWidget::getObjectsAtPosition(sf::Vector2f worldPos) {
    std::vector<std::shared_ptr<Object>> objectsAtPos;

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
                return posA > posB; // Higher position = front
            }

            // Same position - use object type priority
            auto getTypePriority = [](uint32_t pid) -> int {
                unsigned int typeId = pid >> FileFormat::TYPE_MASK_SHIFT;
                switch (static_cast<Pro::OBJECT_TYPE>(typeId)) {
                    case Pro::OBJECT_TYPE::SCENERY:
                        return 3; // Highest priority
                    case Pro::OBJECT_TYPE::WALL:
                        return 2;
                    case Pro::OBJECT_TYPE::ITEM:
                        return 1;
                    case Pro::OBJECT_TYPE::CRITTER:
                        return 1;
                    case Pro::OBJECT_TYPE::TILE:
                        return 1;
                    case Pro::OBJECT_TYPE::MISC:
                        return 1;
                    default:
                        return 0;
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
                case Pro::OBJECT_TYPE::ITEM:
                    typeName = "ITEM";
                    break;
                case Pro::OBJECT_TYPE::CRITTER:
                    typeName = "CRITTER";
                    break;
                case Pro::OBJECT_TYPE::SCENERY:
                    typeName = "SCENERY";
                    break;
                case Pro::OBJECT_TYPE::WALL:
                    typeName = "WALL";
                    break;
                case Pro::OBJECT_TYPE::TILE:
                    typeName = "TILE";
                    break;
                case Pro::OBJECT_TYPE::MISC:
                    typeName = "MISC";
                    break;
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
    if (!isPointInSpriteBounds(worldPos, sprite)) {
        return false;
    }

    // Get sprite bounds and texture
    const auto bounds = sprite.getGlobalBounds();
    const auto& texture = sprite.getTexture();

    // Convert world position to sprite-local coordinates
    sf::Vector2f localPos = worldPos - sf::Vector2f(bounds.position.x, bounds.position.y);

    // Apply sprite's texture rectangle and scale
    const auto textureRect = sprite.getTextureRect();
    const auto scale = sprite.getScale();

    // Convert to texture coordinates
    unsigned int texX = static_cast<unsigned int>((localPos.x / scale.x) + textureRect.position.x);
    unsigned int texY = static_cast<unsigned int>((localPos.y / scale.y) + textureRect.position.y);

    // Get texture size
    const auto texSize = texture.getSize();

    // Bounds check
    if (texX >= texSize.x || texY >= texSize.y) {
        return false;
    }

    // Get the image and check pixel transparency
    const auto image = texture.copyToImage();
    const auto pixel = image.getPixel({ texX, texY });

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
    float distance = std::sqrt(std::pow(worldPos.x - _lastClickPosition.x, 2) + std::pow(worldPos.y - _lastClickPosition.y, 2));

    bool isDouble = (timeSinceLastClick < DOUBLE_CLICK_TIME) && (distance < DOUBLE_CLICK_DISTANCE);

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

    // Reset roof sprites - empty tiles back to transparent, others to white
    for (int i = 0; i < static_cast<int>(_roofSprites.size()); ++i) {
        auto tile = _map->getMapFile().tiles.at(_currentElevation).at(i);
        if (tile.getRoof() == Map::EMPTY_TILE) {
            _roofSprites[i].setColor(geck::TileColors::transparent()); // Transparent
        } else {
            _roofSprites[i].setColor(sf::Color::White); // Opaque white
        }
    }

    // Clear roof tile selection background sprites
    _selectedRoofTileBackgroundSprites.clear();
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
        zoomFactor = 1.0f - ZOOM_STEP; // Zoom in
    } else {
        zoomFactor = 1.0f + ZOOM_STEP; // Zoom out
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

    if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mousePressed->button == sf::Mouse::Button::Left) {
            sf::Vector2f worldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                mousePressed->position, _view);

            // Check if we're in tile placement mode
            if (_tilePlacementMode && _tilePlacementIndex >= 0 && !_tilePlacementReplaceMode) {
                // Check if Shift is held to place roof tiles instead of floor tiles
                bool placeOnRoof = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);

                // Unified placement mode: prepare for either single or area placement
                _currentAction = EditorAction::TILE_PLACING;
                _dragStartWorldPos = worldPos;
                _isDragSelecting = false; // Will become true on first mouse move if dragging
                _tilePlacementIsRoof = placeOnRoof;

                // Don't place immediately - let mouse release handle single placement
                // and mouse move handle area drag detection
                return; // Don't process as selection
            }

            // Detect modifier keys for multi-selection
            SelectionModifier modifier = SelectionModifier::NONE;
            bool hasModifiers = false;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl)) {
                modifier = SelectionModifier::TOGGLE; // Ctrl+Click toggles items
                hasModifiers = true;
            } else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt)) {
                modifier = SelectionModifier::ADD; // Alt+Click (Option on macOS) adds items
                hasModifiers = true;
            } else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)) {
                modifier = SelectionModifier::RANGE; // Shift+Click range selection
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
                bool canDragSelect = !hasModifiers && (_currentSelectionMode == SelectionMode::ALL || _currentSelectionMode == SelectionMode::FLOOR_TILES || _currentSelectionMode == SelectionMode::ROOF_TILES || _currentSelectionMode == SelectionMode::ROOF_TILES_ALL || _currentSelectionMode == SelectionMode::OBJECTS);

                if (canDragSelect) {
                    // Start drag selection
                    _currentAction = EditorAction::DRAG_SELECTING;
                    _dragStartWorldPos = worldPos;
                    _isDragSelecting = false;             // Will become true on first mouse move
                    _immediateSelectionPerformed = false; // Reset flag
                } else {
                    // Immediate selection
                    selectAtPosition(worldPos, modifier);
                    _immediateSelectionPerformed = true; // Mark that immediate selection was performed
                }
            }
        } else if (mousePressed->button == sf::Mouse::Button::Right) {
            // Start panning
            _currentAction = EditorAction::PANNING;
            _mouseStartingPosition = mousePressed->position;
            _mouseLastPosition = _mouseStartingPosition;
        }
    } else if (const auto* mouseReleased = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (mouseReleased->button == sf::Mouse::Button::Left) {
            if (_currentAction == EditorAction::TILE_PLACING) {
                // Clear preview visuals first
                clearDragPreview();

                if (_isDragSelecting && _tilePlacementMode) {
                    // Complete tile area fill
                    sf::Vector2f worldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                        mouseReleased->position, _view);

                    // Create fill area
                    float left = std::min(_dragStartWorldPos.x, worldPos.x);
                    float top = std::min(_dragStartWorldPos.y, worldPos.y);
                    float width = std::abs(worldPos.x - _dragStartWorldPos.x);
                    float height = std::abs(worldPos.y - _dragStartWorldPos.y);
                    sf::FloatRect fillArea({ left, top }, { width, height });

                    // Fill area with selected tile
                    fillAreaWithTile(_tilePlacementIndex, fillArea, _tilePlacementIsRoof);

                    _isDragSelecting = false;
                } else if (_tilePlacementMode && _tilePlacementIndex >= 0) {
                    // Single tile placement - no dragging occurred
                    placeTileAtPosition(_tilePlacementIndex, _dragStartWorldPos, _tilePlacementIsRoof);
                    spdlog::debug("Placed single tile at mouse release");
                }

                _currentAction = EditorAction::NONE;
            } else if (_currentAction == EditorAction::DRAG_SELECTING) {
                // Clear preview visuals first
                clearDragPreview();

                if (_isDragSelecting) {
                    // Complete drag selection
                    sf::Vector2f worldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                        mouseReleased->position, _view);

                    // Create selection area
                    float left = std::min(_dragStartWorldPos.x, worldPos.x);
                    float top = std::min(_dragStartWorldPos.y, worldPos.y);
                    float width = std::abs(worldPos.x - _dragStartWorldPos.x);
                    float height = std::abs(worldPos.y - _dragStartWorldPos.y);
                    sf::FloatRect selectionArea({ left, top }, { width, height });

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
                    mouseReleased->position, _view);

                if (_isDraggingObjects) {
                    // Complete the drag operation
                    finishObjectDrag(worldPos);
                }

                _currentAction = EditorAction::NONE;
            }
        } else if (mouseReleased->button == sf::Mouse::Button::Right) {
            // Stop panning
            _currentAction = EditorAction::NONE;
        }
    } else if (const auto* mouseMoved = event.getIf<sf::Event::MouseMoved>()) {
        if (_currentAction == EditorAction::PANNING) {
            // Calculate pan delta
            sf::Vector2i currentPos = mouseMoved->position;
            sf::Vector2i delta = _mouseLastPosition - currentPos;

            // Scale panning delta by zoom level for consistent visual speed
            // When zoomed out (_zoomLevel > 1.0), we need to move more in world coordinates
            // When zoomed in (_zoomLevel < 1.0), we need to move less for precision
            float panScale = _zoomLevel;
            sf::Vector2f scaledDelta = {
                static_cast<float>(delta.x) * panScale,
                static_cast<float>(delta.y) * panScale
            };

            // Pan the view with zoom-scaled movement
            _view.move(scaledDelta);

            _mouseLastPosition = currentPos;
        } else if (_currentAction == EditorAction::DRAG_SELECTING) {
            // Update drag selection rectangle
            sf::Vector2f currentWorldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                mouseMoved->position, _view);

            // Mark as actively dragging if we've moved enough
            float distance = std::sqrt(std::pow(currentWorldPos.x - _dragStartWorldPos.x, 2) + std::pow(currentWorldPos.y - _dragStartWorldPos.y, 2));
            if (distance > DRAG_START_THRESHOLD) {
                _isDragSelecting = true;
            }

            if (_isDragSelecting) {
                // Update selection rectangle
                float left = std::min(_dragStartWorldPos.x, currentWorldPos.x);
                float top = std::min(_dragStartWorldPos.y, currentWorldPos.y);
                float width = std::abs(currentWorldPos.x - _dragStartWorldPos.x);
                float height = std::abs(currentWorldPos.y - _dragStartWorldPos.y);

                _selectionRectangle.setPosition({ left, top });
                _selectionRectangle.setSize(sf::Vector2f(width, height));

                // Update preview of items that would be selected
                updateDragSelectionPreview(currentWorldPos);
            }
        } else if (_currentAction == EditorAction::TILE_PLACING) {
            // Handle tile area fill drag
            if (_tilePlacementMode) {
                sf::Vector2f currentWorldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                    mouseMoved->position, _view);

                // Mark as actively dragging if we've moved enough
                float distance = std::sqrt(std::pow(currentWorldPos.x - _dragStartWorldPos.x, 2) + std::pow(currentWorldPos.y - _dragStartWorldPos.y, 2));
                if (distance > DRAG_START_THRESHOLD) {
                    _isDragSelecting = true;
                }

                if (_isDragSelecting) {
                    // Update selection rectangle for tile area fill (same as selection mode)
                    float left = std::min(_dragStartWorldPos.x, currentWorldPos.x);
                    float top = std::min(_dragStartWorldPos.y, currentWorldPos.y);
                    float width = std::abs(currentWorldPos.x - _dragStartWorldPos.x);
                    float height = std::abs(currentWorldPos.y - _dragStartWorldPos.y);

                    _selectionRectangle.setPosition({ left, top });
                    _selectionRectangle.setSize(sf::Vector2f(width, height));

                    // Update tile area fill preview
                    updateTileAreaFillPreview(currentWorldPos);
                }
            }
        } else if (_currentAction == EditorAction::OBJECT_MOVING) {
            // Handle object drag movement
            sf::Vector2f currentWorldPos = _sfmlWidget->getRenderWindow()->mapPixelToCoords(
                mouseMoved->position, _view);

            // Mark as actively dragging if we've moved enough
            float distance = std::sqrt(std::pow(currentWorldPos.x - _dragStartWorldPos.x, 2) + std::pow(currentWorldPos.y - _dragStartWorldPos.y, 2));
            if (distance > DRAG_START_THRESHOLD) {
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
                mouseMoved->position, _view);

            updateHoverHex(currentWorldPos);
        }
    } else if (const auto* wheelScrolled = event.getIf<sf::Event::MouseWheelScrolled>()) {
        // Zoom in/out with limits and sensitivity control
        if (wheelScrolled->wheel == sf::Mouse::Wheel::Vertical) {
            // Use smaller steps for smoother trackpad experience
            zoomView(wheelScrolled->delta);
        }
    } else if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
        // Arrow key movement
        switch (keyPressed->scancode) {
            case sf::Keyboard::Scancode::Left:
                // Left arrow key - move view left
                if (!(sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl))) {
                    float panScale = _zoomLevel;
                    _view.move({ -VIEW_MOVE_STEP * panScale, 0.0f });
                }
                break;

            case sf::Keyboard::Scancode::A:
                // Note: Ctrl+A should be handled by Qt menu shortcuts, not SFML
                // SFML only handles 'A' for movement (if not using WASD)
                // Removed Ctrl+A logic to prevent conflicts with Qt shortcuts
                break;
            case sf::Keyboard::Scancode::Right:
                // Right arrow key - move view right
                if (!(sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl))) {
                    float panScale = _zoomLevel;
                    _view.move({ VIEW_MOVE_STEP * panScale, 0.0f });
                }
                break;

            case sf::Keyboard::Scancode::D:
                // Note: Ctrl+D deselection is handled by Qt menu shortcut, not SFML
                // Do nothing here to avoid conflicts with Qt shortcut handling
                break;
            case sf::Keyboard::Scancode::Up:
            case sf::Keyboard::Scancode::W: {
                float panScale = _zoomLevel;
                _view.move({ 0.0f, -VIEW_MOVE_STEP * panScale });
            } break;
            case sf::Keyboard::Scancode::Down:
            case sf::Keyboard::Scancode::S: {
                float panScale = _zoomLevel;
                _view.move({ 0.0f, VIEW_MOVE_STEP * panScale });
            } break;
            case sf::Keyboard::Scancode::Home:
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
            case sf::Keyboard::Scancode::Escape:
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
    } else if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        // Update view size when window is resized
        float newWidth = static_cast<float>(resized->size.x);
        float newHeight = static_cast<float>(resized->size.y);

        // Preserve the current view center
        sf::Vector2f currentCenter = _view.getCenter();

        // Set the view to the new size at 1:1 scale
        _view.setSize({ newWidth, newHeight });
        _view.setCenter(currentCenter);

        // Reapply the current zoom level by scaling the view size directly
        if (_zoomLevel != 1.0f) {
            _view.zoom(_zoomLevel);
        }
    }
}

void EditorWidget::update([[maybe_unused]] const float dt) {
    // Update game logic here
    // This is called by the SFMLWidget's update loop
}

void EditorWidget::render([[maybe_unused]] const float dt) {
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

    // Render hex grid overlay
    renderHexGrid();

    // Render objects with visibility filtering
    if (_showObjects) {
        for (const auto& object : _objects) {
            window->draw(object->getSprite());
        }
    }
    
    // Render drag preview object if active
    if (_isDraggingFromPalette && _dragPreviewObject) {
        window->draw(_dragPreviewObject->getSprite());
    }

    // Render roof tiles
    if (_showRoof) {
        // First draw background sprites for selected roof tiles (blank.frm tiles)
        for (const auto& backgroundSprite : _selectedRoofTileBackgroundSprites) {
            window->draw(backgroundSprite);
        }

        // Then draw the roof sprites on top
        for (const auto& roof : _roofSprites) {
            window->draw(roof);
        }
    }

    // Render drag selection rectangle
    if (_isDragSelecting && (_currentAction == EditorAction::DRAG_SELECTING || _currentAction == EditorAction::TILE_PLACING)) {
        window->draw(_selectionRectangle);
    }

    // Render hex highlight if there's a valid hover hex
    if (_currentHoverHex >= 0) {
        // Find the hex with the matching position
        for (const auto& hex : _hexgrid.grid()) {
            if (hex.position() == static_cast<uint32_t>(_currentHoverHex)) {
                float spriteX = static_cast<float>(hex.x() - 16);
                float spriteY = static_cast<float>(hex.y() - 8);

                _hexHighlightSprite.setPosition({ spriteX, spriteY });
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
    for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); i++) {
        auto& sprites = isRoof ? _roofSprites : _floorSprites;
        const sf::Sprite& tileSprite = sprites.at(i);

        if (isSpriteClicked(worldPos, tileSprite)) {
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

void EditorWidget::selectAll() {
    if (_selectionManager) {
        _selectionManager->selectAll(_currentSelectionMode, _currentElevation);
    }
}

void EditorWidget::clearSelection() {
    if (_selectionManager) {
        _selectionManager->clearSelection();
    }
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

        const std::string tileName = tileList->list()[tileIndex];
        std::string tilePath = "art/tiles/" + tileName;
        const auto& texture = resourceManager.texture(tilePath);

        // Update the sprite
        sprites[hexIndex].setTexture(texture);

        // Calculate position using utility function (eliminates duplicate code)
        auto screenPos = indexToScreenPosition(hexIndex, isRoof);
        sprites[hexIndex].setPosition({ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) });

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
    if (minDistance < TILE_CLICK_DISTANCE_THRESHOLD * TILE_CLICK_DISTANCE_THRESHOLD) {
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

    for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); i++) {
        // Use the actual sprite's bounds instead of a fake sprite
        const sf::Sprite& tileSprite = sprites.at(i);

        if (isSpriteClicked(worldPos, tileSprite)) {
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
    for (int i = 0; i < static_cast<int>(Map::TILES_PER_ELEVATION); ++i) {
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
        if (item.type == selection::SelectionType::FLOOR_TILE || item.type == selection::SelectionType::ROOF_TILE) {
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
        { left - AREA_SELECTION_X_PADDING, top - AREA_SELECTION_Y_PADDING },
        { (right - left) + AREA_SELECTION_X_TOTAL_PADDING, (bottom - top) + AREA_SELECTION_Y_TOTAL_PADDING });

    // Determine selection mode based on current mode
    SelectionMode areaMode = _currentSelectionMode;
    if (_currentSelectionMode == SelectionMode::ALL) {
        // For ALL mode, default to floor tiles for range selection
        areaMode = SelectionMode::FLOOR_TILES;
    }

    // Use the SelectionManager's area selection functionality
    auto result = _selectionManager->selectArea(selectionArea, areaMode, _currentElevation);

    spdlog::info("Range selection: area ({:.1f}, {:.1f}, {:.1f}, {:.1f})",
        selectionArea.position.x, selectionArea.position.y, selectionArea.size.x, selectionArea.size.y);

    return result;
}

void EditorWidget::updateDragSelectionPreview(sf::Vector2f currentWorldPos) {
    // Clear previous preview
    clearDragPreview();

    // Create selection area
    float left = std::min(_dragStartWorldPos.x, currentWorldPos.x);
    float top = std::min(_dragStartWorldPos.y, currentWorldPos.y);
    float width = std::abs(currentWorldPos.x - _dragStartWorldPos.x);
    float height = std::abs(currentWorldPos.y - _dragStartWorldPos.y);
    sf::FloatRect selectionArea({ left, top }, { width, height });

    // Get items that would be selected
    switch (_currentSelectionMode) {
        case SelectionMode::FLOOR_TILES: {
            _previewTiles = _selectionManager->getTilesInArea(selectionArea, false, _currentElevation);
            // Apply preview coloring to floor tiles
            for (int tileIndex : _previewTiles) {
                if (tileIndex >= 0 && tileIndex < static_cast<int>(Map::TILES_PER_ELEVATION)) {
                    applyPreviewHighlight(_floorSprites.at(tileIndex));
                }
            }
            break;
        }

        case SelectionMode::ROOF_TILES: {
            _previewTiles = _selectionManager->getTilesInArea(selectionArea, true, _currentElevation);
            // Apply preview coloring to roof tiles
            for (int tileIndex : _previewTiles) {
                if (tileIndex >= 0 && tileIndex < static_cast<int>(Map::TILES_PER_ELEVATION)) {
                    applyPreviewHighlight(_roofSprites.at(tileIndex));
                }
            }
            break;
        }

        case SelectionMode::ROOF_TILES_ALL: {
            _previewTiles = _selectionManager->getTilesInAreaIncludingEmpty(selectionArea, true, _currentElevation);
            // Apply preview coloring to roof tiles including empty ones
            for (int tileIndex : _previewTiles) {
                if (tileIndex >= 0 && tileIndex < static_cast<int>(Map::TILES_PER_ELEVATION)) {
                    // Apply preview coloring to roof sprite (makes empty tiles visible if they were transparent)
                    applyPreviewHighlight(_roofSprites.at(tileIndex));
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
                if (tileIndex >= 0 && tileIndex < static_cast<int>(Map::TILES_PER_ELEVATION)) {
                    applyPreviewHighlight(_floorSprites.at(tileIndex));
                }
            }

            // Preview roof tiles
            auto roofTiles = _selectionManager->getTilesInArea(selectionArea, true, _currentElevation);
            for (int tileIndex : roofTiles) {
                if (tileIndex >= 0 && tileIndex < static_cast<int>(Map::TILES_PER_ELEVATION)) {
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
    sf::FloatRect selectionArea({ left, top }, { width, height });

    // Get tiles that would be affected by the area fill (default to floor tiles)
    bool isRoof = _tilePlacementIsRoof;
    _previewTiles = _selectionManager->getTilesInArea(selectionArea, isRoof, _currentElevation);

    // Apply preview coloring to tiles (same as selection mode)
    auto& sprites = isRoof ? _roofSprites : _floorSprites;
    for (int tileIndex : _previewTiles) {
        if (tileIndex >= 0 && tileIndex < static_cast<int>(Map::TILES_PER_ELEVATION)) {
            applyPreviewHighlight(sprites.at(tileIndex));
        }
    }
}

void EditorWidget::clearDragPreview() {
    // Clear tile preview coloring
    for (int tileIndex : _previewTiles) {
        if (tileIndex >= 0 && tileIndex < static_cast<int>(Map::TILES_PER_ELEVATION)) {
            removePreviewHighlight(_floorSprites.at(tileIndex));

            // For roof sprites, check if it's empty and set back to transparent
            auto tile = _map->getMapFile().tiles.at(_currentElevation).at(tileIndex);
            if (tile.getRoof() == Map::EMPTY_TILE) {
                _roofSprites.at(tileIndex).setColor(geck::TileColors::transparent()); // Transparent
            } else {
                removePreviewHighlight(_roofSprites.at(tileIndex));
            }
        }
    }

    // Clear object preview coloring
    for (auto& object : _previewObjects) {
        if (object) {
            removePreviewHighlight(object->getSprite());
        }
    }

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
            auto currentHex = _hexgrid.getHexByPosition(static_cast<uint32_t>(currentHexPosition));
            if (currentHex) {
                // Store hex center position, not sprite position
                _objectDragStartPositions.push_back(sf::Vector2f(
                    static_cast<float>(currentHex->get().x()),
                    static_cast<float>(currentHex->get().y())));
                spdlog::debug("Object at hex {} starts drag from hex center ({}, {})",
                    currentHexPosition, currentHex->get().x(), currentHex->get().y());
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

            object->getSprite().setPosition({ spriteX, spriteY });
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
            auto targetHex = _hexgrid.getHexByPosition(static_cast<uint32_t>(newHexPosition));
            if (targetHex) {
                // Update the map object's position index for saving
                int oldHexPosition = object->getMapObject().position;

                // Use setHexPosition to properly position the object with FRM offsets
                object->setHexPosition(targetHex->get());

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
                auto originalHex = _hexgrid.getHexByPosition(static_cast<uint32_t>(originalHexPosition));
                if (originalHex) {
                    object->setHexPosition(originalHex->get());
                } else {
                    // Last resort: use sprite positioning
                    object->getSprite().setPosition(_objectDragStartPositions[i]);
                }
            }
        } else {
            // Invalid position - restore to original hex position
            int originalHexPosition = object->getMapObject().position;
            auto originalHex = _hexgrid.getHexByPosition(static_cast<uint32_t>(originalHexPosition));
            if (originalHex) {
                object->setHexPosition(originalHex->get());
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
            auto originalHex = _hexgrid.getHexByPosition(static_cast<uint32_t>(originalHexPosition));
            if (originalHex) {
                _draggedObjects[i]->setHexPosition(originalHex->get());
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
    // Check for negative coordinates that would wrap when cast to uint32_t
    if (worldPos.x < 0 || worldPos.y < 0) {
        return -1;
    }

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
        spdlog::debug("renderHexGrid: No render window available");
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

    // Iterate through hex grid
    for (int y = 0; y < HexagonGrid::GRID_HEIGHT; y++) {
        for (int x = 0; x < HexagonGrid::GRID_WIDTH; x++) {
            // Convert hex coordinates to world coordinates using existing HexagonGrid
            int hexIndex = y * HexagonGrid::GRID_WIDTH + x;
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
            if ((hexWorldX + Hex::HEX_WIDTH * 2 > worldX && hexWorldX < worldX + viewWidth) && (hexWorldY + Hex::HEX_HEIGHT + 4 > worldY && hexWorldY < worldY + viewHeight)) {

                // Position hex sprite
                float spriteX = static_cast<float>(hexWorldX - Hex::HEX_WIDTH);
                float spriteY = static_cast<float>(hexWorldY - Hex::HEX_HEIGHT + 4);

                _hexSprite.setPosition({ spriteX, spriteY });
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

const sf::Texture& EditorWidget::createHexTexture() {
    ResourceManager::getInstance().loadResource<Frm>("art/tiles/HEX.frm");

    return ResourceManager::getInstance().texture("art/tiles/HEX.frm");
}

const sf::Texture& EditorWidget::createCursorHexTexture() {
    // Use the same HEX.frm texture as the normal hex sprite
    return createHexTexture();
}

const sf::Texture& EditorWidget::createBlankTexture() {
    // Use ResourceManager to handle the blank texture to avoid static OpenGL resources
    static std::string blankTextureName = "__blank_texture__";

    try {
        return ResourceManager::getInstance().texture(blankTextureName);
    } catch (const std::exception&) {
        // Create blank texture and store it in ResourceManager
        auto& resourceManager = ResourceManager::getInstance();
        sf::Image blankImage{ sf::Vector2u{ 1, 1 }, sf::Color::Transparent };

        auto texture = std::make_unique<sf::Texture>();
        [[maybe_unused]] bool loadSuccess = texture->loadFromImage(blankImage);

        // Store in ResourceManager's texture cache using the new method
        resourceManager.storeTexture(blankTextureName, std::move(texture));

        return resourceManager.texture(blankTextureName);
    }
}

void EditorWidget::placeObjectAtPosition(sf::Vector2f worldPos) {
    if (!_map) {
        spdlog::warn("EditorWidget: Cannot place object - no map loaded");
        return;
    }

    // Convert world position to hex position
    int hexPosition = worldPosToHexPosition(worldPos);
    if (hexPosition < 0 || hexPosition >= (HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT)) {
        spdlog::warn("EditorWidget: Invalid hex position {} for object placement", hexPosition);
        return;
    }

    // Create a MapObject for the placed object using shared_ptr (same as existing objects)
    auto mapObject = std::make_shared<MapObject>();
    
    // Set basic properties
    mapObject->position = hexPosition;
    mapObject->elevation = _currentElevation;
    mapObject->direction = 0;
    mapObject->frame_number = 0;
    
    // Set world coordinates
    auto hexCoords = _hexgrid.getHexByPosition(static_cast<uint32_t>(hexPosition));
    if (hexCoords) {
        mapObject->x = static_cast<uint32_t>(hexCoords->get().x());
        mapObject->y = static_cast<uint32_t>(hexCoords->get().y());
    }
    
    // Only use actual PIDs from ObjectInfo - fail if not available
    if (!_previewObjectInfo || !_previewObjectInfo->pro) {
        spdlog::error("EditorWidget: Cannot place object - no ObjectInfo or PRO data available");
        return;
    }
    
    mapObject->pro_pid = _previewObjectInfo->pro->header.PID;
    mapObject->frm_pid = _previewObjectInfo->pro->header.FID;

    // Initialize remaining fields
    mapObject->sx = 0;
    mapObject->sy = 0;
    mapObject->flags = 0;
    mapObject->critter_index = -1;
    mapObject->light_radius = 0;
    mapObject->light_intensity = 0;
    mapObject->outline_color = 0;
    mapObject->map_scripts_pid = -1;
    mapObject->script_id = -1;
    mapObject->objects_in_inventory = 0;
    mapObject->max_inventory_size = 0;
    mapObject->amount = 1;
    mapObject->unknown10 = 0;
    mapObject->unknown11 = 0;

    // Add to map for saving (both MapFile and Map's objects collection)
    auto& mapFile = _map->getMapFile();
    mapFile.map_objects[_currentElevation].push_back(mapObject);
    
    // Create visual Object for immediate display
    try {
        // Try to load FRM for the object if we have ObjectInfo
        const Frm* frm = nullptr;
        auto& resourceManager = ResourceManager::getInstance();
        if (_previewObjectInfo && !_previewObjectInfo->frmPath.isEmpty()) {
            frm = resourceManager.loadResource<Frm>(_previewObjectInfo->frmPath.toStdString());
        }
        
        // Create Object instance same way as existing objects
        auto object = std::make_shared<Object>(frm);
        
        // Set the MapObject association (same as existing objects)
        object->setMapObject(mapObject);
        
        // Set sprite texture if FRM was loaded successfully
        if (frm && _previewObjectInfo && !_previewObjectInfo->frmPath.isEmpty()) {
            sf::Sprite objectSprite{ resourceManager.texture(_previewObjectInfo->frmPath.toStdString()) };
            object->setSprite(std::move(objectSprite));
            
            // Set direction to show correct frame (first frame of first direction)
            if (frm) {
                object->setDirection(static_cast<ObjectDirection>(0));
            }
        }
        
        // Set proper position using hex positioning
        // Use the hex grid system like existing objects
        if (hexPosition < static_cast<int>(_hexgrid.grid().size())) {
            const auto& hex = _hexgrid.grid()[hexPosition];
            object->setHexPosition(hex);
        }
        
        // Add to objects list for immediate display
        _objects.push_back(object);
        
            
    } catch (const std::exception& e) {
        spdlog::warn("EditorWidget: Failed to create visual object for placed item: {}", e.what());
        // The MapObject is still saved, just won't be visible until reload
    }
    
}

void EditorWidget::startDragPreview(int objectIndex, int categoryInt, sf::Vector2f worldPos) {
    // Cancel any existing drag preview
    cancelDragPreview();
    
    // Set drag preview state
    _isDraggingFromPalette = true;
    _previewObjectIndex = objectIndex;
    _previewObjectCategory = categoryInt;
    
    try {
        // Get ObjectInfo from ObjectPalettePanel
        const ObjectInfo* objectInfo = nullptr;
        if (_mainWindow) {
            ObjectPalettePanel* palettePanel = _mainWindow->getObjectPalettePanel();
            if (palettePanel) {
                objectInfo = palettePanel->getObjectInfo(objectIndex, static_cast<ObjectCategory>(categoryInt));
                _previewObjectInfo = objectInfo;
                
                if (!objectInfo) {
                    spdlog::warn("EditorWidget: No ObjectInfo found for index {} in category {}", objectIndex, categoryInt);
                }
            }
        }
        
        // Try to load actual FRM data
        sf::Sprite previewSprite(createBlankTexture());
        
        // Load actual FRM if ObjectInfo is available
        if (objectInfo && !objectInfo->frmPath.isEmpty()) {
            try {
                auto& resourceManager = ResourceManager::getInstance();
                const auto* frm = resourceManager.loadResource<Frm>(objectInfo->frmPath.toStdString());
                
                if (frm) {
                    // Create Object with actual FRM
                    _dragPreviewObject = std::make_shared<Object>(frm);
                    
                    // Load sprite texture same way as existing objects
                    sf::Sprite objectSprite{ resourceManager.texture(objectInfo->frmPath.toStdString()) };
                    objectSprite.setColor(sf::Color(255, 255, 255, 180)); // Semi-transparent for preview
                    _dragPreviewObject->setSprite(std::move(objectSprite));
                    
                    // Set direction to show correct frame (first frame of first direction)
                    if (frm) {
                        _dragPreviewObject->setDirection(static_cast<ObjectDirection>(0));
                        
                    }
                    
                } else {
                    spdlog::warn("EditorWidget: Failed to load FRM for drag preview");
                    cancelDragPreview();
                    return;
                }
            } catch (const std::exception& e) {
                spdlog::warn("EditorWidget: Failed to load FRM {}: {}", objectInfo->frmPath.toStdString(), e.what());
                cancelDragPreview();
                return;
            }
        } else {
            spdlog::warn("EditorWidget: No ObjectInfo available for drag preview");
            cancelDragPreview();
            return;
        }
        
        // Position the preview object initially
        updateDragPreview(worldPos);
        
            
    } catch (const std::exception& e) {
        spdlog::warn("EditorWidget: Failed to create drag preview: {}", e.what());
        cancelDragPreview();
    }
}

void EditorWidget::updateDragPreview(sf::Vector2f worldPos) {
    if (!_isDraggingFromPalette || !_dragPreviewObject) {
        return;
    }
    
    // Find the closest hex position directly
    int hexPosition = worldPosToHexPosition(worldPos);
    
    if (hexPosition >= 0 && hexPosition < (HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT)) {
        // Update hover hex for visual feedback
        _currentHoverHex = hexPosition;
        
        // Position the preview object at the hex center with proper FRM offsets
        auto hex = _hexgrid.getHexByPosition(static_cast<uint32_t>(hexPosition));
        if (hex) {
            _dragPreviewObject->setHexPosition(hex->get());
        }
        
        spdlog::debug("EditorWidget: Drag preview at hex position {}", hexPosition);
    } else {
        _currentHoverHex = -1;
        spdlog::debug("EditorWidget: Drag preview at invalid position ({})", hexPosition);
    }
}

void EditorWidget::finishDragPreview(sf::Vector2f worldPos) {
    if (!_isDraggingFromPalette) {
        return;
    }
    
    // Convert to hex position
    int hexPosition = worldPosToHexPosition(worldPos);
    if (hexPosition >= 0 && hexPosition < (HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT)) {
        // Place the actual object
        placeObjectAtPosition(worldPos);
        
        spdlog::info("EditorWidget: Finished drag preview - placed object at hex {}", hexPosition);
    } else {
        spdlog::warn("EditorWidget: Invalid drop position for drag preview (hex position: {})", hexPosition);
    }
    
    // Clean up preview
    cancelDragPreview();
}

void EditorWidget::cancelDragPreview() {
    _isDraggingFromPalette = false;
    _dragPreviewObject.reset();
    _previewObjectIndex = -1;
    _previewObjectCategory = 0;
    _previewObjectInfo = nullptr;
    _currentHoverHex = -1;
}

} // namespace geck