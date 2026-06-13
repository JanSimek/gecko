#include "EditorWidget.h"
#include "ui/widgets/SFMLWidget.h"
#include "ui/input/InputHandler.h"
#include "ui/rendering/MapSpriteLoader.h"
#include "ui/rendering/ObjectVisibility.h"
#include "ui/rendering/RenderingEngine.h"
#include "ui/dragdrop/DragDropManager.h"
#include "ui/tiles/TilePlacementManager.h"
#include "ui/tools/ExitGridPlacementManager.h"
#include "ui/viewport/ViewportController.h"
#include "ui/panels/ObjectPalettePanel.h"
#include "ui/panels/TilePalettePanel.h"
#include "MainWindow.h"

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstdlib>
#include <set>
#include <unordered_map>

#include "util/Constants.h"
#include "util/ColorUtils.h"
#include "ui/ResourceInitializer.h"
#include "util/TileUtils.h"
#include "ui/QtDialogs.h"
#include "util/ProHelper.h"
#include "util/Coordinates.h"

#include "editor/Object.h"
#include "pattern/PatternStamper.h"
#include "pattern/PatternSprite.h"
#include "editor/HexagonGrid.h"

#include "format/frm/Frm.h"
#include "format/map/Tile.h"
#include "format/pro/Pro.h"
#include "format/map/MapObject.h"
#include "resource/GameResources.h"

#include "writer/map/MapWriter.h"

namespace geck {

EditorWidget::EditorWidget(resource::GameResources& resources, std::unique_ptr<Map> map, QWidget* parent)
    : QWidget(parent)
    , _layout(nullptr)
    , _sfmlWidget(nullptr)
    , _mainWindow(nullptr)
    , _resources(resources)
    , _map(std::move(map)) {
    _floorSprites.reserve(Map::TILES_PER_ELEVATION);
    _roofSprites.reserve(Map::TILES_PER_ELEVATION);

    if (_map) {
        initializeSelectionSystem();
    }

    _renderingEngine = std::make_unique<RenderingEngine>(_resources);
    _mapSpriteLoader = std::make_unique<MapSpriteLoader>(_resources, _hexgrid);
    _objectCommandController = std::make_unique<ObjectCommandController>(
        _resources,
        _map,
        _hexgrid,
        *_mapSpriteLoader,
        _objects,
        _wallBlockerOverlays,
        _undoStack,
        [this]() { refreshObjects(); },
        [this]() { Q_EMIT undoStackChanged(); },
        [this](int elevation) -> std::vector<Tile>& { return ensureElevationTiles(elevation); },
        [this]() { return _currentElevation; },
        [this](int hexIndex, bool isRoof, int elevation) { updateTileSprite(hexIndex, isRoof, elevation); },
        [this]() { loadTileSprites(); });
    _inputHandler = std::make_unique<InputHandler>();
    _dragDropManager = std::make_unique<DragDropManager>(
        *this,
        [this](int idx, ObjectCategory cat) -> const ObjectInfo* {
            auto* mw = getMainWindow();
            auto* p = mw ? mw->getObjectPalettePanel() : nullptr;
            return p ? p->getObjectInfo(idx, cat) : nullptr;
        });
    _tilePlacementManager = std::make_unique<TilePlacementManager>(*this);
    _exitGridPlacementManager = std::make_unique<ExitGridPlacementManager>(
        *this,
        [this](const QString& m) {
            if (auto* mw = getMainWindow())
                mw->showStatusMessage(m);
        });
    _viewportController = std::make_unique<ViewportController>(&_hexgrid);
    setupInputCallbacks();

    setupUI();
    _viewportController->centerViewOnMap();
}

// Tile-edit command logic lives in ObjectCommandController (the single command
// owner); these are thin TilePlacementContext delegators. The controller invokes
// ensureElevationTiles/updateTileSprite/getCurrentElevation back through callbacks.
void EditorWidget::applyTileChanges(const std::vector<TileChange>& changes, bool applyAfterState) {
    _objectCommandController->applyTileChanges(changes, applyAfterState);
}

void EditorWidget::registerTileEdit(const QString& description, const std::vector<TileChange>& changes) {
    _objectCommandController->registerTileEdit(description.toStdString(), changes);
}

void EditorWidget::addPlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    _objectCommandController->addPlacedObject(mapObject, object);
}

void EditorWidget::removePlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    _objectCommandController->removePlacedObject(mapObject, object);
}

// The register*() helpers below emit undoStackChanged through the controller's
// onStackChanged callback (wired in the constructor), so they no longer emit here.
void EditorWidget::registerObjectPlacement(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    _objectCommandController->registerObjectPlacement(mapObject, object);
}

void EditorWidget::registerObjectMove(const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<std::pair<int, int>>& moves) {
    _objectCommandController->registerObjectMove(objects, moves);
}

void EditorWidget::registerObjectRotation(const std::vector<std::shared_ptr<Object>>& objects, const std::vector<int>& beforeDirs, const std::vector<int>& afterDirs) {
    _objectCommandController->registerObjectRotation(objects, beforeDirs, afterDirs);
}

void EditorWidget::applyFrmToObject(const std::shared_ptr<Object>& object, uint32_t frmPid, const std::string& frmPath) {
    _objectCommandController->applyFrmToObject(object, frmPid, frmPath);
}

void EditorWidget::registerObjectFrmChange(const std::shared_ptr<Object>& object, uint32_t oldFrmPid, const std::string& oldFrmPath, uint32_t newFrmPid, const std::string& newFrmPath) {
    _objectCommandController->registerObjectFrmChange(object, oldFrmPid, oldFrmPath, newFrmPid, newFrmPath);
}

void EditorWidget::registerExitGridCreation(const std::vector<std::shared_ptr<MapObject>>& exitGrids, int elevation) {
    _objectCommandController->registerExitGridCreation(exitGrids, elevation);
}

void EditorWidget::registerExitGridEdit(const std::vector<std::shared_ptr<MapObject>>& exitGrids,
    const std::vector<ExitGridState>& beforeStates,
    const std::vector<ExitGridState>& afterStates) {
    _objectCommandController->registerExitGridEdit(exitGrids, beforeStates, afterStates);
}

void EditorWidget::registerInstanceEdit(const std::shared_ptr<MapObject>& mapObject,
    const MapObjectInstanceState& before,
    const MapObjectInstanceState& after,
    const std::string& description) {
    _objectCommandController->registerInstanceEdit(mapObject, before, after, description);
}

void EditorWidget::clearElevationObjects(int elevation) {
    _objectCommandController->clearElevationObjects(elevation);
}

void EditorWidget::copyElevation(int fromElevation, int toElevation) {
    _objectCommandController->copyElevation(fromElevation, toElevation);
}

void EditorWidget::registerInventoryEdit(const std::shared_ptr<MapObject>& container,
    std::vector<std::shared_ptr<MapObject>> before,
    std::vector<std::shared_ptr<MapObject>> after) {
    _objectCommandController->registerInventoryEdit(container, std::move(before), std::move(after));
}

void EditorWidget::attachScript(const std::shared_ptr<MapObject>& object, int scriptType, uint32_t programIndex) {
    _objectCommandController->attachScript(object, scriptType, programIndex);
}

void EditorWidget::detachScript(const std::shared_ptr<MapObject>& object) {
    _objectCommandController->detachScript(object);
}

void EditorWidget::addSpatialScript(uint32_t programIndex, int tile, int elevation, int radius) {
    _objectCommandController->addSpatialScript(programIndex, tile, elevation, radius);
}

EditorWidget::~EditorWidget() {
}

bool EditorWidget::undoLastEdit() {
    bool result = _undoStack.undo();
    Q_EMIT undoStackChanged();
    return result;
}

bool EditorWidget::redoLastEdit() {
    bool result = _undoStack.redo();
    Q_EMIT undoStackChanged();
    return result;
}

void EditorWidget::initializeSelectionSystem() {
    _selectionManager = std::make_unique<selection::SelectionManager>(*this);

    _selectionManager->setSelectionCallback([this](const selection::SelectionState& selection) {
        this->clearAllVisualSelections();
        this->applySelectionVisuals(selection);
        Q_EMIT selectionChanged(selection, _currentElevation);
    });
}

void EditorWidget::applySelectionVisuals(const selection::SelectionState& selection) {
    for (const auto& item : selection.items) {
        switch (item.type) {
            case selection::SelectionType::OBJECT: {
                auto object = item.getObject();
                if (object) {
                    object->select();
                }
                break;
            }

            case selection::SelectionType::ROOF_TILE:
                applyRoofTileSelectionVisual(item.getTileIndex());
                break;

            case selection::SelectionType::FLOOR_TILE: {
                int tileIndex = item.getTileIndex();
                if (isValidTileIndex(tileIndex)) {
                    this->_floorSprites.at(tileIndex).setColor(geck::ColorUtils::createErrorIndicatorColor());
                }
                break;
            }

            case selection::SelectionType::HEX: {
                int hexIndex = item.getHexIndex();
                if (hexIndex >= 0 && hexIndex < static_cast<int>(_hexgrid.size())) {
                    this->_selectedHexPositions.push_back(hexIndex);
                }
                break;
            }
        }
    }
}

void EditorWidget::applyRoofTileSelectionVisual(int tileIndex) {
    if (!isValidTileIndex(tileIndex) || _map->getMapFile().tiles.find(_currentElevation) == _map->getMapFile().tiles.end()) {
        return;
    }
    _roofSprites.at(tileIndex).setColor(geck::ColorUtils::createRoofTileSelectionColor());

    // blank.frm background sprite makes tiles with transparent pixels visible when selected
    auto screenPos = geck::indexToScreenPosition(tileIndex, true); // true for roof offset
    sf::Sprite backgroundSprite(_resources.textures().get("art/tiles/blank.frm"));
    backgroundSprite.setPosition({ static_cast<float>(screenPos.x), static_cast<float>(screenPos.y) });
    backgroundSprite.setColor(sf::Color(Colors::ERROR_R, Colors::ERROR_G, Colors::ERROR_B, 128)); // 50% transparency
    _selectedRoofTileBackgroundSprites.push_back(backgroundSprite);
}

void EditorWidget::refreshSelectionVisuals() {
    clearAllVisualSelections();
    applySelectionVisuals(_selectionManager->getCurrentSelection());
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

    _sfmlWidget = new SFMLWidget(renderingContainer);

    // Set this EditorWidget as the delegate for SFML event handling
    _sfmlWidget->setEditorWidget(this);
    _sfmlWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    containerLayout->addWidget(_sfmlWidget);
    _layout->addWidget(renderingContainer, 1);

    setLayout(_layout);
}

void EditorWidget::init() {
    if (_map) {
        loadSprites();
        showLoadingErrorsSummary();

        // Spatial index gives O(1) area selection
        if (_selectionManager) {
            _selectionManager->initializeSpatialIndex();
        }
    }

    _selectionRectangle.setFillColor(TileColors::selectionFill());
    _selectionRectangle.setOutlineColor(TileColors::selectionOutline());
    _selectionRectangle.setOutlineThickness(2.0f);

    sf::Vector2u windowSize(800, 600); // Default size
    if (_sfmlWidget) {
        windowSize = sf::Vector2u(
            static_cast<unsigned int>(_sfmlWidget->width()),
            static_cast<unsigned int>(_sfmlWidget->height()));
    }
    _viewportController->initialize(windowSize);
}

void EditorWidget::saveMap() {
    // Default the save dialog to the current map's file name.
    const QString suggestedName = _map ? QString::fromStdString(_map->filename()) : QString();
    QString destinationQString = QtDialogs::saveFile(this, "Save Map",
        "Map Files (*.map);;All Files (*.*)", suggestedName);

    if (destinationQString.isEmpty()) {
        return;
    }

    std::string destination = destinationQString.toStdString();

    try {
        MapWriter map_writer{ [this](int32_t PID) {
            return _resources.repository().load<Pro>(ProHelper::basePath(_resources, PID));
        } };

        map_writer.openFile(destination);
        if (map_writer.write(_map->getMapFile())) {
            spdlog::info("Saved map {} ({} bytes)", destination, map_writer.getBytesWritten());
        } else {
            spdlog::error("Failed to save map {}", destination);
            QtDialogs::showError(this, "Save Failed",
                QString("Failed to save map to:\n%1").arg(destinationQString));
        }
    } catch (const geck::FileWriterException& e) {
        spdlog::error("Failed to save map {}: {}", destination, e.what());
        QtDialogs::showError(this, "Save Failed",
            QString("Failed to save map to:\n%1\n\n%2").arg(destinationQString, QString::fromStdString(e.what())));
    } catch (const std::exception& e) {
        spdlog::error("Unexpected error saving map {}: {}", destination, e.what());
    }
}

void EditorWidget::openMap() {
    QString mapPathQString = QtDialogs::openMapFile(this, "Choose Fallout 2 map to load");

    if (mapPathQString.isEmpty()) {
        spdlog::info("No map file selected");
        return;
    }

    std::string mapPath = mapPathQString.toStdString();
    spdlog::info("User requested to open new map: {}", mapPath);

    // MainWindow handles the actual loading
    Q_EMIT mapLoadRequested(mapPath);
}

void EditorWidget::createNewMap() {
    spdlog::info("Creating new empty map");

    auto newMapFile = std::make_unique<Map::MapFile>(Map::createEmptyMapFile());

    _map = std::make_unique<Map>(std::filesystem::path("newmap.map"));
    _map->setMapFile(std::move(newMapFile));

    _currentElevation = 0;

    _objects.clear();
    _floorSprites.clear();
    _roofSprites.clear();
    _wallBlockerOverlays.clear();
    _selectedHexPositions.clear();

    // Load the core helper textures needed by an empty map.
    // Resource lists are bootstrapped once during startup.
    try {
        ResourceInitializer::loadEssentialTextures(_resources);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load some essential resources for new map: {}", e.what());
    }

    if (!_selectionManager) {
        initializeSelectionSystem();
    }

    loadSprites();

    _selectionManager->clearSelection();
    _viewportController->centerViewOnMap();

    if (_mainWindow) {
        _mainWindow->updateMapInfo(_map.get());
    }

    spdlog::info("Created new empty map with {} tiles per elevation", Map::TILES_PER_ELEVATION);
}

std::vector<int> EditorWidget::calculateRectangleBorderHexes(sf::FloatRect rectangle) {
    sf::Vector2f topLeft = sf::Vector2f(rectangle.position.x, rectangle.position.y);
    sf::Vector2f topRight = sf::Vector2f(rectangle.position.x + rectangle.size.x, rectangle.position.y);
    sf::Vector2f bottomLeft = sf::Vector2f(rectangle.position.x, rectangle.position.y + rectangle.size.y);
    sf::Vector2f bottomRight = sf::Vector2f(rectangle.position.x + rectangle.size.x, rectangle.position.y + rectangle.size.y);

    int topLeftHex = _viewportController->worldPosToHexIndex(topLeft);
    int topRightHex = _viewportController->worldPosToHexIndex(topRight);
    int bottomLeftHex = _viewportController->worldPosToHexIndex(bottomLeft);
    int bottomRightHex = _viewportController->worldPosToHexIndex(bottomRight);

    if (!_hexgrid.containsPosition(topLeftHex)
        || !_hexgrid.containsPosition(topRightHex)
        || !_hexgrid.containsPosition(bottomLeftHex)
        || !_hexgrid.containsPosition(bottomRightHex)) {
        spdlog::warn("Rectangle contains invalid hex positions, skipping border calculation");
        return {};
    }

    auto borderHexes = _hexgrid.rectangleBorderPositions(topLeftHex, topRightHex, bottomLeftHex, bottomRightHex);

    spdlog::debug("Calculated {} border hexes for rectangle ({}, {}, {}, {})",
        borderHexes.size(), rectangle.position.x, rectangle.position.y, rectangle.size.x, rectangle.size.y);

    return borderHexes;
}

std::shared_ptr<MapObject> EditorWidget::createScrollBlockerObject(int hexPosition) {
    auto mapObject = std::make_shared<MapObject>();

    mapObject->position = hexPosition;
    mapObject->elevation = _currentElevation;
    mapObject->direction = 0;
    mapObject->frame_number = 0;

    // scrblk.frm: MISC type (0x05) with base ID 1
    mapObject->frm_pid = 0x05000000 | WallBlockers::SCROLL_BLOCKER_BASE_ID;

    // MISC type, generic small object proto
    mapObject->pro_pid = 0x05000000 | WallBlockers::GENERIC_PROTO_ID;

    // Scroll blockers don't block movement, they are visual indicators only
    mapObject->flags = 0;

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

void EditorWidget::loadTileSprites() {
    if (!_map) {
        return;
    }

    _mapSpriteLoader->loadTileSprites(*_map, _currentElevation, _floorSprites, _roofSprites);
}

void EditorWidget::loadSprites() {
    spdlog::stopwatch sw;
    _mapSpriteLoader->loadSprites(*_map, _currentElevation, _floorSprites, _roofSprites, _objects, _wallBlockerOverlays);

    _selectionManager->initializeSpatialIndex();

    spdlog::info("Map sprites loaded in {:.3} seconds", sw);
}

std::vector<std::shared_ptr<Object>> EditorWidget::getObjectsAtPosition(sf::Vector2f worldPos) {
    std::vector<std::shared_ptr<Object>> objectsAtPos;

    // Only objects that are actually drawn are selectable: a click must never land on a
    // hidden object (e.g. a scroll blocker on a hidden layer) and produce an invisible
    // selection. isObjectVisible is the same rule RenderingEngine::renderObjects applies.
    std::ranges::copy_if(_objects, std::back_inserter(objectsAtPos),
        [this, worldPos](const auto& object) {
            return isObjectVisible(object->getMapObject(), _visibility)
                && isPointInSpritePixel(worldPos, object->getSprite());
        });

    // Objects are drawn in _objects order (see RenderingEngine::renderObjects), so the object
    // drawn last is the one visually on top. copy_if preserved that draw order, so reverse it to
    // put the topmost-drawn object first: the pick then matches exactly what the user sees, and
    // repeated clicks cycle stacked objects from top to bottom.
    std::ranges::reverse(objectsAtPos);

    if (objectsAtPos.size() > 1) {
        spdlog::debug("getObjectsAtPosition: {} overlapping objects under cursor (topmost first)",
            objectsAtPos.size());
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

    const auto bounds = sprite.getGlobalBounds();
    const auto& texture = sprite.getTexture();

    // Convert world position to sprite-local, then to texture coordinates (accounting for texture rect and scale)
    sf::Vector2f localPos = worldPos - sf::Vector2f(bounds.position.x, bounds.position.y);
    const auto textureRect = sprite.getTextureRect();
    const auto scale = sprite.getScale();
    unsigned int texX = static_cast<unsigned int>((localPos.x / scale.x) + textureRect.position.x);
    unsigned int texY = static_cast<unsigned int>((localPos.y / scale.y) + textureRect.position.y);

    const auto texSize = texture.getSize();
    if (texX >= texSize.x || texY >= texSize.y) {
        return false;
    }

    const auto image = texture.copyToImage();
    const auto pixel = image.getPixel({ texX, texY });

    bool isHit = pixel.a > 0; // not fully transparent
    if (isHit) {
        spdlog::debug("Hit detected: world({:.2f},{:.2f}) -> local({:.2f},{:.2f}) -> tex({},{}) alpha={}",
            worldPos.x, worldPos.y, localPos.x, localPos.y, texX, texY, pixel.a);
    }

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
    std::ranges::for_each(_objects, [](auto& object) {
        if (object) {
            object->unselect();
        }
    });

    std::ranges::for_each(_floorSprites, [](auto& sprite) {
        sprite.setColor(sf::Color::White);
    });

    // Reset roof sprites: empty tiles back to transparent, others to white
    if (_map && _map->getMapFile().tiles.find(_currentElevation) != _map->getMapFile().tiles.end()) {
        for (int i = 0; i < static_cast<int>(_roofSprites.size()); ++i) {
            auto tile = _map->getMapFile().tiles.at(_currentElevation).at(i);
            if (tile.getRoof() == Map::EMPTY_TILE) {
                _roofSprites[i].setColor(geck::TileColors::transparent());
            } else {
                _roofSprites[i].setColor(sf::Color::White);
            }
        }
    } else {
        std::ranges::for_each(_roofSprites, [](auto& sprite) {
            sprite.setColor(sf::Color::White);
        });
    }

    _selectedRoofTileBackgroundSprites.clear();
    _selectedHexPositions.clear();
}

// SFML event handling interface (called by SFMLWidget)
void EditorWidget::handleEvent(const sf::Event& event) {
    if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        if (_viewportController) {
            _viewportController->updateViewForWindowSize(sf::Vector2u(resized->size.x, resized->size.y));
            spdlog::debug("EditorWidget: Handled window resize to {}x{}", resized->size.x, resized->size.y);
        }
    }

    if (_inputHandler && _sfmlWidget) {
        if (auto* target = _sfmlWidget->getRenderTarget()) {
            _inputHandler->handleEvent(event, *target, _viewportController->getView());
        }
    }
}

void EditorWidget::setupInputCallbacks() {
    if (!_inputHandler)
        return;

    InputHandler::Callbacks callbacks;

    // Mouse events
    callbacks.onSelectionClick = [this](sf::Vector2f worldPos, InputHandler::SelectionModifier modifier) {
        SelectionModifier selectionModifier;
        switch (modifier) {
            case InputHandler::SelectionModifier::ADD:
                selectionModifier = SelectionModifier::ADD;
                break;
            case InputHandler::SelectionModifier::TOGGLE:
                selectionModifier = SelectionModifier::TOGGLE;
                break;
            case InputHandler::SelectionModifier::RANGE:
                selectionModifier = SelectionModifier::RANGE;
                break;
            default:
                selectionModifier = SelectionModifier::NONE;
                break;
        }
        selectAtPosition(worldPos, selectionModifier);
    };

    callbacks.onDragSelectionPreview = [this](sf::Vector2f startPos, sf::Vector2f currentPos, InputHandler::SelectionModifier modifier) {
        updateDragSelectionPreview(startPos, currentPos, modifier == InputHandler::SelectionModifier::TOGGLE);
    };

    callbacks.onDragSelection = [this](sf::Vector2f startPos, sf::Vector2f endPos, InputHandler::SelectionModifier modifier) {
        float left = std::min(startPos.x, endPos.x);
        float top = std::min(startPos.y, endPos.y);
        float width = std::abs(endPos.x - startPos.x);
        float height = std::abs(endPos.y - startPos.y);
        sf::FloatRect selectionArea({ left, top }, { width, height });

        if (_currentSelectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE) {
            auto borderHexes = calculateRectangleBorderHexes(selectionArea);
            createScrollBlockersFromHexes(borderHexes);
        } else if (modifier == InputHandler::SelectionModifier::TOGGLE) {
            // Ctrl+drag only removes already-selected items in the area; it never adds.
            _selectionManager->deselectArea(selectionArea, _currentSelectionMode, _currentElevation);
        } else {
            auto result = _selectionManager->selectArea(selectionArea, _currentSelectionMode, _currentElevation);
            if (result.success) {
                spdlog::debug("Area selection completed: {}", result.message);
            }
        }
    };

    callbacks.onTilePlacement = [this](sf::Vector2f worldPos) {
        bool isRoof = _inputHandler->isInTilePlacementMode() && sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift);
        _tilePlacementManager->handleTilePlacement(worldPos, isRoof);
        clearDragSelectionPreview(); // Clear yellow selection tint after placement
    };

    callbacks.onTileAreaFill = [this](sf::Vector2f startPos, sf::Vector2f endPos, bool isRoof) {
        _tilePlacementManager->handleTileAreaFill(startPos, endPos, isRoof);
        clearDragSelectionPreview();         // Clear yellow selection tint after area fill
        _selectionManager->clearSelection(); // Clear selection so it doesn't interfere with next tile selection
    };

    callbacks.onPan = [this](sf::Vector2f delta) {
        sf::Vector2f center = _viewportController->getView().getCenter();
        _viewportController->getView().setCenter(center + delta);
    };

    callbacks.onZoom = [this](float direction) {
        _viewportController->zoomView(direction);
    };

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

    callbacks.onPlayerPositionSelect = [this](sf::Vector2f worldPos) {
        int hexPosition = _viewportController->worldPosToHexIndex(worldPos);
        if (hexPosition >= 0) {
            Q_EMIT playerPositionSelected(hexPosition);
            spdlog::debug("EditorWidget: Player position selected at hex {}", hexPosition);
        }
        Q_EMIT statusMessageClearRequested();
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

    callbacks.onStampPattern = [this](sf::Vector2f worldPos) {
        stampPatternAt(worldPos);
    };

    callbacks.onStampPatternCancel = [this]() {
        setMode(EditorMode::Select);
        Q_EMIT statusMessageRequested("Pattern stamping cancelled.");
    };

    callbacks.onStampCycleVariant = [this]() {
        cycleStampVariant();
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

    callbacks.onMouseMove = [this](sf::Vector2f worldPos) {
        _currentHoverHex = _viewportController->updateHoverHex(worldPos);
        Q_EMIT hexHoverChanged(_currentHoverHex);
        if (_mode == EditorMode::StampPattern) {
            updateStampPreview(worldPos);
        }
    };

    callbacks.onEscape = [this]() {
        if (_mode == EditorMode::StampPattern) {
            setMode(EditorMode::Select);
            Q_EMIT statusMessageRequested("Pattern stamping cancelled.");
        } else if (_tilePlacementManager->isTilePlacementMode()) {
            _tilePlacementManager->resetState();
            if (_mainWindow && _mainWindow->getTilePalettePanel()) {
                _mainWindow->getTilePalettePanel()->deselectTile();
            }
        } else {
            clearSelection();
        }
    };

    callbacks.onDeleteObjects = [this]() {
        deleteSelectedObjects();
    };

    callbacks.onMarkExitsModeCancelled = [this]() {
        // Notify MainWindow to deselect the toolbar button
        if (_mainWindow) {
            _mainWindow->deselectMarkExitsMode();
        }
    };

    _inputHandler->setCallbacks(callbacks);

    _inputHandler->setSelectionMode(_currentSelectionMode);
}

void EditorWidget::createScrollBlockersFromHexes(const std::vector<int>& borderHexes) {
    if (borderHexes.empty()) {
        spdlog::warn("No valid border hexes found for scroll blocker rectangle");
        return;
    }

    int scrollBlockersCreated = 0;
    for (int hexPos : borderHexes) {
        auto scrollBlockerObject = createScrollBlockerObject(hexPos);

        _map->getMapFile().map_objects[_currentElevation].push_back(scrollBlockerObject);

        // Create visual object for immediate display
        try {
            std::string frmPath = _resources.frmResolver().resolve(scrollBlockerObject->frm_pid);
            auto frm = _resources.repository().find<Frm>(frmPath);
            if (!frm) {
                frm = _resources.repository().load<Frm>(frmPath);
            }

            if (frm) {
                auto object = std::make_shared<Object>(frm);
                sf::Sprite sprite{ _resources.textures().get(frmPath) };
                object->setSprite(std::move(sprite));
                object->setDirection(static_cast<ObjectDirection>(scrollBlockerObject->direction));
                if (auto hex = _hexgrid.getHexByPosition(static_cast<uint32_t>(hexPos)); hex.has_value()) {
                    object->setHexPosition(hex->get());
                }
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
    // Called by the SFMLWidget's update loop
}

void EditorWidget::render(sf::RenderTarget& target, [[maybe_unused]] const float dt) {
    // Called by the SFMLWidget's render loop
    if (!_renderingEngine) {
        return;
    }

    RenderingEngine::VisibilitySettings visibility;
    visibility.showObjects = _visibility.showObjects;
    visibility.showCritters = _visibility.showCritters;
    visibility.showWalls = _visibility.showWalls;
    visibility.showRoof = _visibility.showRoof;
    visibility.showScrollBlockers = _visibility.showScrollBlockers;
    visibility.showWallBlockers = _visibility.showWallBlockers;
    visibility.showHexGrid = _visibility.showHexGrid;
    visibility.showLightOverlays = _visibility.showLightOverlays;
    visibility.showExitGrids = _visibility.showExitGrids;

    RenderingEngine::RenderData renderData;
    renderData.floorSprites = &_floorSprites;
    renderData.roofSprites = &_roofSprites;
    renderData.objects = &_objects;
    renderData.wallBlockerOverlays = &_wallBlockerOverlays;
    renderData.selectedRoofTileBackgroundSprites = &_selectedRoofTileBackgroundSprites;
    renderData.selectedHexPositions = &_selectedHexPositions;
    renderData.dragPreviewObject = &_dragPreviewObject;
    renderData.isDraggingFromPalette = _isDraggingFromPalette;
    renderData.stampPreviewFloorTiles = &_stampPreviewFloorTiles;
    renderData.stampPreviewObjects = &_stampPreviewObjects;
    renderData.stampPreviewRoofTiles = &_stampPreviewRoofTiles;
    renderData.selectionRectangle = &_selectionRectangle;
    // Use InputHandler state for drag selection rendering
    renderData.isDragSelecting = _inputHandler && _inputHandler->isDragging();
    renderData.currentSelectionMode = _currentSelectionMode;
    renderData.hexGrid = &_hexgrid;
    renderData.currentHoverHex = _currentHoverHex;
    renderData.playerPositionHex = _map ? static_cast<int>(_map->getMapFile().header.player_default_position) : -1;
    renderData.map = _map.get();
    renderData.currentElevation = _currentElevation;

    _renderingEngine->render(target, _viewportController->getView(), renderData, visibility);
}

bool EditorWidget::selectAtPosition(sf::Vector2f worldPos) {
    return selectAtPosition(worldPos, SelectionModifier::NONE);
}

bool EditorWidget::selectAtPosition(sf::Vector2f worldPos, SelectionModifier modifier) {
    selection::SelectionResult result;

    switch (modifier) {
        case SelectionModifier::NONE:
            result = _selectionManager->selectAtPosition(worldPos, _currentSelectionMode, _currentElevation);
            break;

        case SelectionModifier::ADD:
            result = _selectionManager->addToSelection(worldPos, _currentSelectionMode, _currentElevation);
            spdlog::debug("Add to selection at ({:.1f}, {:.1f})", worldPos.x, worldPos.y);
            break;

        case SelectionModifier::TOGGLE:
            result = _selectionManager->deselectAtPosition(worldPos, _currentSelectionMode, _currentElevation);
            spdlog::debug("Deselect at ({:.1f}, {:.1f})", worldPos.x, worldPos.y);
            break;

        case SelectionModifier::RANGE:
            result = handleRangeSelection(worldPos);
            spdlog::debug("Range selection at ({:.1f}, {:.1f})", worldPos.x, worldPos.y);
            break;
    }

    if (result.success && result.selectionChanged) {
        const auto& selection = _selectionManager->getCurrentSelection();
        if (selection.count() > 1) {
            spdlog::info("Multi-selection: {} items selected", selection.count());
        }
    }

    return result.success && result.selectionChanged;
}

void EditorWidget::cycleSelectionMode() {
    int currentMode = static_cast<int>(_currentSelectionMode);
    currentMode = (currentMode + 1) % static_cast<int>(SelectionMode::NUM_SELECTION_TYPES);
    _currentSelectionMode = static_cast<SelectionMode>(currentMode);

    if (_inputHandler) {
        _inputHandler->setSelectionMode(_currentSelectionMode);
    }

    _selectionManager->clearSelection();

    spdlog::info("Selection mode changed to: {}", selectionModeToString(_currentSelectionMode));
}

void EditorWidget::setSelectionMode(SelectionMode mode) {
    if (_currentSelectionMode == mode) {
        return;
    }

    _currentSelectionMode = mode;

    if (_inputHandler) {
        _inputHandler->setSelectionMode(_currentSelectionMode);
    }

    _selectionManager->clearSelection();

    spdlog::info("Selection mode set to: {}", selectionModeToString(_currentSelectionMode));
}

void EditorWidget::toggleScrollBlockerRectangleMode() {
    if (_currentSelectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE) {
        _currentSelectionMode = SelectionMode::ALL;
        spdlog::info("Scroll blocker rectangle mode disabled, switched to ALL mode");
    } else {
        _currentSelectionMode = SelectionMode::SCROLL_BLOCKER_RECTANGLE;
        // Auto-enable scroll blocker visibility so the user can see what they are placing
        if (!_visibility.showScrollBlockers) {
            _visibility.showScrollBlockers = true;
            spdlog::info("Automatically enabled scroll blocker visibility");
        }
        spdlog::info("Scroll blocker rectangle mode enabled");
    }

    if (_inputHandler) {
        _inputHandler->setSelectionMode(_currentSelectionMode);
    }

    _selectionManager->clearSelection();
}

void EditorWidget::rotateSelectedObject() {
    const auto& selection = _selectionManager->getCurrentSelection();
    auto objects = selection.getObjects();

    if (!objects.empty()) {
        std::vector<std::shared_ptr<Object>> validObjects;
        std::vector<int> beforeDirs;
        std::vector<int> afterDirs;
        validObjects.reserve(objects.size());
        beforeDirs.reserve(objects.size());
        afterDirs.reserve(objects.size());

        for (auto& object : objects) {
            if (!object)
                continue;
            validObjects.push_back(object);
            beforeDirs.push_back(static_cast<int>(object->getMapObject().direction));
            object->rotate();
            afterDirs.push_back(static_cast<int>(object->getMapObject().direction));
            spdlog::debug("Rotated object to direction {}", object->getMapObject().direction);
        }
        registerObjectRotation(validObjects, beforeDirs, afterDirs);
        spdlog::info("Rotated {} selected object(s)", objects.size());
    } else {
        spdlog::debug("No objects selected for rotation");
    }
}

void EditorWidget::changeElevation(int elevation) {
    if (elevation >= ELEVATION_1 && elevation <= ELEVATION_3) {
        _currentElevation = elevation;
        loadSprites();
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

void EditorWidget::setMode(EditorMode mode, int tileIndex, bool isRoof) {
    _mode = mode;

    // Single owner of mutual exclusion: deactivate every mode's state across all
    // components, then activate the target. This replaces the scattered
    // resetState()/setX(false) calls the individual setters used to make.
    _tilePlacementManager->setTilePlacementMode(false, -1, false);
    _exitGridPlacementManager->setExitGridPlacementMode(false);
    _exitGridPlacementManager->setMarkExitsMode(false);
    _playerPositionSelectionMode = false;
    if (mode != EditorMode::StampPattern) {
        clearStampPreview();
    }
    if (_inputHandler) {
        _inputHandler->setTilePlacementMode(false, -1, false);
        _inputHandler->setExitGridPlacementMode(false);
        _inputHandler->setMarkExitsMode(false);
        _inputHandler->setPlayerPositionMode(false);
        _inputHandler->setStampPatternMode(false);
    }

    switch (mode) {
        case EditorMode::Select:
            break;
        case EditorMode::PlaceTile:
            _tilePlacementManager->setTilePlacementMode(true, tileIndex, isRoof);
            if (_inputHandler) {
                _inputHandler->setTilePlacementMode(true, tileIndex, false);
            }
            break;
        case EditorMode::PlaceExitGrid:
            _exitGridPlacementManager->setExitGridPlacementMode(true);
            if (_inputHandler) {
                // Previously skipped: placement clicks never reached the handler.
                _inputHandler->setExitGridPlacementMode(true);
            }
            break;
        case EditorMode::MarkExits:
            _exitGridPlacementManager->setMarkExitsMode(true);
            if (_inputHandler) {
                _inputHandler->setMarkExitsMode(true);
            }
            break;
        case EditorMode::SetPlayerPosition:
            _playerPositionSelectionMode = true;
            if (_inputHandler) {
                _inputHandler->setPlayerPositionMode(true);
            }
            break;
        case EditorMode::StampPattern:
            if (_inputHandler) {
                _inputHandler->setStampPatternMode(true);
            }
            break;
    }

    Q_EMIT editorModeChanged(_mode);
}

void EditorWidget::setTilePlacementMode(bool enabled, int tileIndex, bool isRoof) {
    setMode(enabled ? EditorMode::PlaceTile : EditorMode::Select, tileIndex, isRoof);
}

void EditorWidget::setTilePlacementAreaFill(bool enabled) {
    _tilePlacementManager->setTilePlacementAreaFill(enabled);
}

void EditorWidget::setTilePlacementReplaceMode(bool enabled) {
    _tilePlacementManager->setTilePlacementReplaceMode(enabled);
}

void EditorWidget::setExitGridPlacementMode(bool enabled) {
    setMode(enabled ? EditorMode::PlaceExitGrid : EditorMode::Select);
}

void EditorWidget::setMarkExitsMode(bool enabled) {
    setMode(enabled ? EditorMode::MarkExits : EditorMode::Select);
}

void EditorWidget::beginStampPattern(pattern::Pattern pattern) {
    if (pattern.variants.empty()) {
        Q_EMIT statusMessageRequested("Pattern has no variants to stamp.");
        return;
    }
    _stampPattern = std::move(pattern);
    _stampVariantIndex = 0;
    setMode(EditorMode::StampPattern);
    QString message = QString("Stamp mode: click to place '%1'.")
                          .arg(QString::fromStdString(_stampPattern->name));
    if (_stampPattern->variants.size() > 1) {
        message += " R cycles variants.";
    }
    message += " Right-click or Esc to exit.";
    Q_EMIT statusMessageRequested(message);
}

void EditorWidget::cycleStampVariant() {
    if (!_stampPattern || _stampPattern->variants.size() <= 1) {
        return;
    }
    _stampVariantIndex = (_stampVariantIndex + 1) % static_cast<int>(_stampPattern->variants.size());
    const std::string& label = _stampPattern->variants[_stampVariantIndex].label;
    Q_EMIT statusMessageRequested(
        QString("Pattern variant %1/%2: %3")
            .arg(_stampVariantIndex + 1)
            .arg(_stampPattern->variants.size())
            .arg(label.empty() ? QStringLiteral("(unnamed)") : QString::fromStdString(label)));
}

void EditorWidget::stampPatternAt(sf::Vector2f worldPos) {
    if (!_stampPattern || !_map || _stampPattern->variants.empty()) {
        return;
    }
    const int hex = _viewportController->worldPosToHexIndex(worldPos);
    if (!_hexgrid.containsPosition(hex)) {
        return;
    }
    if (_stampVariantIndex < 0 || _stampVariantIndex >= static_cast<int>(_stampPattern->variants.size())) {
        _stampVariantIndex = 0;
    }
    const pattern::PatternVariant& variant = _stampPattern->variants[_stampVariantIndex];

    pattern::PatternStamper stamper(_resources, _hexgrid, *_objectCommandController, *_map);
    const pattern::PatternStamper::Result result = stamper.stamp(variant, hex, _currentElevation);

    // PatternStamper appends object sprites and applies tile sprites incrementally
    // through the controller, so no full refresh is needed here (mirrors the single
    // placeObjectAtPosition path).

    QString message = QString("Stamped '%1': %2 objects, %3 tiles")
                          .arg(QString::fromStdString(_stampPattern->name))
                          .arg(result.objectsPlaced)
                          .arg(result.tilesPainted);
    if (result.objectsFailed > 0) {
        message += QString(" (%1 missing art)").arg(result.objectsFailed);
    }
    if (result.dropped > 0) {
        message += QString(" (%1 off-grid)").arg(result.dropped);
    }
    Q_EMIT statusMessageRequested(message);
}

void EditorWidget::clearStampPreview() {
    _stampPreviewFloorTiles.clear();
    _stampPreviewObjects.clear();
    _stampPreviewRoofTiles.clear();
    _stampPreviewHex = -1;
}

void EditorWidget::updateStampPreview(sf::Vector2f worldPos) {
    if (!_stampPattern || _mode != EditorMode::StampPattern || _stampPattern->variants.empty()) {
        clearStampPreview();
        return;
    }
    const int hex = _viewportController->worldPosToHexIndex(worldPos);
    if (!_hexgrid.containsPosition(hex)) {
        clearStampPreview();
        return;
    }
    if (hex == _stampPreviewHex) {
        return; // still over the same hex; the ghost is unchanged
    }
    _stampPreviewHex = hex;
    _stampPreviewFloorTiles.clear();
    _stampPreviewObjects.clear();
    _stampPreviewRoofTiles.clear();

    if (_stampVariantIndex < 0 || _stampVariantIndex >= static_cast<int>(_stampPattern->variants.size())) {
        _stampVariantIndex = 0;
    }
    const pattern::PatternVariant& variant = _stampPattern->variants[_stampVariantIndex];
    const pattern::PatternStamper::Plan plan = pattern::PatternStamper::plan(variant, hex);

    const auto ghostAlpha = sf::Color(255, 255, 255, 140);
    for (const pattern::PatternStamper::TilePlacement& tp : plan.tiles) {
        if (auto sprite = pattern::buildTileSprite(_resources, tp.tileIndex, tp.isRoof, tp.tileId)) {
            sprite->setColor(ghostAlpha);
            (tp.isRoof ? _stampPreviewRoofTiles : _stampPreviewFloorTiles).push_back(std::move(*sprite));
        }
    }

    for (const pattern::PatternStamper::ObjectPlacement& op : plan.objects) {
        auto object = pattern::buildSpriteObject(_resources, _hexgrid, op.frmPid, op.hex, op.direction);
        if (!object) {
            continue;
        }
        object->getSprite().setColor(ghostAlpha); // semi-transparent ghost
        _stampPreviewObjects.push_back(std::move(object));
    }
}

bool EditorWidget::isTilePlacementMode() const {
    return _tilePlacementManager->isTilePlacementMode();
}

void EditorWidget::refreshObjects() {
    if (!_map) {
        return;
    }

    _mapSpriteLoader->loadObjectSprites(*_map, _currentElevation, _objects, _wallBlockerOverlays);

    spdlog::debug("Refreshed objects for current elevation");
}

void EditorWidget::updateTileSprite(int hexIndex, bool isRoof) {
    updateTileSprite(hexIndex, isRoof, _currentElevation);
}

void EditorWidget::updateTileSprite(int hexIndex, bool isRoof, int elevation) {
    if (!_map || !isValidHexPosition(hexIndex)) {
        return;
    }

    const auto& elevationTiles = ensureElevationTiles(elevation);
    _mapSpriteLoader->updateTileSprite(hexIndex, isRoof, elevation, elevationTiles, _floorSprites, _roofSprites);
}

bool EditorWidget::isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite) {
    return sprite.getGlobalBounds().contains(worldPos);
}

std::optional<int> EditorWidget::getTileAtPosition(sf::Vector2f worldPos, bool isRoof) {
    if (!_map || _map->getMapFile().tiles.find(_currentElevation) == _map->getMapFile().tiles.end()) {
        return std::nullopt;
    }

    // Resolve by nearest tile centre (the diamond actually under the cursor) instead of
    // snapping the click to a hex and converting hex->tile, which is imprecise at boundaries.
    const auto tileIndex = screenToTileIndex(worldPos.x, worldPos.y, isRoof);
    if (!tileIndex.has_value()) {
        return std::nullopt;
    }

    // Editor-specific guard: a roof selection only counts on a non-empty roof tile.
    if (isRoof && _map->getMapFile().tiles.at(_currentElevation).at(*tileIndex).getRoof() == Map::EMPTY_TILE) {
        spdlog::debug("EditorWidget::getTileAtPosition: Empty roof tile at index {} [worldPos: ({:.1f}, {:.1f})]",
            *tileIndex, worldPos.x, worldPos.y);
        return std::nullopt;
    }

    return *tileIndex;
}

std::optional<int> EditorWidget::getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos) {
    // Includes empty roof tiles in the selection. Resolves by nearest roof-tile centre
    // (screenToTileIndex applies the roof offset) for accuracy at tile boundaries.
    if (!_map || _map->getMapFile().tiles.find(_currentElevation) == _map->getMapFile().tiles.end()) {
        return std::nullopt;
    }

    return screenToTileIndex(worldPos.x, worldPos.y, true);
}

selection::SelectionResult EditorWidget::handleRangeSelection(sf::Vector2f worldPos) {
    // Range selection is primarily for tiles. It needs a starting point, so if
    // nothing is selected, fall back to a normal single selection.
    const auto& currentSelection = _selectionManager->getCurrentSelection();

    if (currentSelection.isEmpty()) {
        return _selectionManager->selectAtPosition(worldPos, _currentSelectionMode, _currentElevation);
    }

    // First selected tile becomes the range start point
    sf::Vector2f startPos;
    bool hasStartTile = false;

    for (const auto& item : currentSelection.items) {
        if (item.type == selection::SelectionType::FLOOR_TILE || item.type == selection::SelectionType::ROOF_TILE) {
            int tileIndex = item.getTileIndex();
            auto screenPos = indexToScreenPosition(tileIndex);
            startPos = sf::Vector2f(static_cast<float>(screenPos.x), static_cast<float>(screenPos.y));
            hasStartTile = true;
            break;
        }
    }

    if (!hasStartTile) {
        return _selectionManager->selectAtPosition(worldPos, _currentSelectionMode, _currentElevation);
    }

    float left = std::min(startPos.x, worldPos.x);
    float top = std::min(startPos.y, worldPos.y);
    float right = std::max(startPos.x, worldPos.x);
    float bottom = std::max(startPos.y, worldPos.y);

    // Padding ensures we catch tiles at the edges
    sf::FloatRect selectionArea(
        { left - AREA_SELECTION_X_PADDING, top - AREA_SELECTION_Y_PADDING },
        { (right - left) + AREA_SELECTION_X_TOTAL_PADDING, (bottom - top) + AREA_SELECTION_Y_TOTAL_PADDING });

    SelectionMode areaMode = _currentSelectionMode;
    if (_currentSelectionMode == SelectionMode::ALL) {
        areaMode = SelectionMode::FLOOR_TILES; // ALL mode defaults to floor tiles for range selection
    }

    auto result = _selectionManager->selectArea(selectionArea, areaMode, _currentElevation);

    spdlog::info("Range selection: area ({:.1f}, {:.1f}, {:.1f}, {:.1f})",
        selectionArea.position.x, selectionArea.position.y, selectionArea.size.x, selectionArea.size.y);

    return result;
}

void EditorWidget::updateDragSelectionPreview(sf::Vector2f startWorldPos, sf::Vector2f currentWorldPos, bool isDeselect) {
    clearDragPreview();

    float left = std::min(startWorldPos.x, currentWorldPos.x);
    float top = std::min(startWorldPos.y, currentWorldPos.y);
    float width = std::abs(currentWorldPos.x - startWorldPos.x);
    float height = std::abs(currentWorldPos.y - startWorldPos.y);
    sf::FloatRect selectionArea({ left, top }, { width, height });

    _selectionRectangle.setPosition({ left, top });
    _selectionRectangle.setSize({ width, height });

    if (isDeselect) {
        // Ctrl+drag: show the selection with the covered (visible) selected items removed, so
        // they un-highlight live while everything else stays highlighted. The commit
        // (deselectArea) and the manager's preview query share the same visibility rules, so
        // the preview matches exactly what will be deselected.
        auto toRemove = _selectionManager->itemsToDeselectInArea(selectionArea, _currentSelectionMode, _currentElevation);
        selection::SelectionState preview = _selectionManager->getCurrentSelection();
        for (const auto& item : toRemove) {
            preview.removeItem(item);
        }
        clearAllVisualSelections();
        applySelectionVisuals(preview);
        return;
    }

    switch (_currentSelectionMode) {
        case SelectionMode::FLOOR_TILES:
            previewAreaTiles(selectionArea, false, false);
            break;

        case SelectionMode::ROOF_TILES:
            previewAreaTiles(selectionArea, true, false);
            break;

        case SelectionMode::ROOF_TILES_ALL:
            previewAreaTiles(selectionArea, true, true);
            break;

        case SelectionMode::OBJECTS:
            previewAreaObjects(selectionArea);
            break;

        case SelectionMode::ALL:
            previewAreaTiles(selectionArea, false, false);
            previewAreaTiles(selectionArea, true, false);
            previewAreaObjects(selectionArea);
            break;

        default:
            break;
    }
}

void EditorWidget::previewAreaTiles(const sf::FloatRect& area, bool roof, bool includeEmpty) {
    auto tiles = includeEmpty ? _selectionManager->getTilesInAreaIncludingEmpty(area, roof, _currentElevation)
                              : _selectionManager->getTilesInArea(area, roof, _currentElevation);
    auto& sprites = roof ? _roofSprites : _floorSprites;
    for (int tileIndex : tiles) {
        if (isValidTileIndex(tileIndex)) {
            applyPreviewHighlight(sprites.at(tileIndex)); // also makes empty (transparent) roof tiles visible
        }
    }
    _previewTiles.insert(_previewTiles.end(), tiles.begin(), tiles.end());
}

void EditorWidget::previewAreaObjects(const sf::FloatRect& area) {
    auto objects = _selectionManager->getObjectsInArea(area, _currentElevation);
    std::ranges::for_each(objects, [](auto& object) {
        if (object) {
            applyPreviewHighlight(object->getSprite());
        }
    });
    _previewObjects.insert(_previewObjects.end(), objects.begin(), objects.end());
}

void EditorWidget::updateTileAreaFillPreview(sf::Vector2f startWorldPos, sf::Vector2f currentWorldPos) {
    clearDragPreview();

    float left = std::min(startWorldPos.x, currentWorldPos.x);
    float top = std::min(startWorldPos.y, currentWorldPos.y);
    float width = std::abs(currentWorldPos.x - startWorldPos.x);
    float height = std::abs(currentWorldPos.y - startWorldPos.y);
    sf::FloatRect selectionArea({ left, top }, { width, height });

    bool isRoof = _tilePlacementManager->getTilePlacementIsRoof();
    _previewTiles = _selectionManager->getTilesInArea(selectionArea, isRoof, _currentElevation);

    auto& sprites = isRoof ? _roofSprites : _floorSprites;
    for (int tileIndex : _previewTiles) {
        if (isValidTileIndex(tileIndex)) {
            applyPreviewHighlight(sprites.at(tileIndex));
        }
    }
}

void EditorWidget::clearDragPreview() {
    _selectionRectangle.setSize({ 0, 0 });
    _selectionRectangle.setPosition({ 0, 0 });

    for (int tileIndex : _previewTiles) {
        if (isValidTileIndex(tileIndex)) {
            removePreviewHighlight(_floorSprites.at(tileIndex));

            // Empty roof sprites must go back to transparent, not white
            if (_map && _map->getMapFile().tiles.find(_currentElevation) != _map->getMapFile().tiles.end()) {
                auto tile = _map->getMapFile().tiles.at(_currentElevation).at(tileIndex);
                if (tile.getRoof() == Map::EMPTY_TILE) {
                    _roofSprites.at(tileIndex).setColor(geck::TileColors::transparent());
                } else {
                    removePreviewHighlight(_roofSprites.at(tileIndex));
                }
            } else {
                removePreviewHighlight(_roofSprites.at(tileIndex));
            }
        }
    }

    std::ranges::for_each(_previewObjects, [](auto& object) {
        if (object) {
            removePreviewHighlight(object->getSprite());
        }
    });

    _previewTiles.clear();
    _previewObjects.clear();
}

std::vector<Tile>& EditorWidget::ensureElevationTiles(int elevation) {
    auto& mapFile = _map->getMapFile();
    auto& tilesVec = mapFile.tiles[elevation];
    if (tilesVec.size() < Map::TILES_PER_ELEVATION) {
        tilesVec.resize(Map::TILES_PER_ELEVATION, Tile(Map::EMPTY_TILE, Map::EMPTY_TILE));
    }
    return tilesVec;
}

void EditorWidget::updateMarkExitsPreview(sf::Vector2f startWorldPos, sf::Vector2f currentWorldPos) {
    clearDragPreview();

    float left = std::min(startWorldPos.x, currentWorldPos.x);
    float top = std::min(startWorldPos.y, currentWorldPos.y);
    float width = std::abs(currentWorldPos.x - startWorldPos.x);
    float height = std::abs(currentWorldPos.y - startWorldPos.y);
    sf::FloatRect selectionArea({ left, top }, { width, height });

    _selectionRectangle.setPosition({ left, top });
    _selectionRectangle.setSize({ width, height });

    // Highlight only exit grid objects
    for (auto& object : _objects) {
        if (!object || !object->getMapObjectPtr() || !object->getMapObjectPtr()->isExitGridMarker()) {
            continue;
        }

        const auto& sprite = object->getSprite();
        auto objectBounds = sprite.getGlobalBounds();

        auto intersection = selectionArea.findIntersection(objectBounds);
        if (intersection.has_value()) {
            _previewObjects.push_back(object);
            // Bright magenta highlight contrasts well against green exit grids
            object->getSprite().setColor(geck::TileColors::exitGridHighlight());
        }
    }
}

void EditorWidget::placeObjectAtPosition(sf::Vector2f worldPos) {
    if (!_map) {
        spdlog::warn("EditorWidget: Cannot place object - no map loaded");
        return;
    }

    int hexPosition = _viewportController->worldPosToHexIndex(worldPos);
    if (!_hexgrid.containsPosition(hexPosition)) {
        spdlog::warn("EditorWidget: Invalid hex position {} for object placement", hexPosition);
        return;
    }

    auto mapObject = std::make_shared<MapObject>();

    mapObject->position = hexPosition;
    mapObject->elevation = _currentElevation;
    mapObject->direction = 0;
    mapObject->frame_number = 0;

    auto hexCoords = _hexgrid.getHexByPosition(static_cast<uint32_t>(hexPosition));
    if (hexCoords) {
        mapObject->x = static_cast<uint32_t>(hexCoords->get().x());
        mapObject->y = static_cast<uint32_t>(hexCoords->get().y());
    }

    // Only use actual PIDs from ObjectInfo - fail rather than substitute placeholder PIDs
    if (!_previewObjectInfo || !_previewObjectInfo->pro) {
        spdlog::error("EditorWidget: Cannot place object - no ObjectInfo or PRO data available");
        return;
    }

    mapObject->pro_pid = _previewObjectInfo->pro->header.PID;
    mapObject->frm_pid = _previewObjectInfo->pro->header.FID;

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

    // Create visual Object for immediate display
    try {
        const Frm* frm = nullptr;
        if (_previewObjectInfo && !_previewObjectInfo->frmPath.isEmpty()) {
            frm = _resources.repository().load<Frm>(_previewObjectInfo->frmPath.toStdString());
        }

        auto object = std::make_shared<Object>(frm);
        object->setMapObject(mapObject);

        if (frm && _previewObjectInfo && !_previewObjectInfo->frmPath.isEmpty()) {
            sf::Sprite objectSprite{ _resources.textures().get(_previewObjectInfo->frmPath.toStdString()) };
            object->setSprite(std::move(objectSprite));
            object->setDirection(static_cast<ObjectDirection>(0));
        }

        if (auto hex = _hexgrid.getHexByPosition(static_cast<uint32_t>(hexPosition)); hex.has_value()) {
            object->setHexPosition(hex->get());
        }

        spdlog::info("EditorWidget: Successfully placed object at hex {} (pro_pid: {})",
            hexPosition, mapObject->pro_pid);

        registerObjectPlacement(mapObject, object);

    } catch (const std::exception& e) {
        spdlog::warn("EditorWidget: Failed to create visual object for placed item: {}", e.what());
        // The MapObject is still saved, just won't be visible until reload
    }
}

void EditorWidget::startDragPreview(int objectIndex, int categoryInt, sf::Vector2f worldPos) {
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
    setMode(EditorMode::SetPlayerPosition);

    Q_EMIT statusMessageRequested("Click on a hex to set the player starting position (Press Escape to cancel)");

    spdlog::debug("EditorWidget: Entered player position selection mode");
}

void EditorWidget::centerViewOnPlayerPosition() {
    if (!_map) {
        spdlog::warn("EditorWidget::centerViewOnPlayerPosition: No map loaded");
        return;
    }

    uint32_t playerHexPosition = _map->getMapFile().header.player_default_position;

    auto hex = _hexgrid.getHexByPosition(playerHexPosition);
    if (!hex) {
        spdlog::warn("EditorWidget::centerViewOnPlayerPosition: Invalid player hex position {}", playerHexPosition);
        return;
    }

    float screenX = static_cast<float>(hex->get().x());
    float screenY = static_cast<float>(hex->get().y());

    _viewportController->getView().setCenter(sf::Vector2f(screenX, screenY));

    spdlog::debug("EditorWidget::centerViewOnPlayerPosition: Centered view on player position {} at screen ({}, {})",
        playerHexPosition, screenX, screenY);
}

void EditorWidget::showLoadingErrorsSummary() {
    const auto& loadingErrors = _mapSpriteLoader->lastLoadErrors();
    if (!loadingErrors.hasErrors()) {
        return;
    }

    QString title = "Map Loading Warnings";
    QString message;

    message += QString("Some objects could not be loaded:\n\n");
    message += QString("• %1 objects skipped due to missing or invalid FRM files\n")
                   .arg(loadingErrors.objectsSkipped);

    if (!loadingErrors.failedFrmNames.empty()) {
        message += QString("• %1 unique FRM files failed to load:\n\n")
                       .arg(loadingErrors.failedFrmNames.size());

        // Show up to 10 FRM files to avoid overwhelming the user
        int count = 0;
        const int maxShow = 10;
        for (const auto& frmName : loadingErrors.failedFrmNames) {
            if (count >= maxShow) {
                message += QString("  ... and %1 more\n")
                               .arg(loadingErrors.failedFrmNames.size() - maxShow);
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
    _visibility.showLightOverlays = show;

    int lightObjectCount = 0;
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
    clearDragPreview();
    // A Ctrl+drag preview temporarily un-highlights the covered selected items; restore the
    // real selection highlight so cancelling the drag (or clearing after another op) leaves
    // the selection looking correct.
    refreshSelectionVisuals();

    spdlog::debug("EditorWidget::clearDragSelectionPreview() - cleared selection rectangle");
}

void EditorWidget::onObjectFrmChanged(std::shared_ptr<Object> object, uint32_t newFrmPid) {
    if (!object) {
        spdlog::warn("EditorWidget::onObjectFrmChanged - null object provided");
        return;
    }

    try {
        std::string newFrmPath = _resources.frmResolver().resolve(newFrmPid);
        uint32_t oldFrmPid = object->hasMapObject() ? object->getMapObject().frm_pid : 0;
        std::string oldFrmPath = _resources.frmResolver().resolve(oldFrmPid);

        if (newFrmPath.empty()) {
            spdlog::error("EditorWidget::onObjectFrmChanged - could not resolve FRM path for PID {}", newFrmPid);
            return;
        }

        registerObjectFrmChange(object, oldFrmPid, oldFrmPath, newFrmPid, newFrmPath);
        spdlog::info("EditorWidget::onObjectFrmChanged - updated object visual to FRM PID {} ({})", newFrmPid, newFrmPath);
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
        uint32_t oldFrmPid = object->hasMapObject() ? object->getMapObject().frm_pid : 0;
        std::string oldFrmPath = _resources.frmResolver().resolve(oldFrmPid);

        registerObjectFrmChange(object, oldFrmPid, oldFrmPath, oldFrmPid, newFrmPath);
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

    std::vector<std::pair<std::shared_ptr<MapObject>, std::shared_ptr<Object>>> removedObjects;
    for (const auto& object : selectedObjects) {
        if (!object || !object->hasMapObject())
            continue;

        try {
            // Use the actual shared_ptr; do not construct one with a no-op deleter here
            auto mapObjPtr = object->getMapObjectPtr();
            if (!mapObjPtr) {
                spdlog::warn("EditorWidget::deleteSelectedObjects - Object has null MapObject shared_ptr");
                continue;
            }
            removedObjects.push_back({ mapObjPtr, object });
            removePlacedObject(mapObjPtr, object);
        } catch (const std::exception& e) {
            spdlog::error("EditorWidget::deleteSelectedObjects - Error deleting object: {}", e.what());
        }
    }

    _objectCommandController->registerObjectDeletion(removedObjects);

    _selectionManager->clearSelection();

    Q_EMIT selectionChanged(_selectionManager->getCurrentSelection(), _currentElevation);

    spdlog::info("EditorWidget::deleteSelectedObjects - Successfully deleted {} objects", removedObjects.size());
}

} // namespace geck
