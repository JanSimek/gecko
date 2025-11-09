#include "EditorWidget.h"
#include "../widgets/SFMLWidget.h"
#include "../input/InputHandler.h"
#include "../rendering/RenderingEngine.h"
#include "../dragdrop/DragDropManager.h"
#include "../tiles/TilePlacementManager.h"
#include "../tools/ExitGridPlacementManager.h"
#include "../viewport/ViewportController.h"
#include "../panels/ObjectPalettePanel.h"
#include "../panels/TilePalettePanel.h"
#include "MainWindow.h"

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstdlib>
#include <set>

#include "../../util/Constants.h"
#include "../../util/ResourcePaths.h"
#include "../../util/ColorUtils.h"
#include "../../util/ResourceInitializer.h"
#include "../../util/TileUtils.h"
#include "../../util/QtDialogs.h"
#include "../../util/ProHelper.h"
#include "../../util/SpriteFactory.h"
#include "../../util/Coordinates.h"

#include "../../editor/Object.h"
#include "../../editor/HexagonGrid.h"

#include "../../format/frm/Frm.h"
#include "../../format/lst/Lst.h"
#include "../../format/map/Tile.h"
#include "../../format/pro/Pro.h"
#include "../../format/map/MapObject.h"

#include "../../writer/map/MapWriter.h"

namespace geck {

EditorWidget::EditorWidget(std::unique_ptr<Map> map, QWidget* parent)
    : QWidget(parent)
    , _layout(nullptr)
    , _sfmlWidget(nullptr)
    , _mainWindow(nullptr)
    , _map(std::move(map))
    , _hexSprite(createHexTexture())
    , _hexHighlightSprite(createCursorHexTexture())
    , _playerPositionSprite(createCursorHexTexture()) {

    // Set texture rectangle to show only half of HEX.frm (right half for highlighting)
    sf::Vector2u textureSize = _hexHighlightSprite.getTexture().getSize();
    _hexHighlightSprite.setTextureRect(
        sf::IntRect(
            sf::Vector2i(static_cast<int>(textureSize.x / 2), 0),
            sf::Vector2i(static_cast<int>(textureSize.x / 2), static_cast<int>(textureSize.y))));
    _hexHighlightSprite.setColor(sf::Color(Colors::ERROR_R, Colors::ERROR_G, Colors::ERROR_B, 255));
    
    // Set up player position marker (blue color for visibility)
    _playerPositionSprite.setTextureRect(
        sf::IntRect(
            sf::Vector2i(static_cast<int>(textureSize.x / 2), 0),
            sf::Vector2i(static_cast<int>(textureSize.x / 2), static_cast<int>(textureSize.y))));
    _playerPositionSprite.setColor(sf::Color(PlayerColors::POSITION_R, PlayerColors::POSITION_G, PlayerColors::POSITION_B, PlayerColors::POSITION_ALPHA)); // Semi-transparent blue

    // Initialize sprite vectors - will be populated when sprites are loaded
    _floorSprites.reserve(Map::TILES_PER_ELEVATION);
    _roofSprites.reserve(Map::TILES_PER_ELEVATION);

    // Initialize selection management only if map is provided
    if (_map) {
        initializeSelectionSystem();
    }

    // Initialize rendering engine, input handler, drag/drop manager, tile placement manager, and viewport controller
    _renderingEngine = std::make_unique<RenderingEngine>();
    _inputHandler = std::make_unique<InputHandler>(this);
    _dragDropManager = std::make_unique<DragDropManager>(this);
    _tilePlacementManager = std::make_unique<TilePlacementManager>(this);
    _exitGridPlacementManager = std::make_unique<ExitGridPlacementManager>(this);
    _viewportController = std::make_unique<ViewportController>(&_hexgrid);
    setupInputCallbacks();

    setupUI();
    _viewportController->centerViewOnMap();
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
                    if (isValidTileIndex(tileIndex) &&
                        _map->getMapFile().tiles.find(_currentElevation) != _map->getMapFile().tiles.end()) {
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
                    if (isValidTileIndex(tileIndex)) {
                        this->_floorSprites.at(tileIndex).setColor(geck::ColorUtils::createErrorIndicatorColor());
                    }
                    break;
                }

                case selection::SelectionType::HEX: {
                    int hexIndex = item.getHexIndex();
                    if (hexIndex >= 0 && hexIndex < (HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT)) {
                        // Create a hex selection sprite using the hex highlight texture
                        if (hexIndex < static_cast<int>(_hexgrid.grid().size())) {
                            const auto& hex = _hexgrid.grid().at(hexIndex);
                            sf::Sprite hexSelectionSprite = _hexHighlightSprite; // Copy the highlight sprite
                            // Use the same positioning as hover highlight
                            float spriteX = static_cast<float>(hex.x() + SpriteOffset::HEX_HIGHLIGHT_X);
                            float spriteY = static_cast<float>(hex.y() + SpriteOffset::HEX_HIGHLIGHT_Y);
                            hexSelectionSprite.setPosition(sf::Vector2f(spriteX, spriteY));
                            // Use a different color for selection vs hover (blue for selection, red for hover)
                            hexSelectionSprite.setColor(sf::Color(SelectionColors::HEX_R, SelectionColors::HEX_G, SelectionColors::HEX_B, SelectionColors::HEX_ALPHA)); // Semi-transparent blue
                            this->_selectedHexSprites.push_back(hexSelectionSprite);
                        }
                    }
                    break;
                }
            }
        }

        // Emit unified selection update
        emit selectionChanged(selection, _currentElevation);
        
        // Publish to EventBus
        EventBus::getInstance().publish(SelectionChangedEvent{
            selection.items.empty() ? SelectionChangedEvent::Type::Cleared : SelectionChangedEvent::Type::Added,
            static_cast<int>(selection.items.size())
        });
    });

    // Register the observer with the selection manager
}

void EditorWidget::setupUI() {
    _layout = new QVBoxLayout(this);
    _layout->setContentsMargins(0, 0, 0, 0);
    _layout->setSpacing(0);

    // Container ensures dock splitter handles stay accessible when using the native SFML widget
    auto* renderingContainer = new QWidget(this);
    renderingContainer->setObjectName("sfmlRenderingContainer");
    renderingContainer->setContentsMargins(0, 0, 0, 0);
    renderingContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* containerLayout = new QVBoxLayout(renderingContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    // Create the SFML widget that will handle rendering
    _sfmlWidget = new SFMLWidget(renderingContainer);

    // Set this EditorWidget as the delegate for SFML event handling
    _sfmlWidget->setEditorWidget(this);
    _sfmlWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    containerLayout->addWidget(_sfmlWidget);
    _layout->addWidget(renderingContainer, 1);

    setLayout(_layout);
}

void EditorWidget::init() {
    // Only load sprites if we have a map
    if (_map) {
        loadSprites();

        // Show error summary if there were loading issues
        showLoadingErrorsSummary();

        // Initialize spatial index for O(1) area selection performance
        if (_selectionManager) {
            _selectionManager->initializeSpatialIndex();
        }
    }

    // Initialize selection rectangle for drag selection
    _selectionRectangle.setFillColor(TileColors::selectionFill());
    _selectionRectangle.setOutlineColor(TileColors::selectionOutline());
    _selectionRectangle.setOutlineThickness(2.0f);

    // Initialize viewport controller with current widget size
    sf::Vector2u windowSize(800, 600); // Default size
    if (_sfmlWidget) {
        windowSize = sf::Vector2u(
            static_cast<unsigned int>(_sfmlWidget->width()),
            static_cast<unsigned int>(_sfmlWidget->height())
        );
    }
    _viewportController->initialize(windowSize);
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
    spdlog::info("Creating new empty map");
    
    // Create a new empty MapFile
    auto newMapFile = std::make_unique<Map::MapFile>();
    
    // Initialize header with default values
    newMapFile->header.version = FileFormat::FALLOUT2_MAP_VERSION; // Standard Fallout 2 map version
    newMapFile->header.filename = "newmap"; // Default filename
    newMapFile->header.player_default_position = MapDefaults::PLAYER_DEFAULT_POSITION; // Center of map (hex 99,99 area)
    newMapFile->header.player_default_elevation = MapDefaults::PLAYER_DEFAULT_ELEVATION; // Ground level
    newMapFile->header.player_default_orientation = MapDefaults::PLAYER_DEFAULT_ORIENTATION; // North
    newMapFile->header.num_local_vars = 0;
    newMapFile->header.script_id = MapDefaults::NO_SCRIPT_ID; // No map script
    newMapFile->header.flags = MapDefaults::DEFAULT_FLAGS; // All elevations enabled
    newMapFile->header.darkness = MapDefaults::NO_DARKNESS; // No darkness
    newMapFile->header.num_global_vars = 0;
    newMapFile->header.map_id = MapDefaults::DEFAULT_MAP_ID; // Default map ID
    newMapFile->header.timestamp = MapDefaults::DEFAULT_TIMESTAMP;
    
    // Initialize empty variables
    newMapFile->map_local_vars.clear();
    newMapFile->map_global_vars.clear();
    
    // Initialize empty tiles for all elevations (0, 1, 2)
    for (int elevation = ELEVATION_1; elevation <= ELEVATION_3; elevation++) {
        std::vector<Tile> elevationTiles;
        elevationTiles.reserve(Map::TILES_PER_ELEVATION);
        
        // Create empty tiles (EMPTY_TILE = 1 for both floor and roof)
        for (unsigned int i = 0; i < Map::TILES_PER_ELEVATION; i++) {
            Tile tile(Map::EMPTY_TILE, Map::EMPTY_TILE); // Empty floor and roof tiles
            elevationTiles.push_back(tile);
        }
        
        newMapFile->tiles[elevation] = std::move(elevationTiles);
    }
    
    // Initialize empty scripts
    for (int i = 0; i < Map::SCRIPT_SECTIONS; i++) {
        newMapFile->map_scripts[i].clear();
        newMapFile->scripts_in_section[i] = 0;
    }
    
    // Initialize empty objects for all elevations
    for (int elevation = ELEVATION_1; elevation <= ELEVATION_3; elevation++) {
        newMapFile->map_objects[elevation].clear();
    }
    
    // Create a new Map instance with default filename for new map
    _map = std::make_unique<Map>(std::filesystem::path("newmap.map"));
    _map->setMapFile(std::move(newMapFile));
    
    // Reset current elevation to 0
    _currentElevation = 0;
    
    // Clear all existing visual data
    _objects.clear();
    _floorSprites.clear();
    _roofSprites.clear();
    _wallBlockerOverlays.clear();
    _selectedHexSprites.clear();
    
    // Load essential resources for empty map
    try {
        ResourceInitializer::loadEssentialResources();
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load some essential resources for new map: {}", e.what());
    }
    
    // Initialize selection system now that map is available
    if (!_selectionManager) {
        initializeSelectionSystem();
    }
    
    // Initialize sprites for the new empty map
    loadSprites();
    
    // Clear selection
    _selectionManager->clearSelection();
    
    // Center view on the map
    _viewportController->centerViewOnMap();
    
    // Update palettes and UI now that map is created
    if (_mainWindow) {
        _mainWindow->updateMapInfo(_map.get());
    }
    
    spdlog::info("Created new empty map with {} tiles per elevation", Map::TILES_PER_ELEVATION);
}

void EditorWidget::loadObjectSprites() {
    // Objects
    if (_map->objects().empty())
        return;

    size_t totalObjects = _map->objects().at(_currentElevation).size();
    size_t objectsLoaded = 0;
    size_t objectsSkipped = 0;
    
    spdlog::debug("Loading {} objects for elevation {}", totalObjects, _currentElevation);

    for (const auto& object : _map->objects().at(_currentElevation)) {
        if (object->position == -1)
            continue; // object inside an inventory/container

        // Special handling for wall blockers - use wallblock.frm based on blocking behavior
        std::string frm_name = ResourceManager::getInstance().FIDtoFrmName(object->frm_pid);

        if (frm_name.empty()) {
            spdlog::error("Empty FRM name for object at position {} (frm_pid=0x{:08X}, pro_pid=0x{:08X})", 
                         object->position, object->frm_pid, object->pro_pid);
            continue;
        }
        
        spdlog::debug("Loading object sprite: FRM='{}', position={}, direction={}, frm_pid=0x{:08X}, pro_pid=0x{:08X}", 
                     frm_name, object->position, object->direction, object->frm_pid, object->pro_pid);
        auto frm = ResourceManager::getInstance().getResource<Frm>(frm_name);

        if (!frm) {
            spdlog::debug("FRM '{}' not in cache, attempting on-demand loading", frm_name);
            try {
                frm = ResourceManager::getInstance().loadResource<Frm>(frm_name);
                if (!frm) {
                    spdlog::error("Failed to load FRM resource '{}' for object at position {} - resource still null after loading", frm_name, object->position);
                    _lastLoadErrors.failedFrmNames.insert(frm_name);
                    _lastLoadErrors.failedObjects.emplace_back(frm_name, object->position);
                    objectsSkipped++;
                    continue;
                }
                spdlog::debug("Successfully loaded FRM '{}' on-demand", frm_name);
            } catch (const std::exception& e) {
                spdlog::error("Failed to load FRM '{}' for object at position {}: {}", frm_name, object->position, e.what());
                _lastLoadErrors.failedFrmNames.insert(frm_name);
                _lastLoadErrors.failedObjects.emplace_back(frm_name, object->position);
                objectsSkipped++;
                continue;
            }
        }
        
        spdlog::debug("FRM '{}' available: {} directions, filename='{}'", 
                     frm_name, frm->directions().size(), frm->filename());

        try {
            _objects.emplace_back(std::make_shared<Object>(frm));
            sf::Sprite object_sprite{ ResourceManager::getInstance().texture(frm_name) };
            _objects.back()->setSprite(std::move(object_sprite));
            _objects.back()->setHexPosition(_hexgrid.grid().at(object->position));
            _objects.back()->setMapObject(object);
            _objects.back()->setDirection(static_cast<ObjectDirection>(object->direction));
            
            spdlog::debug("Successfully created object for FRM '{}' at position {}", frm_name, object->position);
            objectsLoaded++;
        } catch (const std::exception& e) {
            spdlog::error("Failed to create object for FRM '{}' at position {}: {}", 
                         frm_name, object->position, e.what());
            // Remove the partially created object if it was added
            if (!_objects.empty() && _objects.back() != nullptr) {
                _objects.pop_back();
            }
            _lastLoadErrors.failedFrmNames.insert(frm_name);
            _lastLoadErrors.failedObjects.emplace_back(frm_name, object->position);
            objectsSkipped++;
            continue; // Skip this object and continue with others
        }
    }
    
    // Create wall blocker overlays for objects that block movement (but aren't gap-filling blockers)
    _wallBlockerOverlays.clear();
    size_t overlaysCreated = 0;
    
    for (const auto& object : _map->objects().at(_currentElevation)) {
        if (object->position == -1) continue; // Skip inventory objects
        
        size_t overlayCountBefore = _wallBlockerOverlays.size();
        createWallBlockerOverlay(object, object->position);
        if (_wallBlockerOverlays.size() > overlayCountBefore) {
            overlaysCreated++;
        }
    }
    
    _lastLoadErrors.objectsSkipped = objectsSkipped;
    spdlog::info("Object loading complete for elevation {}: {} loaded, {} skipped, {} total, {} wall blocker overlays", 
                _currentElevation, objectsLoaded, objectsSkipped, totalObjects, overlaysCreated);
}

void EditorWidget::createWallBlockerOverlay(const std::shared_ptr<MapObject>& mapObject, int hexPosition) {
    // Only create overlays for regular objects that block movement
    // Gap-filling wall blockers (PIDs 620/621) already show wallblock.frm as their main sprite
    bool blocks = mapObject->blocksMovement();

    spdlog::debug("createWallBlockerOverlay: hex {}, pro_pid 0x{:08X}, blocks: {}",
                 hexPosition, mapObject->pro_pid, blocks);
    
    if (!blocks) {
        return; // No overlay needed
    }

    bool is_shoot_through = mapObject->isShootThroughWallBlocker();
    
    try {
        // Load wallblock.frm as overlay  
        std::string overlayFrmPath;
        if (is_shoot_through) {
            overlayFrmPath = std::string(ResourcePaths::Frm::WALL_BLOCK);
        } else {
            overlayFrmPath = std::string(ResourcePaths::Frm::WALL_BLOCK_FULL);
        }
        ResourceManager::getInstance().insertTexture(overlayFrmPath);
        
        sf::Sprite overlaySprite{ ResourceManager::getInstance().texture(overlayFrmPath) };
        
        // Position overlay at the specified hex position
        auto hex = _hexgrid.grid().at(hexPosition);
        float x = static_cast<float>(hex.x() + SpriteOffset::HEX_HIGHLIGHT_X); // Use same offset as hex selection
        float y = static_cast<float>(hex.y() + SpriteOffset::HEX_HIGHLIGHT_Y);
        overlaySprite.setPosition(sf::Vector2f(x, y));
        
        // Make overlay semi-transparent to show the object underneath
        overlaySprite.setColor(sf::Color(255, 255, 255, OverlayColors::WALL_BLOCKER_ALPHA));
        
        _wallBlockerOverlays.push_back(std::move(overlaySprite));
        
        spdlog::debug("Created wall blocker overlay for object at hex {} (pro_pid {})", 
                     hexPosition, mapObject->pro_pid);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to create wall blocker overlay for object at hex {}: {}", 
                    hexPosition, e.what());
    }
}

std::vector<int> EditorWidget::calculateRectangleBorderHexes(sf::FloatRect rectangle) {
    std::vector<int> borderHexes;
    
    // Convert rectangle corners to hex positions
    sf::Vector2f topLeft = sf::Vector2f(rectangle.position.x, rectangle.position.y);
    sf::Vector2f topRight = sf::Vector2f(rectangle.position.x + rectangle.size.x, rectangle.position.y);
    sf::Vector2f bottomLeft = sf::Vector2f(rectangle.position.x, rectangle.position.y + rectangle.size.y);
    sf::Vector2f bottomRight = sf::Vector2f(rectangle.position.x + rectangle.size.x, rectangle.position.y + rectangle.size.y);
    
    // Convert corner world positions to hex indices
    int topLeftHex = _viewportController->worldPosToHexIndex(topLeft);
    int topRightHex = _viewportController->worldPosToHexIndex(topRight);
    int bottomLeftHex = _viewportController->worldPosToHexIndex(bottomLeft);
    int bottomRightHex = _viewportController->worldPosToHexIndex(bottomRight);
    
    // Validate hex positions
    const int maxHex = HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT;
    if (topLeftHex < 0 || topLeftHex >= maxHex || 
        topRightHex < 0 || topRightHex >= maxHex ||
        bottomLeftHex < 0 || bottomLeftHex >= maxHex ||
        bottomRightHex < 0 || bottomRightHex >= maxHex) {
        spdlog::warn("Rectangle contains invalid hex positions, skipping border calculation");
        return borderHexes;
    }
    
    // Calculate hex grid coordinates for rectangle bounds
    int leftX = topLeftHex % HexagonGrid::GRID_WIDTH;
    int rightX = topRightHex % HexagonGrid::GRID_WIDTH;
    int topY = topLeftHex / HexagonGrid::GRID_WIDTH;
    int bottomY = bottomLeftHex / HexagonGrid::GRID_WIDTH;
    
    // Ensure proper ordering (left < right, top < bottom)
    if (leftX > rightX) std::swap(leftX, rightX);
    if (topY > bottomY) std::swap(topY, bottomY);
    
    std::set<int> uniqueHexes; // Use set to avoid duplicates
    
    // Top border (left to right)
    for (int x = leftX; x <= rightX; x++) {
        int hexPos = topY * HexagonGrid::GRID_WIDTH + x;
        if (hexPos >= 0 && hexPos < maxHex) {
            uniqueHexes.insert(hexPos);
        }
    }
    
    // Bottom border (left to right)
    if (bottomY != topY) {
        for (int x = leftX; x <= rightX; x++) {
            int hexPos = bottomY * HexagonGrid::GRID_WIDTH + x;
            if (hexPos >= 0 && hexPos < maxHex) {
                uniqueHexes.insert(hexPos);
            }
        }
    }
    
    // Left border (top to bottom, excluding corners already added)
    for (int y = topY + 1; y < bottomY; y++) {
        int hexPos = y * HexagonGrid::GRID_WIDTH + leftX;
        if (hexPos >= 0 && hexPos < maxHex) {
            uniqueHexes.insert(hexPos);
        }
    }
    
    // Right border (top to bottom, excluding corners already added)
    if (rightX != leftX) {
        for (int y = topY + 1; y < bottomY; y++) {
            int hexPos = y * HexagonGrid::GRID_WIDTH + rightX;
            if (hexPos >= 0 && hexPos < maxHex) {
                uniqueHexes.insert(hexPos);
            }
        }
    }
    
    // Convert set to vector
    borderHexes.assign(uniqueHexes.begin(), uniqueHexes.end());
    
    spdlog::debug("Calculated {} border hexes for rectangle ({}, {}, {}, {})", 
                 borderHexes.size(), rectangle.position.x, rectangle.position.y, rectangle.size.x, rectangle.size.y);
    
    return borderHexes;
}

std::shared_ptr<MapObject> EditorWidget::createScrollBlockerObject(int hexPosition) {
    auto mapObject = std::make_shared<MapObject>();
    
    // Set basic properties
    mapObject->position = hexPosition;
    mapObject->elevation = _currentElevation;
    mapObject->direction = 0;
    mapObject->frame_number = 0;

    // Set scroll blocker FRM PID (base ID = 1 for scrblk.frm)
    mapObject->frm_pid = 0x05000000 | WallBlockers::SCROLL_BLOCKER_BASE_ID; // MISC type (0x05) with base ID 1
    
    // Set proto PID to a valid MISC object proto
    mapObject->pro_pid = 0x05000000 | WallBlockers::GENERIC_PROTO_ID; // MISC type, generic small object
    
    // Set flags - scroll blockers don't block movement, just visual indicators
    mapObject->flags = 0;
    
    // Initialize other fields to default values
    mapObject->unknown0 = 0;
    mapObject->x = 0;
    mapObject->y = 0;
    mapObject->sx = 0;
    mapObject->sy = 0;
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
    
    return mapObject;
}

// Tiles
void EditorWidget::loadTileSprites() {
    // Clear previous sprites and reserve space
    _floorSprites.clear();
    _roofSprites.clear();
    _floorSprites.reserve(Map::TILES_PER_ELEVATION);
    _roofSprites.reserve(Map::TILES_PER_ELEVATION);

    // Check if current elevation exists in the map data
    if (!_map || _map->getMapFile().tiles.find(_currentElevation) == _map->getMapFile().tiles.end()) {
        spdlog::warn("EditorWidget::loadTileSprites: Current elevation {} does not exist in map data", _currentElevation);
        return;
    }

    for (auto tileNumber = 0U; tileNumber < Map::TILES_PER_ELEVATION; ++tileNumber) {
        const auto& tile = _map->getMapFile().tiles.at(_currentElevation).at(tileNumber);

        // Convert tile number to screen coordinates
        const auto coords = indexToCoordinates(static_cast<int>(tileNumber));
        const auto screenPos = coordinatesToScreenPosition(coords);

        // Create floor and roof sprites using factory
        _floorSprites.push_back(SpriteFactory::createFloorTileSprite(tile.getFloor(), screenPos));
        _roofSprites.push_back(SpriteFactory::createRoofTileSprite(tile.getRoof(), screenPos));
    }
}

void EditorWidget::loadSprites() {
    spdlog::stopwatch sw;
    
    // Clear previous loading errors
    _lastLoadErrors.clear();

    _objects.clear();
    _wallBlockerOverlays.clear();

    // Data
    loadTileSprites();
    loadObjectSprites();

    // Rebuild spatial index after sprites are loaded
    _selectionManager->initializeSpatialIndex();

    spdlog::info("Map sprites loaded in {:.3} seconds", sw);
}


// New improved object selection methods
std::vector<std::shared_ptr<Object>> EditorWidget::getObjectsAtPosition(sf::Vector2f worldPos) {
    std::vector<std::shared_ptr<Object>> objectsAtPos;

    std::ranges::copy_if(_objects, std::back_inserter(objectsAtPos),
        [this, worldPos](const auto& object) {
            return isPointInSpritePixel(worldPos, object->getSprite());
        });

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
            unsigned int typeId = pid >> FileFormat::TYPE_MASK_SHIFT;
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
    std::ranges::for_each(_objects, [](auto& object) {
        if (object) {
            object->unselect();
        }
    });

    // Clear all tile colors
    std::ranges::for_each(_floorSprites, [](auto& sprite) {
        sprite.setColor(sf::Color::White);
    });

    // Reset roof sprites - empty tiles back to transparent, others to white
    if (_map && _map->getMapFile().tiles.find(_currentElevation) != _map->getMapFile().tiles.end()) {
        for (int i = 0; i < static_cast<int>(_roofSprites.size()); ++i) {
            auto tile = _map->getMapFile().tiles.at(_currentElevation).at(i);
            if (tile.getRoof() == Map::EMPTY_TILE) {
                _roofSprites[i].setColor(geck::TileColors::transparent()); // Transparent
            } else {
                _roofSprites[i].setColor(sf::Color::White); // Opaque white
            }
        }
    } else {
        // If elevation doesn't exist, just set all roof sprites to white
        std::ranges::for_each(_roofSprites, [](auto& sprite) {
            sprite.setColor(sf::Color::White);
        });
    }

    // Clear roof tile selection background sprites
    _selectedRoofTileBackgroundSprites.clear();

    // Clear hex selection sprites
    _selectedHexSprites.clear();
}


// SFML Event handling interface (called by SFMLWidget)
void EditorWidget::handleEvent(const sf::Event& event) {
    // Handle resize events to maintain aspect ratio (SFML 3 syntax)
    if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        if (_viewportController) {
            _viewportController->updateViewForWindowSize(sf::Vector2u(resized->size.x, resized->size.y));
            spdlog::debug("EditorWidget: Handled window resize to {}x{}", resized->size.x, resized->size.y);
        }
    }
    
    // Delegate all other event handling to InputHandler
    if (_inputHandler && _sfmlWidget) {
        if (auto* target = _sfmlWidget->getRenderTarget()) {
            _inputHandler->handleEvent(event, *target, _viewportController->getView());
        }
    }
}

void EditorWidget::setupInputCallbacks() {
    if (!_inputHandler) return;
    
    InputHandler::Callbacks callbacks;
    
    // Mouse events
    callbacks.onSelectionClick = [this](sf::Vector2f worldPos, InputHandler::SelectionModifier modifier) {
        SelectionModifier selectionModifier;
        switch (modifier) {
            case InputHandler::SelectionModifier::ADD: selectionModifier = SelectionModifier::ADD; break;
            case InputHandler::SelectionModifier::TOGGLE: selectionModifier = SelectionModifier::TOGGLE; break;
            case InputHandler::SelectionModifier::RANGE: selectionModifier = SelectionModifier::RANGE; break;
            default: selectionModifier = SelectionModifier::NONE; break;
        }
        selectAtPosition(worldPos, selectionModifier);
    };
    
    callbacks.onDragSelectionPreview = [this](sf::Vector2f startPos, sf::Vector2f currentPos) {
        updateDragSelectionPreview(startPos, currentPos);
    };
    
    callbacks.onDragSelection = [this](sf::Vector2f startPos, sf::Vector2f endPos) {
        float left = std::min(startPos.x, endPos.x);
        float top = std::min(startPos.y, endPos.y);
        float width = std::abs(endPos.x - startPos.x);
        float height = std::abs(endPos.y - startPos.y);
        sf::FloatRect selectionArea({left, top}, {width, height});
        
        if (_currentSelectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE) {
            // Handle scroll blocker rectangle placement
            auto borderHexes = calculateRectangleBorderHexes(selectionArea);
            createScrollBlockersFromHexes(borderHexes);
        } else {
            // Normal area selection
            auto result = _selectionManager->selectArea(selectionArea, _currentSelectionMode, _currentElevation);
            if (result.success) {
                spdlog::debug("Area selection completed: {}", result.message);
            }
        }
    };
    
    callbacks.onTilePlacement = [this](sf::Vector2f worldPos) {
        bool isRoof = _inputHandler->isInTilePlacementMode() && 
                     sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift);
        _tilePlacementManager->handleTilePlacement(worldPos, isRoof);
        clearDragSelectionPreview(); // Clear yellow selection tint after placement
    };
    
    callbacks.onTileAreaFill = [this](sf::Vector2f startPos, sf::Vector2f endPos, bool isRoof) {
        _tilePlacementManager->handleTileAreaFill(startPos, endPos, isRoof);
        clearDragSelectionPreview(); // Clear yellow selection tint after area fill
        _selectionManager->clearSelection(); // Clear selection so it doesn't interfere with next tile selection
    };
    
    callbacks.onPan = [this](sf::Vector2f delta) {
        sf::Vector2f center = _viewportController->getView().getCenter();
        _viewportController->getView().setCenter(center + delta);
    };
    
    callbacks.onZoom = [this](float direction) {
        _viewportController->zoomView(direction);
    };
    
    // Object dragging - delegate to DragDropManager
    callbacks.canStartObjectDrag = [this](sf::Vector2f worldPos) {
        return _dragDropManager ? _dragDropManager->canStartObjectDrag(worldPos) : false;
    };
    
    callbacks.onObjectDragStart = [this](sf::Vector2f worldPos) {
        return _dragDropManager ? _dragDropManager->startObjectDrag(worldPos) : false;
    };
    
    callbacks.onObjectDragUpdate = [this](sf::Vector2f worldPos) {
        if (_dragDropManager) {
            _dragDropManager->updateObjectDrag(worldPos);
        }
    };
    
    callbacks.onObjectDragEnd = [this](sf::Vector2f worldPos) {
        if (_dragDropManager) {
            _dragDropManager->finishObjectDrag(worldPos);
        }
    };
    
    callbacks.onObjectDragCancel = [this]() {
        if (_dragDropManager) {
            _dragDropManager->cancelObjectDrag();
        }
    };
    
    // Special modes
    callbacks.onPlayerPositionSelect = [this](sf::Vector2f worldPos) {
        int hexPosition = _viewportController->worldPosToHexIndex(worldPos);
        if (hexPosition >= 0) {
            emit playerPositionSelected(hexPosition);
            spdlog::debug("EditorWidget: Player position selected at hex {}", hexPosition);
        }
        emit statusMessageClearRequested();
    };
    
    callbacks.onScrollBlockerRectangle = [this](sf::FloatRect area) {
        auto borderHexes = calculateRectangleBorderHexes(area);
        createScrollBlockersFromHexes(borderHexes);
    };
    
    callbacks.onTilePlacementCancel = [this]() {
        _tilePlacementManager->resetState();
        if (_mainWindow && _mainWindow->getTilePalettePanel()) {
            _mainWindow->getTilePalettePanel()->deselectTile();
        }
        spdlog::info("Tile placement mode cancelled");
    };
    
    callbacks.onExitGridPlacement = [this](sf::Vector2f worldPos) {
        _exitGridPlacementManager->handleExitGridPlacement(worldPos);
    };
    
    callbacks.onMarkExitsSelection = [this](sf::Vector2f worldPos) {
        _exitGridPlacementManager->handleMarkExitsSelection(worldPos);
    };
    
    callbacks.onMarkExitsAreaSelection = [this](sf::Vector2f startPos, sf::Vector2f endPos) {
        _exitGridPlacementManager->selectExitGridsInArea(startPos, endPos);
    };
    
    callbacks.onMarkExitsPreview = [this](sf::Vector2f startPos, sf::Vector2f currentPos) {
        updateMarkExitsPreview(startPos, currentPos);
    };
    
    // Hover
    callbacks.onMouseMove = [this](sf::Vector2f worldPos) {
        _currentHoverHex = _viewportController->updateHoverHex(worldPos);
        emit hexHoverChanged(_currentHoverHex);
    };
    
    // Keyboard
    callbacks.onEscape = [this]() {
        // Handle escape key logic
        if (_tilePlacementManager->isTilePlacementMode()) {
            _tilePlacementManager->resetState();
            if (_mainWindow && _mainWindow->getTilePalettePanel()) {
                _mainWindow->getTilePalettePanel()->deselectTile();
            }
        }
    };
    
    callbacks.onDeleteObjects = [this]() {
        // Handle delete key to remove selected objects
        deleteSelectedObjects();
    };
    
    callbacks.onMarkExitsModeCancelled = [this]() {
        // Notify MainWindow to deselect the toolbar button
        if (_mainWindow) {
            _mainWindow->deselectMarkExitsMode();
        }
    };
    
    _inputHandler->setCallbacks(callbacks);
    
    // Set initial modes
    _inputHandler->setSelectionMode(_currentSelectionMode);
}

// Helper method to extract scroll blocker creation logic
void EditorWidget::createScrollBlockersFromHexes(const std::vector<int>& borderHexes) {
    if (borderHexes.empty()) {
        spdlog::warn("No valid border hexes found for scroll blocker rectangle");
        return;
    }
    
    int scrollBlockersCreated = 0;
    for (int hexPos : borderHexes) {
        auto scrollBlockerObject = createScrollBlockerObject(hexPos);
        
        // Add to map storage
        _map->getMapFile().map_objects[_currentElevation].push_back(scrollBlockerObject);
        
        // Create visual object for immediate display
        try {
            std::string frmPath = ResourceManager::getInstance().FIDtoFrmName(scrollBlockerObject->frm_pid);
            auto frm = ResourceManager::getInstance().getResource<Frm>(frmPath);
            if (!frm) {
                frm = ResourceManager::getInstance().loadResource<Frm>(frmPath);
            }
            
            if (frm) {
                auto object = std::make_shared<Object>(frm);
                sf::Sprite sprite{ ResourceManager::getInstance().texture(frmPath) };
                object->setSprite(std::move(sprite));
                object->setDirection(static_cast<ObjectDirection>(scrollBlockerObject->direction));
                object->setHexPosition(_hexgrid.grid().at(hexPos));
                object->setMapObject(scrollBlockerObject);
                _objects.push_back(object);
                scrollBlockersCreated++;
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to create visual scroll blocker object at hex {}: {}", hexPos, e.what());
        }
    }
    
    spdlog::info("Scroll blocker rectangle: {} scroll blockers created on border", scrollBlockersCreated);
}

void EditorWidget::update([[maybe_unused]] const float dt) {
    // Update game logic here
    // This is called by the SFMLWidget's update loop
}

void EditorWidget::render(sf::RenderTarget& target, [[maybe_unused]] const float dt) {
    // Render the game here
    // This is called by the SFMLWidget's render loop

    if (!_renderingEngine) {
        return;
    }

    // Prepare visibility settings
    RenderingEngine::VisibilitySettings visibility;
    visibility.showObjects = _showObjects;
    visibility.showCritters = _showCritters;
    visibility.showWalls = _showWalls;
    visibility.showRoof = _showRoof;
    visibility.showScrollBlockers = _showScrollBlk;
    visibility.showWallBlockers = _showWallBlockers;
    visibility.showHexGrid = _showHexGrid;
    visibility.showLightOverlays = _showLightOverlays;
    visibility.showExitGrids = _showExitGrids;
    
    // Prepare render data
    RenderingEngine::RenderData renderData;
    renderData.floorSprites = &_floorSprites;
    renderData.roofSprites = &_roofSprites;
    renderData.objects = &_objects;
    renderData.wallBlockerOverlays = &_wallBlockerOverlays;
    renderData.selectedRoofTileBackgroundSprites = &_selectedRoofTileBackgroundSprites;
    renderData.selectedHexSprites = &_selectedHexSprites;
    renderData.dragPreviewObject = &_dragPreviewObject;
    renderData.isDraggingFromPalette = _isDraggingFromPalette;
    renderData.selectionRectangle = &_selectionRectangle;
    // Use InputHandler state for drag selection rendering
    renderData.isDragSelecting = _inputHandler && _inputHandler->isDragging();
    renderData.currentSelectionMode = _currentSelectionMode;
    renderData.hexGrid = &_hexgrid;
    renderData.hexSprite = &_hexSprite;
    renderData.hexHighlightSprite = &_hexHighlightSprite;
    renderData.playerPositionSprite = &_playerPositionSprite;
    renderData.currentHoverHex = _currentHoverHex;
    renderData.map = _map.get();
    renderData.currentElevation = _currentElevation;
    
    // Delegate rendering to the engine
    _renderingEngine->render(target, _viewportController->getView(), renderData, visibility);
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

    // Update InputHandler with new selection mode
    if (_inputHandler) {
        _inputHandler->setSelectionMode(_currentSelectionMode);
    }

    // Clear current selection when mode changes
    _selectionManager->clearSelection();

    spdlog::info("Selection mode changed to: {}", selectionModeToString(_currentSelectionMode));
}

void EditorWidget::setSelectionMode(SelectionMode mode) {
    if (_currentSelectionMode == mode) {
        return; // No change needed
    }
    
    _currentSelectionMode = mode;
    
    // Update InputHandler with new selection mode
    if (_inputHandler) {
        _inputHandler->setSelectionMode(_currentSelectionMode);
    }

    // Clear current selection when mode changes
    _selectionManager->clearSelection();
    
    spdlog::info("Selection mode set to: {}", selectionModeToString(_currentSelectionMode));
}

void EditorWidget::toggleScrollBlockerRectangleMode() {
    if (_currentSelectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE) {
        // Switch back to ALL mode
        _currentSelectionMode = SelectionMode::ALL;
        spdlog::info("Scroll blocker rectangle mode disabled, switched to ALL mode");
    } else {
        // Switch to scroll blocker rectangle mode
        _currentSelectionMode = SelectionMode::SCROLL_BLOCKER_RECTANGLE;
        // Automatically enable scroll blocker visibility for better UX
        if (!_showScrollBlk) {
            _showScrollBlk = true;
            spdlog::info("Automatically enabled scroll blocker visibility");
        }
        spdlog::info("Scroll blocker rectangle mode enabled");
    }
    
    // Update InputHandler with new selection mode
    if (_inputHandler) {
        _inputHandler->setSelectionMode(_currentSelectionMode);
    }
    
    // Clear current selection when mode changes
    _selectionManager->clearSelection();
}

void EditorWidget::rotateSelectedObject() {
    const auto& selection = _selectionManager->getCurrentSelection();
    auto objects = selection.getObjects();

    if (!objects.empty()) {
        std::ranges::for_each(objects, [](auto& object) {
            object->rotate();
            spdlog::debug("Rotated object to direction {}", object->getMapObject().direction);
        });
        spdlog::info("Rotated {} selected object(s)", objects.size());
    } else {
        spdlog::debug("No objects selected for rotation");
    }
}

void EditorWidget::changeElevation(int elevation) {
    if (elevation >= ELEVATION_1 && elevation <= ELEVATION_3) {
        _currentElevation = elevation;
        loadSprites(); // Reload sprites for new elevation
    }
}

void EditorWidget::placeTileAtPosition(int tileIndex, sf::Vector2f worldPos, bool isRoof) {
    _tilePlacementManager->placeTileAtPosition(tileIndex, worldPos, isRoof);
}

void EditorWidget::fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof) {
    _tilePlacementManager->fillAreaWithTile(tileIndex, area, isRoof);
}

void EditorWidget::replaceSelectedTiles(int newTileIndex) {
    _tilePlacementManager->replaceSelectedTiles(newTileIndex);
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
    _tilePlacementManager->setTilePlacementMode(enabled, tileIndex, isRoof);

    // Update InputHandler with new tile placement state
    if (_inputHandler) {
        _inputHandler->setTilePlacementMode(enabled, tileIndex, false); // replaceMode is always false for now
    }
}

void EditorWidget::setTilePlacementAreaFill(bool enabled) {
    _tilePlacementManager->setTilePlacementAreaFill(enabled);
}

void EditorWidget::setTilePlacementReplaceMode(bool enabled) {
    _tilePlacementManager->setTilePlacementReplaceMode(enabled);
}

void EditorWidget::setExitGridPlacementMode(bool enabled) {
    _exitGridPlacementManager->setExitGridPlacementMode(enabled);
}

void EditorWidget::setMarkExitsMode(bool enabled) {
    _exitGridPlacementManager->setMarkExitsMode(enabled);
    // Update InputHandler with new mark exits mode state
    if (_inputHandler) {
        _inputHandler->setMarkExitsMode(enabled);
    }
}

bool EditorWidget::isTilePlacementMode() const {
    return _tilePlacementManager->isTilePlacementMode();
}

void EditorWidget::refreshObjects() {
    // Clear existing objects
    _objects.clear();
    
    // Reload objects from the current map
    loadObjectSprites();
    
    spdlog::debug("Refreshed objects for current elevation");
}

void EditorWidget::updateTileSprite(int hexIndex, bool isRoof) {
    if (!_map || !isValidHexPosition(hexIndex)) {
        return;
    }

    // Convert hex coordinates (200x200 grid) to tile coordinates (100x100 grid)
    int hexX = hexIndex % HexagonGrid::GRID_WIDTH;  // 0-199
    int hexY = hexIndex / HexagonGrid::GRID_WIDTH;  // 0-199
    int tileX = hexX / 2;  // 0-99
    int tileY = hexY / 2;  // 0-99
    int tileIndex = tileY * MAP_WIDTH + tileX; // Convert to tile index
    
    // Get tiles for current elevation
    const auto& elevationTiles = _map->getMapFile().tiles[_currentElevation];
    if (tileIndex >= static_cast<int>(elevationTiles.size())) {
        spdlog::warn("EditorWidget::updateTileSprite: Tile index {} out of bounds (hex {})", tileIndex, hexIndex);
        return;
    }

    // Get the tile data using the converted tile index
    const auto& tile = elevationTiles[tileIndex];
    int tileID = isRoof ? tile.getRoof() : tile.getFloor();

    // Handle empty tiles by setting transparent color
    if (tileID == Map::EMPTY_TILE) {
        auto& sprites = isRoof ? _roofSprites : _floorSprites;
        sprites[tileIndex].setColor(isRoof ? geck::TileColors::transparent() : sf::Color::White);
        spdlog::debug("EditorWidget::updateTileSprite: Set tile {} to empty [roof: {}]", tileIndex, isRoof);
        return;
    }

    // Get the sprite array to update
    auto& sprites = isRoof ? _roofSprites : _floorSprites;

    // Load the texture for this tile
    try {
        auto& resourceManager = ResourceManager::getInstance();
        const auto* tileList = resourceManager.getResource<Lst, std::string>("art/tiles/tiles.lst");
        if (!tileList || tileID >= static_cast<int>(tileList->list().size())) {
            return;
        }

        const std::string tileName = tileList->list()[tileID];
        std::string tilePath = "art/tiles/" + tileName;
        const auto& texture = resourceManager.texture(tilePath);

        // Update the sprite using tile index for array access
        sprites[tileIndex].setTexture(texture);
        // Reset sprite color to ensure visibility (in case it was previously transparent)
        sprites[tileIndex].setColor(sf::Color::White);

        // Calculate position using tile index for screen positioning (same as initial loading)
        auto screenPos = indexToScreenPosition(tileIndex, isRoof);
        sprites[tileIndex].setPosition({ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) });

        spdlog::debug("EditorWidget::updateTileSprite: Updated sprite for hex {} -> tile {} ({}) [roof: {}]", 
                      hexIndex, tileIndex, tileName, isRoof);
    } catch (const std::exception& e) {
        spdlog::warn("EditorWidget::updateTileSprite: Failed to update tile sprite: {}", e.what());
    }
}

// Helper methods implementation


// Tile selection helper implementation

bool EditorWidget::isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite) {
    // Simple bounding box collision check - same as original EditorState
    return sprite.getGlobalBounds().contains(worldPos);
}

// Methods for SelectionManager to access tile and object data

std::optional<int> EditorWidget::getTileAtPosition(sf::Vector2f worldPos, bool isRoof) {
    // Check if current elevation exists in the map data
    if (!_map || _map->getMapFile().tiles.find(_currentElevation) == _map->getMapFile().tiles.end()) {
        return std::nullopt;
    }

    // FIXME: this is inaccurate and we should not use hex-to-tile conversion in the future
    // Use hex-based selection for consistency with hex highlighting
    // This ensures tile selection matches what users see in the hex highlights
    
    // Adjust world position for roof offset if selecting roof tiles
    sf::Vector2f adjustedWorldPos = worldPos;
    if (isRoof) {
        adjustedWorldPos.y += ROOF_OFFSET;  // Roof tiles are visually offset upward
    }
    
    int hexIndex = _viewportController->worldPosToHexIndex(adjustedWorldPos);
    if (hexIndex < 0) {
        spdlog::debug("EditorWidget::getTileAtPosition: No hex found at worldPos ({:.1f}, {:.1f}) adjusted({:.1f}, {:.1f}) [roof: {}]",
                      worldPos.x, worldPos.y, adjustedWorldPos.x, adjustedWorldPos.y, isRoof);
        return std::nullopt;
    }
    
    // Convert hex coordinates (200x200 grid) to tile coordinates (100x100 grid)
    int hexX = hexIndex % HexagonGrid::GRID_WIDTH;  // 0-199
    int hexY = hexIndex / HexagonGrid::GRID_WIDTH;  // 0-199
    int tileX = hexX / 2;  // 0-99
    int tileY = hexY / 2;  // 0-99
    int tileIndex = tileY * MAP_WIDTH + tileX; // Convert to tile index
    
    // Validate tile bounds (should be redundant but kept for safety)
    if (tileIndex >= TILES_PER_ELEVATION) {
        spdlog::debug("EditorWidget::getTileAtPosition: Tile index {} out of bounds", tileIndex);
        return std::nullopt;
    }
    
    // For roof tiles, check if there's actually a roof tile at this position
    if (isRoof && _map->getMapFile().tiles.at(_currentElevation).at(tileIndex).getRoof() == Map::EMPTY_TILE) {
        spdlog::debug("EditorWidget::getTileAtPosition: Empty roof tile at index {} [worldPos: ({:.1f}, {:.1f})]",
                      tileIndex, worldPos.x, worldPos.y);
        return std::nullopt;
    }
    
    spdlog::debug("EditorWidget::getTileAtPosition: Found tile {} at worldPos ({:.1f}, {:.1f}) [roof: {}]",
                  tileIndex, worldPos.x, worldPos.y, isRoof);
    
    return tileIndex;
}

std::optional<int> EditorWidget::getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos) {
    // This version includes empty roof tiles in the selection using F2 Mapper algorithm
    if (!_map || _map->getMapFile().tiles.find(_currentElevation) == _map->getMapFile().tiles.end()) {
        return std::nullopt;
    }
    
    // Use hex-based selection for roof tiles too, for consistency
    // Adjust world position for roof offset since we're selecting roof tiles
    sf::Vector2f adjustedWorldPos = worldPos;
    adjustedWorldPos.y += ROOF_OFFSET;  // Roof tiles are visually offset upward
    
    int hexIndex = _viewportController->worldPosToHexIndex(adjustedWorldPos);
    if (hexIndex < 0) {
        return std::nullopt;
    }
    
    // Convert hex coordinates to tile coordinates
    int hexX = hexIndex % HexagonGrid::GRID_WIDTH;  // 0-199
    int hexY = hexIndex / HexagonGrid::GRID_WIDTH;  // 0-199
    int tileX = hexX / 2;  // 0-99
    int tileY = hexY / 2;  // 0-99
    int tileIndex = tileY * MAP_WIDTH + tileX; // Convert to tile index
    
    if (tileIndex < 0 || tileIndex >= TILES_PER_ELEVATION) {
        return std::nullopt;
    }
    
    return tileIndex;
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

void EditorWidget::updateDragSelectionPreview(sf::Vector2f startWorldPos, sf::Vector2f currentWorldPos) {
    // Clear previous preview
    clearDragPreview();

    // Create selection area
    float left = std::min(startWorldPos.x, currentWorldPos.x);
    float top = std::min(startWorldPos.y, currentWorldPos.y);
    float width = std::abs(currentWorldPos.x - startWorldPos.x);
    float height = std::abs(currentWorldPos.y - startWorldPos.y);
    sf::FloatRect selectionArea({ left, top }, { width, height });

    // Update the visual selection rectangle
    _selectionRectangle.setPosition({ left, top });
    _selectionRectangle.setSize({ width, height });

    // Get items that would be selected
    switch (_currentSelectionMode) {
        case SelectionMode::FLOOR_TILES: {
            _previewTiles = _selectionManager->getTilesInArea(selectionArea, false, _currentElevation);
            // Apply preview coloring to floor tiles
            for (int tileIndex : _previewTiles) {
                if (isValidTileIndex(tileIndex)) {
                    applyPreviewHighlight(_floorSprites.at(tileIndex));
                }
            }
            break;
        }

        case SelectionMode::ROOF_TILES: {
            _previewTiles = _selectionManager->getTilesInArea(selectionArea, true, _currentElevation);
            // Apply preview coloring to roof tiles
            for (int tileIndex : _previewTiles) {
                if (isValidTileIndex(tileIndex)) {
                    applyPreviewHighlight(_roofSprites.at(tileIndex));
                }
            }
            break;
        }

        case SelectionMode::ROOF_TILES_ALL: {
            _previewTiles = _selectionManager->getTilesInAreaIncludingEmpty(selectionArea, true, _currentElevation);
            // Apply preview coloring to roof tiles including empty ones
            for (int tileIndex : _previewTiles) {
                if (isValidTileIndex(tileIndex)) {
                    // Apply preview coloring to roof sprite (makes empty tiles visible if they were transparent)
                    applyPreviewHighlight(_roofSprites.at(tileIndex));
                }
            }
            break;
        }

        case SelectionMode::OBJECTS: {
            _previewObjects = _selectionManager->getObjectsInArea(selectionArea, _currentElevation);
            // Apply preview coloring to objects
            std::ranges::for_each(_previewObjects, [](auto& object) {
                if (object) {
                    applyPreviewHighlight(object->getSprite());
                }
            });
            break;
        }

        case SelectionMode::ALL: {
            // Preview all types of items in ALL mode

            // Preview floor tiles
            _previewTiles = _selectionManager->getTilesInArea(selectionArea, false, _currentElevation);
            for (int tileIndex : _previewTiles) {
                if (isValidTileIndex(tileIndex)) {
                    applyPreviewHighlight(_floorSprites.at(tileIndex));
                }
            }

            // Preview roof tiles
            auto roofTiles = _selectionManager->getTilesInArea(selectionArea, true, _currentElevation);
            for (int tileIndex : roofTiles) {
                if (isValidTileIndex(tileIndex)) {
                    applyPreviewHighlight(_roofSprites.at(tileIndex));
                }
            }
            _previewTiles.insert(_previewTiles.end(), roofTiles.begin(), roofTiles.end());

            // Preview objects
            _previewObjects = _selectionManager->getObjectsInArea(selectionArea, _currentElevation);
            std::ranges::for_each(_previewObjects, [](auto& object) {
                if (object) {
                    applyPreviewHighlight(object->getSprite());
                }
            });
            break;
        }

        default:
            break;
    }
}

void EditorWidget::updateTileAreaFillPreview(sf::Vector2f startWorldPos, sf::Vector2f currentWorldPos) {
    // Clear previous preview
    clearDragPreview();

    // Create selection area
    float left = std::min(startWorldPos.x, currentWorldPos.x);
    float top = std::min(startWorldPos.y, currentWorldPos.y);
    float width = std::abs(currentWorldPos.x - startWorldPos.x);
    float height = std::abs(currentWorldPos.y - startWorldPos.y);
    sf::FloatRect selectionArea({ left, top }, { width, height });

    // Get tiles that would be affected by the area fill (default to floor tiles)
    bool isRoof = _tilePlacementManager->getTilePlacementIsRoof();
    _previewTiles = _selectionManager->getTilesInArea(selectionArea, isRoof, _currentElevation);

    // Apply preview coloring to tiles (same as selection mode)
    auto& sprites = isRoof ? _roofSprites : _floorSprites;
    for (int tileIndex : _previewTiles) {
        if (isValidTileIndex(tileIndex)) {
            applyPreviewHighlight(sprites.at(tileIndex));
        }
    }
}

void EditorWidget::clearDragPreview() {
    // Clear selection rectangle
    _selectionRectangle.setSize({ 0, 0 });
    _selectionRectangle.setPosition({ 0, 0 });
    
    // Clear tile preview coloring
    for (int tileIndex : _previewTiles) {
        if (isValidTileIndex(tileIndex)) {
            removePreviewHighlight(_floorSprites.at(tileIndex));

            // For roof sprites, check if it's empty and set back to transparent
            if (_map && _map->getMapFile().tiles.find(_currentElevation) != _map->getMapFile().tiles.end()) {
                auto tile = _map->getMapFile().tiles.at(_currentElevation).at(tileIndex);
                if (tile.getRoof() == Map::EMPTY_TILE) {
                    _roofSprites.at(tileIndex).setColor(geck::TileColors::transparent()); // Transparent
                } else {
                    removePreviewHighlight(_roofSprites.at(tileIndex));
                }
            } else {
                // If elevation doesn't exist, just remove preview highlight
                removePreviewHighlight(_roofSprites.at(tileIndex));
            }
        }
    }

    // Clear object preview coloring
    std::ranges::for_each(_previewObjects, [](auto& object) {
        if (object) {
            removePreviewHighlight(object->getSprite());
        }
    });

    // Clear the preview arrays
    _previewTiles.clear();
    _previewObjects.clear();
}

void EditorWidget::updateMarkExitsPreview(sf::Vector2f startWorldPos, sf::Vector2f currentWorldPos) {
    // Clear previous preview
    clearDragPreview();
    
    // Create selection area
    float left = std::min(startWorldPos.x, currentWorldPos.x);
    float top = std::min(startWorldPos.y, currentWorldPos.y);
    float width = std::abs(currentWorldPos.x - startWorldPos.x);
    float height = std::abs(currentWorldPos.y - startWorldPos.y);
    sf::FloatRect selectionArea({ left, top }, { width, height });
    
    // Update the visual selection rectangle
    _selectionRectangle.setPosition({ left, top });
    _selectionRectangle.setSize({ width, height });
    
    // Find and highlight only exit grid objects
    for (auto& object : _objects) {
        if (!object || !object->getMapObjectPtr() || !object->getMapObjectPtr()->isExitGridMarker()) {
            continue;
        }
        
        // Get object sprite bounds
        const auto& sprite = object->getSprite();
        auto objectBounds = sprite.getGlobalBounds();
        
        // Check if object intersects with selection area
        auto intersection = selectionArea.findIntersection(objectBounds);
        if (intersection.has_value()) {
            _previewObjects.push_back(object);
            // Use bright magenta highlight for better contrast against green exit grids
            object->getSprite().setColor(geck::TileColors::exitGridHighlight());
        }
    }
}

const sf::Texture& EditorWidget::createHexTexture() {
    [[maybe_unused]] auto* hexFrm =
        ResourceManager::getInstance().loadResource<Frm>(ResourcePaths::Frm::HEX_GRID);
    return ResourceManager::getInstance().texture(ResourcePaths::Frm::HEX_GRID);
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
    int hexPosition = _viewportController->worldPosToHexIndex(worldPos);
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
        
        // Create wall blocker overlay if the object blocks movement
        createWallBlockerOverlay(mapObject, hexPosition);
        
        spdlog::info("EditorWidget: Successfully placed object at hex {} (pro_pid: {})", 
                    hexPosition, mapObject->pro_pid);
            
    } catch (const std::exception& e) {
        spdlog::warn("EditorWidget: Failed to create visual object for placed item: {}", e.what());
        // The MapObject is still saved, just won't be visible until reload
    }
    
}

void EditorWidget::startDragPreview(int objectIndex, int categoryInt, sf::Vector2f worldPos) {
    // Get object info from palette
    if (_mainWindow) {
        auto palette = _mainWindow->getObjectPalettePanel();
        if (palette) {
            _previewObjectInfo = palette->getObjectInfo(objectIndex, static_cast<ObjectCategory>(categoryInt));
        }
    }
    
    if (_dragDropManager) {
        _dragDropManager->startDragPreview(objectIndex, categoryInt, worldPos);
        // Sync the preview object with DragDropManager for rendering
        _dragPreviewObject = _dragDropManager->getDragPreviewObject();
        _isDraggingFromPalette = (_dragPreviewObject != nullptr);
    }
}

void EditorWidget::updateDragPreview(sf::Vector2f worldPos) {
    if (_dragDropManager) {
        _dragDropManager->updateDragPreview(worldPos);
        // Keep the preview object in sync for rendering
        if (_isDraggingFromPalette) {
            _dragPreviewObject = _dragDropManager->getDragPreviewObject();
        }
    }
}

void EditorWidget::finishDragPreview(sf::Vector2f worldPos) {
    if (_dragDropManager) {
        _dragDropManager->finishDragPreview(worldPos);
    }
    _previewObjectInfo = nullptr;
    _dragPreviewObject.reset();
    _isDraggingFromPalette = false;
}

void EditorWidget::cancelDragPreview() {
    _previewObjectInfo = nullptr;
    _dragPreviewObject.reset();
    _isDraggingFromPalette = false;
    if (_dragDropManager) {
        _dragDropManager->cancelDragPreview();
    }
}

void EditorWidget::enterPlayerPositionSelectionMode() {
    // Exit any current modes
    _tilePlacementManager->resetState();
    
    // Enter player position selection mode
    _playerPositionSelectionMode = true;
    
    // Update InputHandler
    if (_inputHandler) {
        _inputHandler->setPlayerPositionMode(true);
        _inputHandler->setTilePlacementMode(false);
    }
    
    // Show status message
    emit statusMessageRequested("Click on a hex to set the player starting position (Press Escape to cancel)");
    
    spdlog::debug("EditorWidget: Entered player position selection mode");
}

void EditorWidget::centerViewOnPlayerPosition() {
    if (!_map) {
        spdlog::warn("EditorWidget::centerViewOnPlayerPosition: No map loaded");
        return;
    }
    
    // Get player default position from map header
    uint32_t playerHexPosition = _map->getMapFile().header.player_default_position;
    
    // Get the hex at the player position
    auto hex = _hexgrid.getHexByPosition(playerHexPosition);
    if (!hex) {
        spdlog::warn("EditorWidget::centerViewOnPlayerPosition: Invalid player hex position {}", playerHexPosition);
        return;
    }
    
    // Get screen coordinates from the hex
    float screenX = static_cast<float>(hex->get().x());
    float screenY = static_cast<float>(hex->get().y());
    
    // Set view center to the hex position
    _viewportController->getView().setCenter(sf::Vector2f(screenX, screenY));
    
    spdlog::debug("EditorWidget::centerViewOnPlayerPosition: Centered view on player position {} at screen ({}, {})", 
                  playerHexPosition, screenX, screenY);
}

void EditorWidget::showLoadingErrorsSummary() {
    if (!_lastLoadErrors.hasErrors()) {
        return; // No errors to show
    }
    
    QString title = "Map Loading Warnings";
    QString message;
    
    // Build the error summary message
    message += QString("Some objects could not be loaded:\n\n");
    message += QString("• %1 objects skipped due to missing or invalid FRM files\n")
               .arg(_lastLoadErrors.objectsSkipped);
    
    if (!_lastLoadErrors.failedFrmNames.empty()) {
        message += QString("• %1 unique FRM files failed to load:\n\n")
                   .arg(_lastLoadErrors.failedFrmNames.size());
        
        // Show up to 10 FRM files to avoid overwhelming the user
        int count = 0;
        const int maxShow = 10;
        for (const auto& frmName : _lastLoadErrors.failedFrmNames) {
            if (count >= maxShow) {
                message += QString("  ... and %1 more\n")
                          .arg(_lastLoadErrors.failedFrmNames.size() - maxShow);
                break;
            }
            message += QString("  - %1\n").arg(QString::fromStdString(frmName));
            count++;
        }
    }
    
    message += "\nPossible causes:\n";
    message += "• Missing or corrupted game data files (master.dat, critter.dat)\n";
    message += "• Incomplete Fallout 2 installation\n";
    message += "• Custom map using non-standard resources\n\n";
    message += "The map will continue to work with the objects that loaded successfully.";
    
    QtDialogs::showWarning(this, title, message);
}

void EditorWidget::setShowLightOverlays(bool show) {
    _showLightOverlays = show;
    
    int lightObjectCount = 0;
    // Update all objects to show/hide their light overlays
    std::ranges::for_each(_objects, [&lightObjectCount, show](auto& object) {
        if (object->hasLight()) {
            lightObjectCount++;
            spdlog::debug("EditorWidget: Found light source object with light_radius={}, light_intensity={}", 
                         object->getMapObject().light_radius, object->getMapObject().light_intensity);
        }
        object->setShowLightOverlay(show);
    });
    
    spdlog::info("Light overlay display set to: {} (found {} light objects)", show, lightObjectCount);
}

void EditorWidget::clearDragSelectionPreview() {
    // Clear all drag preview highlighting and selection rectangle
    clearDragPreview();
    
    spdlog::debug("EditorWidget::clearDragSelectionPreview() - cleared selection rectangle");
}

void EditorWidget::onObjectFrmChanged(std::shared_ptr<Object> object, uint32_t newFrmPid) {
    if (!object) {
        spdlog::warn("EditorWidget::onObjectFrmChanged - null object provided");
        return;
    }

    try {
        // Get the new FRM path from the FRM PID
        auto& resourceManager = ResourceManager::getInstance();
        std::string newFrmPath = resourceManager.FIDtoFrmName(newFrmPid);
        
        if (newFrmPath.empty()) {
            spdlog::error("EditorWidget::onObjectFrmChanged - could not resolve FRM path for PID {}", newFrmPid);
            return;
        }

        // Load the new FRM texture
        const auto& newTexture = resourceManager.texture(newFrmPath);
        
        // Create new sprite with the updated texture
        sf::Sprite newSprite(newTexture);
        object->setSprite(std::move(newSprite));
        
        // Set the direction to update texture rectangle (use current direction)
        auto& mapObject = object->getMapObject();
        object->setDirection(static_cast<ObjectDirection>(mapObject.direction));
        
        spdlog::info("EditorWidget::onObjectFrmChanged - updated object visual to FRM PID {} ({})", 
                    newFrmPid, newFrmPath);
        
    } catch (const std::exception& e) {
        spdlog::error("EditorWidget::onObjectFrmChanged - failed to update object FRM: {}", e.what());
    }
}

void EditorWidget::onObjectFrmPathChanged(std::shared_ptr<Object> object, const std::string& newFrmPath) {
    if (!object) {
        spdlog::warn("EditorWidget::onObjectFrmPathChanged - null object provided");
        return;
    }
    
    if (newFrmPath.empty()) {
        spdlog::warn("EditorWidget::onObjectFrmPathChanged - empty FRM path provided");
        return;
    }

    try {
        auto& resourceManager = ResourceManager::getInstance();
        
        // Load the new FRM data (not just texture)
        auto newFrm = resourceManager.loadResource<Frm>(newFrmPath);
        if (!newFrm) {
            spdlog::error("EditorWidget::onObjectFrmPathChanged - failed to load FRM data for: {}", newFrmPath);
            return;
        }
        
        // Load the new FRM texture 
        const auto& newTexture = resourceManager.texture(newFrmPath);
        
        // Debug: Check object state before FRM change
        sf::Vector2f oldPosition = object->getSprite().getPosition();
        const auto& oldTexture = object->getSprite().getTexture();
        spdlog::info("EditorWidget::onObjectFrmPathChanged - CHANGING FRM FROM existing to: {}", newFrmPath);
        spdlog::debug("EditorWidget::onObjectFrmPathChanged - BEFORE: object position: ({}, {})", 
                     oldPosition.x, oldPosition.y);
        spdlog::debug("EditorWidget::onObjectFrmPathChanged - OLD texture ptr: {}, NEW texture ptr: {}", 
                     static_cast<const void*>(&oldTexture), static_cast<const void*>(&newTexture));
        spdlog::debug("EditorWidget::onObjectFrmPathChanged - OLD texture size: {}x{}, NEW texture size: {}x{}", 
                     oldTexture.getSize().x, oldTexture.getSize().y, newTexture.getSize().x, newTexture.getSize().y);
                     
        // Additional check: Are the textures actually different?
        bool texturesAreDifferent = (&oldTexture != &newTexture);
        spdlog::info("EditorWidget::onObjectFrmPathChanged - Textures are different: {}", texturesAreDifferent);
        
        // Create new sprite with the updated texture
        sf::Sprite newSprite(newTexture);
        // IMPORTANT: Preserve the original position
        newSprite.setPosition(oldPosition);
        object->setSprite(std::move(newSprite));
        
        // Update the FRM data in the object (this is crucial!)
        object->setFrm(newFrm);
        
        // Validate that the object still has its MapObject association
        if (!object->hasMapObject()) {
            spdlog::error("EditorWidget::onObjectFrmPathChanged - Object lost MapObject association during FRM change!");
            return;
        }
        
        // Set the direction to update texture rectangle (use current direction)
        auto& mapObject = object->getMapObject();
        object->setDirection(static_cast<ObjectDirection>(mapObject.direction));
        
        // Note: MapObject's frm_pid is updated by SelectionPanel for persistence
        
        // Final validation: Ensure the object is still properly configured
        const auto& currentSpriteTexture = object->getSprite().getTexture();
        if (currentSpriteTexture.getSize().x == 0 || currentSpriteTexture.getSize().y == 0) {
            spdlog::error("EditorWidget::onObjectFrmPathChanged - Object sprite has invalid texture after FRM change!");
            return;
        }
        
        // Debug: Check object state after FRM change
        sf::Vector2f position = object->getSprite().getPosition();
        sf::IntRect textureRect = object->getSprite().getTextureRect();
        const auto& currentTexture = object->getSprite().getTexture();
        spdlog::debug("EditorWidget::onObjectFrmPathChanged - AFTER: object position: ({}, {}), textureRect: ({}, {}, {}, {})", 
                     position.x, position.y, textureRect.position.x, textureRect.position.y, textureRect.size.x, textureRect.size.y);
        spdlog::debug("EditorWidget::onObjectFrmPathChanged - AFTER: current texture ptr: {}, size: {}x{}", 
                     static_cast<const void*>(&currentTexture), currentTexture.getSize().x, currentTexture.getSize().y);
        
        spdlog::info("EditorWidget::onObjectFrmPathChanged - updated object visual to FRM path: {}", newFrmPath);
        
    } catch (const std::exception& e) {
        spdlog::error("EditorWidget::onObjectFrmPathChanged - failed to update object FRM from path {}: {}", 
                     newFrmPath, e.what());
    }
}

void EditorWidget::deleteSelectedObjects() {
    if (!_map) {
        return;
    }
    
    auto& selectionState = _selectionManager->getCurrentSelection();
    const auto& selectedObjects = selectionState.getObjects();
    
    if (selectedObjects.empty()) {
        spdlog::debug("EditorWidget::deleteSelectedObjects - No objects selected");
        return;
    }
    
    spdlog::info("EditorWidget::deleteSelectedObjects - Deleting {} selected objects", selectedObjects.size());
    
    // Remove objects from map and clear from visual objects
    int deletedCount = 0;
    for (const auto& object : selectedObjects) {
        if (!object) continue;
        
        try {
            // Remove from map objects
            uint32_t elevation = object->getMapObject().elevation;
            // Access mapFile directly to modify objects
            auto& mapFile = _map->getMapFile();
            auto& elevationObjects = mapFile.map_objects[elevation];
            
            // Find and remove the object from the map
            auto it = std::find_if(elevationObjects.begin(), elevationObjects.end(),
                [&object](const std::shared_ptr<MapObject>& mapObj) {
                    return mapObj.get() == &object->getMapObject();
                });
                
            if (it != elevationObjects.end()) {
                elevationObjects.erase(it);
                deletedCount++;
                spdlog::debug("EditorWidget::deleteSelectedObjects - Removed object at elevation {}", elevation);
            }
            
            // Remove from visual objects list
            auto visualIt = std::find(_objects.begin(), _objects.end(), object);
            if (visualIt != _objects.end()) {
                _objects.erase(visualIt);
                spdlog::debug("EditorWidget::deleteSelectedObjects - Removed from visual objects");
            }
            
        } catch (const std::exception& e) {
            spdlog::error("EditorWidget::deleteSelectedObjects - Error deleting object: {}", e.what());
        }
    }
    
    // Clear selection
    _selectionManager->clearSelection();
    
    // Update UI
    emit selectionChanged(_selectionManager->getCurrentSelection(), _currentElevation);
    
    // Publish to EventBus - objects were removed
    EventBus::getInstance().publish(SelectionChangedEvent{
        SelectionChangedEvent::Type::Removed,
        0  // After deletion, selection is cleared
    });
    
    spdlog::info("EditorWidget::deleteSelectedObjects - Successfully deleted {} objects", deletedCount);
}

} // namespace geck
