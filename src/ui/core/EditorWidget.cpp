#include "EditorWidget.h"
#include "ui/widgets/SFMLWidget.h"
#include "ui/input/InputHandler.h"
#ifdef GECK_SCRIPTING_ENABLED
#include "Application.h"
#include "scripting/MapScriptApi.h"
#include "pattern/PatternLibrary.h"
#include "pattern/PatternSerializer.h"
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#endif
#include "rendering/MapSpriteLoader.h"
#include "rendering/RenderingEngine.h"
#include "ui/dragdrop/DragDropManager.h"
#include "ui/tiles/TilePlacementManager.h"
#include "ui/tools/ExitGridPlacementManager.h"
#include "viewport/ViewportController.h"
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
    , _resources(resources) {
    _session.setMap(std::move(map));
    _session.floorSprites().reserve(Map::TILES_PER_ELEVATION);
    _session.roofSprites().reserve(Map::TILES_PER_ELEVATION);

    if (_session.map()) {
        initializeSelectionSystem();
    }

    _renderingEngine = std::make_unique<RenderingEngine>(_resources);
    _mapSpriteLoader = std::make_unique<MapSpriteLoader>(_resources, _session.hexgrid());
    _objectCommandController = std::make_unique<ObjectCommandController>(
        _resources,
        _session.mapPtr(),
        _session.hexgrid(),
        *_mapSpriteLoader,
        _session.objects(),
        _session.wallBlockerOverlays(),
        _session.undoStack(),
        [this]() { refreshObjects(); },
        [this]() { Q_EMIT undoStackChanged(); },
        [this](int elevation) -> std::vector<Tile>& { return ensureElevationTiles(elevation); },
        [this]() { return _session.currentElevation(); },
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
    _viewportController = std::make_unique<ViewportController>(&_session.hexgrid());
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

// The register*() helpers below forward to the controller, which emits undoStackChanged
// through its onStackChanged callback (wired in the constructor).
void EditorWidget::registerObjectPlacement(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    _objectCommandController->registerObjectPlacement(mapObject, object);
}

void EditorWidget::registerObjectMove(const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<std::pair<int, int>>& moves) {
    _objectCommandController->registerObjectMove(objects, moves);
}

void EditorWidget::moveSelectedTilesForDrag(sf::Vector2f worldTranslation) {
    if (!_session.selectionManager()) {
        return;
    }
    const auto changes = _session.selectionManager()->planSelectionMoveForTranslation(worldTranslation);
    _objectCommandController->applyTileEdit("Move Tiles", changes);
}

std::optional<selection::SelectedItem> EditorWidget::remapSelectedItemAfterMove(
    const selection::SelectedItem& item,
    const std::unordered_map<const MapObject*, std::shared_ptr<Object>>& objectsByMapObject,
    const std::optional<std::pair<int, int>>& tileDelta) const {
    using enum selection::SelectionType;
    switch (item.type) {
        case OBJECT: {
            // The MapObject survived the move; find its refreshed wrapper.
            const auto oldObject = item.getObject();
            if (!oldObject || !oldObject->hasMapObject()) {
                return std::nullopt;
            }
            const auto found = objectsByMapObject.find(oldObject->getMapObjectPtr().get());
            if (found == objectsByMapObject.end()) {
                return std::nullopt;
            }
            return selection::SelectedItem{ OBJECT, found->second };
        }
        case FLOOR_TILE:
        case ROOF_TILE: {
            if (!tileDelta.has_value()) {
                return item; // no tile movement; keep as-is
            }
            const auto coords = indexToCoordinates(item.getTileIndex());
            const int newRow = static_cast<int>(coords.x) + tileDelta->first;
            const int newColumn = static_cast<int>(coords.y) + tileDelta->second;
            if (!isTileRowColInGrid(newRow, newColumn)) {
                return std::nullopt;
            }
            return selection::SelectedItem{ item.type,
                coordinatesToIndex(TileCoordinates(static_cast<unsigned int>(newRow), static_cast<unsigned int>(newColumn))) };
        }
        case HEX:
            return item; // hex markers don't move with a region drag
    }
    return std::nullopt;
}

void EditorWidget::reselectAfterDragMove(sf::Vector2f worldTranslation) {
    if (!_session.selectionManager()) {
        return;
    }
    const auto& current = _session.selectionManager()->getCurrentSelection();
    if (current.items.empty()) {
        return;
    }

    // The object move rebuilt _session.objects() (fresh wrappers around the same, now-moved MapObjects),
    // orphaning the selection's old wrappers; the tile items still hold pre-move indices. Rebuild the
    // selection so it follows the move: re-point objects by MapObject identity, and shift the tile
    // items by the same whole-tile delta the move used.
    const auto tileDelta = _session.selectionManager()->selectionTileDelta(worldTranslation);

    std::unordered_map<const MapObject*, std::shared_ptr<Object>> objectsByMapObject;
    for (const auto& object : _session.objects()) {
        if (object && object->hasMapObject()) {
            objectsByMapObject[object->getMapObjectPtr().get()] = object;
        }
    }

    std::vector<selection::SelectedItem> rebuilt;
    rebuilt.reserve(current.items.size());
    for (const auto& item : current.items) {
        if (auto remapped = remapSelectedItemAfterMove(item, objectsByMapObject, tileDelta)) {
            rebuilt.push_back(std::move(*remapped));
        }
    }

    _session.selectionManager()->setSelectedItems(std::move(rebuilt));
}

void EditorWidget::beginMoveBatch(const std::string& description) {
    _objectCommandController->beginBatch(description);
}

void EditorWidget::endMoveBatch() {
    _objectCommandController->endBatch();
}

void EditorWidget::beginTileDragPreview() {
    _tileDragPreviewBases.clear();

    const auto capture = [this](bool roof, const std::vector<int>& indices, const std::vector<sf::Sprite>& sprites) {
        for (int tileIndex : indices) {
            if (tileIndex >= 0 && tileIndex < static_cast<int>(sprites.size())) {
                _tileDragPreviewBases.push_back({ roof, tileIndex, sprites[tileIndex].getPosition() });
            }
        }
    };
    capture(false, _selectedFloorVisuals, _session.floorSprites());
    if (_session.visibility().showRoof) {
        capture(true, _selectedRoofVisuals, _session.roofSprites());
    }
}

void EditorWidget::previewTileDrag(sf::Vector2f worldOffset) {
    for (const auto& base : _tileDragPreviewBases) {
        auto& sprites = base.roof ? _session.roofSprites() : _session.floorSprites();
        sprites[base.tileIndex].setPosition(base.basePosition + worldOffset);
    }
}

void EditorWidget::endTileDragPreview() {
    for (const auto& base : _tileDragPreviewBases) {
        auto& sprites = base.roof ? _session.roofSprites() : _session.floorSprites();
        sprites[base.tileIndex].setPosition(base.basePosition);
    }
    _tileDragPreviewBases.clear();
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
    bool result = _session.undoStack().undo();
    Q_EMIT undoStackChanged();
    return result;
}

bool EditorWidget::redoLastEdit() {
    bool result = _session.undoStack().redo();
    Q_EMIT undoStackChanged();
    return result;
}

void EditorWidget::initializeSelectionSystem() {
    _session.setSelectionManager(std::make_unique<selection::SelectionManager>(*this));

    _session.selectionManager()->setSelectionCallback([this](const selection::SelectionState& selection) {
        this->clearAllVisualSelections();
        this->applySelectionVisuals(selection);
        Q_EMIT selectionChanged(selection, _session.currentElevation());
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
                    // The renderer outlines tiles from their geometry; just record the index.
                    _selectedFloorVisuals.push_back(tileIndex);
                }
                break;
            }

            case selection::SelectionType::HEX: {
                int hexIndex = item.getHexIndex();
                if (hexIndex >= 0 && hexIndex < static_cast<int>(_session.hexgrid().size())) {
                    this->_selectedHexPositions.push_back(hexIndex);
                }
                break;
            }
        }
    }
}

void EditorWidget::applyRoofTileSelectionVisual(int tileIndex) {
    // The renderer outlines roof tiles from their geometry, so even empty (transparent) tiles get
    // a boundary; just record the index.
    if (isValidTileIndex(tileIndex)) {
        _selectedRoofVisuals.push_back(tileIndex);
    }
}

void EditorWidget::refreshSelectionVisuals() {
    clearAllVisualSelections();
    applySelectionVisuals(_session.selectionManager()->getCurrentSelection());
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
    if (_session.map()) {
        loadSprites();
        showLoadingErrorsSummary();

        // Spatial index gives O(1) area selection
        if (_session.selectionManager()) {
            _session.selectionManager()->initializeSpatialIndex();
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

#ifdef GECK_SCRIPTING_ENABLED
namespace {
    // Load every *.json stamp under `dir` into `api`, keyed by each pattern's name. Appends one
    // diagnostic line per file we couldn't open or that the deserializer rejected, so a bad stamp is
    // visible in the Console output instead of being silently swallowed. A missing dir is no-op.
    void loadStampsFromDir(const QString& dir, MapScriptApi& api, QStringList& notes) {
        QDirIterator it(dir, QStringList{ "*.json" }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFile file(path);
            const QString shown = QFileInfo(path).fileName();
            if (!file.open(QIODevice::ReadOnly)) {
                notes << QStringLiteral("stamp library: could not open %1").arg(shown);
                continue;
            }
            QString error;
            if (const auto loaded = pattern::PatternSerializer::deserialize(file.readAll(), &error)) {
                const std::string name = loaded->name.empty() ? QFileInfo(file).baseName().toStdString() : loaded->name;
                api.addStamp(name, *loaded);
            } else {
                notes << QStringLiteral("stamp library: skipped %1 — %2").arg(shown, error);
            }
        }
    }

    // Register stamps a Console script can place by name — api:placeStamp("tent", ...). The CLI/MCP load
    // stamps via --stamp/the stamps arg; the Console pulls from two sources, so worked examples like
    // random_camp.luau find their tent out of the box and a user's own captures are still honoured:
    //   1. bundled examples shipped with the editor (resources/scripts/stamps/*.json);
    //   2. the user's pattern library (where "save pattern" and a captured extract land).
    // The library is scanned last so a user's saved pattern overrides a bundled example of the same name.
    std::string registerLibraryStamps(MapScriptApi& api) {
        QStringList notes;
        const QString bundled = QString::fromStdString((Application::getResourcesPath() / "scripts" / "stamps").string());
        loadStampsFromDir(bundled, api, notes);
        loadStampsFromDir(pattern::PatternLibrary::rootDir(), api, notes);
        return notes.join(QLatin1Char('\n')).toStdString();
    }
} // namespace

ScriptResult EditorWidget::runScript(const std::string& source) {
    if (!_session.map()) {
        return { false, "No map loaded", "" };
    }
    MapScriptApi api(_resources, _session.hexgrid(), *_objectCommandController, *_session.map(), _session.currentElevation());
    // so api:placeStamp(name, ...) finds the user's saved patterns; any unloadable file is reported.
    const std::string stampNotes = registerLibraryStamps(api);
    LuaScriptRuntime runtime;
    // The continuous SFML render loop shows the script's edits on the next frame.
    ScriptResult result = runtime.run(source, api, *_objectCommandController, "Run script");
    if (!stampNotes.empty()) {
        result.output = result.output.empty() ? stampNotes : stampNotes + "\n" + result.output;
    }
    if (api.mutated()) {
        // A script can reset the map (newMap) or change the header (setPlayerStart) without pushing
        // an undo command, leaving the selection and Map Info panel referencing the pre-run state and
        // the map unflagged. Drop the selection, refresh the header UI, and flag the map modified.
        if (_session.selectionManager()) {
            _session.selectionManager()->clearSelection();
        }
        _selectedHexPositions.clear();
        if (_mainWindow) {
            _mainWindow->updateMapInfo(_session.map());
        }
        Q_EMIT mapModifiedByScript();
    }
    return result;
}
#endif

bool EditorWidget::saveMap() {
    // Default the save dialog to the current map's file name.
    const QString suggestedName = _session.map() ? QString::fromStdString(_session.map()->filename()) : QString();
    QString destinationQString = QtDialogs::saveFile(this, "Save Map",
        "Map Files (*.map);;All Files (*.*)", suggestedName);

    if (destinationQString.isEmpty()) {
        return false; // user cancelled the dialog
    }

    std::string destination = destinationQString.toStdString();

    try {
        MapWriter map_writer{ [this](int32_t PID) {
            return _resources.repository().load<Pro>(ProHelper::basePath(_resources, PID));
        } };

        map_writer.openFile(destination);
        if (map_writer.write(_session.map()->getMapFile())) {
            spdlog::info("Saved map {} ({} bytes)", destination, map_writer.getBytesWritten());
            // Repoint the map at the saved file so the window title reflects the chosen name.
            _session.map()->setPath(std::filesystem::path(destination));
            return true;
        }
        spdlog::error("Failed to save map {}", destination);
        QtDialogs::showError(this, "Save Failed",
            QString("Failed to save map to:\n%1").arg(destinationQString));
    } catch (const geck::FileWriterException& e) {
        spdlog::error("Failed to save map {}: {}", destination, e.what());
        QtDialogs::showError(this, "Save Failed",
            QString("Failed to save map to:\n%1\n\n%2").arg(destinationQString, QString::fromStdString(e.what())));
    } catch (const std::exception& e) {
        spdlog::error("Unexpected error saving map {}: {}", destination, e.what());
    }
    return false;
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

    _session.setMap(std::make_unique<Map>(std::filesystem::path("newmap.map")));
    _session.map()->setMapFile(std::move(newMapFile));

    _session.setCurrentElevation(0);

    _session.objects().clear();
    _session.floorSprites().clear();
    _session.roofSprites().clear();
    _session.wallBlockerOverlays().clear();
    _selectedHexPositions.clear();

    // Load the core helper textures needed by an empty map.
    // Resource lists are bootstrapped once during startup.
    try {
        ResourceInitializer::loadEssentialTextures(_resources);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load some essential resources for new map: {}", e.what());
    }

    if (!_session.selectionManager()) {
        initializeSelectionSystem();
    }

    loadSprites();

    _session.selectionManager()->clearSelection();
    _viewportController->centerViewOnMap();

    if (_mainWindow) {
        _mainWindow->updateMapInfo(_session.map());
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

    if (!_session.hexgrid().containsPosition(topLeftHex)
        || !_session.hexgrid().containsPosition(topRightHex)
        || !_session.hexgrid().containsPosition(bottomLeftHex)
        || !_session.hexgrid().containsPosition(bottomRightHex)) {
        spdlog::warn("Rectangle contains invalid hex positions, skipping border calculation");
        return {};
    }

    auto borderHexes = _session.hexgrid().rectangleBorderPositions(topLeftHex, topRightHex, bottomLeftHex, bottomRightHex);

    spdlog::debug("Calculated {} border hexes for rectangle ({}, {}, {}, {})",
        borderHexes.size(), rectangle.position.x, rectangle.position.y, rectangle.size.x, rectangle.size.y);

    return borderHexes;
}

std::shared_ptr<MapObject> EditorWidget::createScrollBlockerObject(int hexPosition) {
    auto mapObject = std::make_shared<MapObject>();

    mapObject->position = hexPosition;
    mapObject->elevation = _session.currentElevation();
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
    if (!_session.map()) {
        return;
    }

    _mapSpriteLoader->loadTileSprites(*_session.map(), _session.currentElevation(), _session.floorSprites(), _session.roofSprites());
}

void EditorWidget::loadSprites() {
    spdlog::stopwatch sw;
    _mapSpriteLoader->loadSprites(*_session.map(), _session.currentElevation(), _session.floorSprites(), _session.roofSprites(), _session.objects(), _session.wallBlockerOverlays());

    _session.selectionManager()->initializeSpatialIndex();

    spdlog::info("Map sprites loaded in {:.3} seconds", sw);
}

bool EditorWidget::isObjectSelectable(const std::shared_ptr<Object>& object) const {
    return _objectPicker.isSelectable(object);
}

std::vector<std::shared_ptr<Object>> EditorWidget::getObjectsAtPosition(sf::Vector2f worldPos) {
    return _objectPicker.objectsAtPosition(worldPos);
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
    std::ranges::for_each(_session.objects(), [](auto& object) {
        if (object) {
            object->unselect();
            // The drag preview tints object sprites; reset the colour so no preview tint lingers.
            object->getSprite().setColor(sf::Color::White);
        }
    });

    // Tiles are outlined, so there's nothing to un-tint — just drop the tracked sets
    // (preview tints are cleared by clearDragPreview).
    _selectedFloorVisuals.clear();
    _selectedRoofVisuals.clear();
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

void EditorWidget::commitDragAreaSelection(sf::Vector2f startPos, sf::Vector2f endPos, bool isDeselect, bool isAdditive) {
    // Tear down the live preview tints first; the selection callback that follows only resets
    // tracked selection tints, so a leftover preview tint must be cleared here.
    clearDragPreview();

    const float left = std::min(startPos.x, endPos.x);
    const float top = std::min(startPos.y, endPos.y);
    const float width = std::abs(endPos.x - startPos.x);
    const float height = std::abs(endPos.y - startPos.y);
    const sf::FloatRect selectionArea({ left, top }, { width, height });

    if (_currentSelectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE) {
        createScrollBlockersFromHexes(calculateRectangleBorderHexes(selectionArea));
    } else if (isDeselect) {
        // Ctrl+drag only removes already-selected items in the area; it never adds.
        _session.selectionManager()->deselectArea(selectionArea, _currentSelectionMode, _session.currentElevation());
    } else if (isAdditive) {
        // Alt+drag adds the covered items to the selection, keeping what was already selected.
        _session.selectionManager()->addArea(selectionArea, _currentSelectionMode, _session.currentElevation());
    } else {
        const auto result = _session.selectionManager()->selectArea(selectionArea, _currentSelectionMode, _session.currentElevation());
        if (result.success) {
            spdlog::debug("Area selection completed: {}", result.message);
        }
    }
}

void EditorWidget::setupInputCallbacks() {
    if (!_inputHandler)
        return;

    InputHandler::Callbacks callbacks;
    bindSelectionCallbacks(callbacks);
    bindInteractionCallbacks(callbacks);
    bindToolModeCallbacks(callbacks);

    _inputHandler->setCallbacks(callbacks);
    _inputHandler->setSelectionMode(_currentSelectionMode);
}

void EditorWidget::bindSelectionCallbacks(InputHandler::Callbacks& callbacks) {
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
        updateDragSelectionPreview(startPos, currentPos,
            modifier == InputHandler::SelectionModifier::TOGGLE,
            modifier == InputHandler::SelectionModifier::ADD);
    };

    callbacks.onDragSelection = [this](sf::Vector2f startPos, sf::Vector2f endPos, InputHandler::SelectionModifier modifier) {
        commitDragAreaSelection(startPos, endPos,
            modifier == InputHandler::SelectionModifier::TOGGLE,
            modifier == InputHandler::SelectionModifier::ADD);
    };
}

void EditorWidget::bindInteractionCallbacks(InputHandler::Callbacks& callbacks) {
    callbacks.onTilePlacement = [this](sf::Vector2f worldPos) {
        bool isRoof = _inputHandler->isInTilePlacementMode() && sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift);
        _tilePlacementManager->handleTilePlacement(worldPos, isRoof);
        clearDragSelectionPreview(); // Clear yellow selection tint after placement
    };

    callbacks.onTileAreaFill = [this](sf::Vector2f startPos, sf::Vector2f endPos, bool isRoof) {
        _tilePlacementManager->handleTileAreaFill(startPos, endPos, isRoof);
        clearDragSelectionPreview();                   // Clear yellow selection tint after area fill
        _session.selectionManager()->clearSelection(); // Clear selection so it doesn't interfere with next tile selection
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
}

void EditorWidget::bindToolModeCallbacks(InputHandler::Callbacks& callbacks) {
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
}

void EditorWidget::createScrollBlockersFromHexes(const std::vector<int>& borderHexes) {
    if (borderHexes.empty()) {
        spdlog::warn("No valid border hexes found for scroll blocker rectangle");
        return;
    }

    int scrollBlockersCreated = 0;
    for (int hexPos : borderHexes) {
        auto scrollBlockerObject = createScrollBlockerObject(hexPos);

        _session.map()->getMapFile().map_objects[_session.currentElevation()].push_back(scrollBlockerObject);

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
                if (auto hex = _session.hexgrid().getHexByPosition(static_cast<uint32_t>(hexPos)); hex.has_value()) {
                    object->setHexPosition(hex->get());
                }
                object->setMapObject(scrollBlockerObject);
                _session.objects().push_back(object);
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
    visibility.showObjects = _session.visibility().showObjects;
    visibility.showCritters = _session.visibility().showCritters;
    visibility.showWalls = _session.visibility().showWalls;
    visibility.showRoof = _session.visibility().showRoof;
    visibility.showScrollBlockers = _session.visibility().showScrollBlockers;
    visibility.showWallBlockers = _session.visibility().showWallBlockers;
    visibility.showHexGrid = _session.visibility().showHexGrid;
    visibility.showLightOverlays = _session.visibility().showLightOverlays;
    visibility.showExitGrids = _session.visibility().showExitGrids;
    visibility.mergeSelectionOutlines = _session.visibility().mergeSelectionOutlines;

    RenderingEngine::RenderData renderData;
    renderData.floorSprites = &_session.floorSprites();
    renderData.roofSprites = &_session.roofSprites();
    renderData.objects = &_session.objects();
    renderData.wallBlockerOverlays = &_session.wallBlockerOverlays();
    renderData.selectedHexPositions = &_selectedHexPositions;
    renderData.selectedFloorTiles = &_selectedFloorVisuals;
    renderData.selectedRoofTiles = &_selectedRoofVisuals;
    renderData.dragPreviewObject = &_dragPreviewObject;
    renderData.isDraggingFromPalette = _isDraggingFromPalette;
    renderData.stampPreviewFloorTiles = &_stampPreviewFloorTiles;
    renderData.stampPreviewObjects = &_stampPreviewObjects;
    renderData.stampPreviewRoofTiles = &_stampPreviewRoofTiles;
    renderData.selectionRectangle = &_selectionRectangle;
    // Use InputHandler state for drag selection rendering
    renderData.isDragSelecting = _inputHandler && _inputHandler->isDragging();
    renderData.currentSelectionMode = _currentSelectionMode;
    renderData.hexGrid = &_session.hexgrid();
    renderData.currentHoverHex = _currentHoverHex;
    renderData.playerPositionHex = _session.map() ? static_cast<int>(_session.map()->getMapFile().header.player_default_position) : -1;
    renderData.map = _session.map();
    renderData.currentElevation = _session.currentElevation();

    _renderingEngine->render(target, _viewportController->getView(), renderData, visibility);
}

bool EditorWidget::selectAtPosition(sf::Vector2f worldPos) {
    return selectAtPosition(worldPos, SelectionModifier::NONE);
}

bool EditorWidget::selectAtPosition(sf::Vector2f worldPos, SelectionModifier modifier) {
    selection::SelectionResult result;

    switch (modifier) {
        case SelectionModifier::NONE:
            result = _session.selectionManager()->selectAtPosition(worldPos, _currentSelectionMode, _session.currentElevation());
            break;

        case SelectionModifier::ADD:
            result = _session.selectionManager()->addToSelection(worldPos, _currentSelectionMode, _session.currentElevation());
            spdlog::debug("Add to selection at ({:.1f}, {:.1f})", worldPos.x, worldPos.y);
            break;

        case SelectionModifier::TOGGLE:
            result = _session.selectionManager()->deselectAtPosition(worldPos, _currentSelectionMode, _session.currentElevation());
            spdlog::debug("Deselect at ({:.1f}, {:.1f})", worldPos.x, worldPos.y);
            break;

        case SelectionModifier::RANGE:
            result = handleRangeSelection(worldPos);
            spdlog::debug("Range selection at ({:.1f}, {:.1f})", worldPos.x, worldPos.y);
            break;
    }

    if (result.success && result.selectionChanged) {
        const auto& selection = _session.selectionManager()->getCurrentSelection();
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

    // Changing the selection type keeps the current selection; subsequent clicks/drags then add to
    // or subtract from it under the new type (the add/deselect paths are already mode-aware).
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

    // Keep the current selection across a type change (see cycleSelectionMode).
    spdlog::info("Selection mode set to: {}", selectionModeToString(_currentSelectionMode));
}

void EditorWidget::setActiveSelectionLayers(SelectionLayers layers) {
    // Combinable-layer selection is the ALL mode restricted to the chosen layers. Switch to ALL
    // and apply the layer set; the selection itself is left untouched (additive/subtractive).
    _currentSelectionMode = SelectionMode::ALL;
    if (_inputHandler) {
        _inputHandler->setSelectionMode(_currentSelectionMode);
    }
    if (!_session.selectionManager()) {
        return; // created lazily with the map; a menu sync can run first (first New Map)
    }
    _session.selectionManager()->setActiveLayers(layers);
    spdlog::info("Selection layers set to: floor={} roof={} objects={}",
        layers.floorTiles, layers.roofTiles, layers.objects);
}

SelectionLayers EditorWidget::getActiveSelectionLayers() const {
    if (!_session.selectionManager()) {
        return {}; // all layers on by default until the selection system is created
    }
    return _session.selectionManager()->activeLayers();
}

void EditorWidget::toggleScrollBlockerRectangleMode() {
    if (_currentSelectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE) {
        _currentSelectionMode = SelectionMode::ALL;
        spdlog::info("Scroll blocker rectangle mode disabled, switched to ALL mode");
    } else {
        _currentSelectionMode = SelectionMode::SCROLL_BLOCKER_RECTANGLE;
        // Auto-enable scroll blocker visibility so the user can see what they are placing
        if (!_session.visibility().showScrollBlockers) {
            _session.visibility().showScrollBlockers = true;
            spdlog::info("Automatically enabled scroll blocker visibility");
        }
        spdlog::info("Scroll blocker rectangle mode enabled");
    }

    if (_inputHandler) {
        _inputHandler->setSelectionMode(_currentSelectionMode);
    }

    _session.selectionManager()->clearSelection();
}

void EditorWidget::rotateSelectedObject() {
    const auto& selection = _session.selectionManager()->getCurrentSelection();
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
        _session.setCurrentElevation(elevation);
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
    if (_session.selectionManager()) {
        _session.selectionManager()->selectAll(_currentSelectionMode, _session.currentElevation());
    }
}

void EditorWidget::clearSelection() {
    if (_session.selectionManager()) {
        _session.selectionManager()->clearSelection();
    }
}

void EditorWidget::setMode(EditorMode mode, int tileIndex, bool isRoof) {
    _mode = mode;

    // Single owner of mutual exclusion: deactivate every mode's state across all
    // components, then activate the target.
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
                // Without this the input handler never sees placement clicks.
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
    if (!_stampPattern || !_session.map() || _stampPattern->variants.empty()) {
        return;
    }
    const int hex = _viewportController->worldPosToHexIndex(worldPos);
    if (!_session.hexgrid().containsPosition(hex)) {
        return;
    }
    if (_stampVariantIndex < 0 || _stampVariantIndex >= static_cast<int>(_stampPattern->variants.size())) {
        _stampVariantIndex = 0;
    }
    const pattern::PatternVariant& variant = _stampPattern->variants[_stampVariantIndex];

    pattern::PatternStamper stamper(_resources, _session.hexgrid(), *_objectCommandController, *_session.map());
    const pattern::PatternStamper::Result result = stamper.stamp(variant, hex, _session.currentElevation());

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
    if (!_session.hexgrid().containsPosition(hex)) {
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

    const auto ghostAlpha = sf::Color(255, 255, 255, ui::constants::sfml::DRAG_PREVIEW_ALPHA);
    for (const pattern::PatternStamper::TilePlacement& tp : plan.tiles) {
        if (auto sprite = pattern::buildTileSprite(_resources, tp.tileIndex, tp.isRoof, tp.tileId)) {
            sprite->setColor(ghostAlpha);
            (tp.isRoof ? _stampPreviewRoofTiles : _stampPreviewFloorTiles).push_back(std::move(*sprite));
        }
    }

    for (const pattern::PatternStamper::ObjectPlacement& op : plan.objects) {
        auto object = pattern::buildSpriteObject(_resources, _session.hexgrid(), op.frmPid, op.hex, op.direction);
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
    if (!_session.map()) {
        return;
    }

    _mapSpriteLoader->loadObjectSprites(*_session.map(), _session.currentElevation(), _session.objects(), _session.wallBlockerOverlays());

    spdlog::debug("Refreshed objects for current elevation");
}

void EditorWidget::updateTileSprite(int hexIndex, bool isRoof) {
    updateTileSprite(hexIndex, isRoof, _session.currentElevation());
}

void EditorWidget::updateTileSprite(int hexIndex, bool isRoof, int elevation) {
    if (!_session.map() || !isValidHexPosition(hexIndex)) {
        return;
    }

    const auto& elevationTiles = ensureElevationTiles(elevation);
    _mapSpriteLoader->updateTileSprite(hexIndex, isRoof, elevation, elevationTiles, _session.floorSprites(), _session.roofSprites());
}

bool EditorWidget::isSpriteClicked(sf::Vector2f worldPos, const sf::Sprite& sprite) {
    return sprite.getGlobalBounds().contains(worldPos);
}

std::optional<int> EditorWidget::getTileAtPosition(sf::Vector2f worldPos, bool isRoof) {
    if (!_session.map() || _session.map()->getMapFile().tiles.find(_session.currentElevation()) == _session.map()->getMapFile().tiles.end()) {
        return std::nullopt;
    }

    // Resolve by nearest tile centre (the diamond actually under the cursor) instead of
    // snapping the click to a hex and converting hex->tile, which is imprecise at boundaries.
    const auto tileIndex = screenToTileIndex(worldPos.x, worldPos.y, isRoof);
    if (!tileIndex.has_value()) {
        return std::nullopt;
    }

    // Editor-specific guard: a roof selection only counts on a non-empty roof tile.
    if (isRoof && _session.map()->getMapFile().tiles.at(_session.currentElevation()).at(*tileIndex).getRoof() == Map::EMPTY_TILE) {
        spdlog::debug("EditorWidget::getTileAtPosition: Empty roof tile at index {} [worldPos: ({:.1f}, {:.1f})]",
            *tileIndex, worldPos.x, worldPos.y);
        return std::nullopt;
    }

    return *tileIndex;
}

std::optional<int> EditorWidget::getRoofTileAtPositionIncludingEmpty(sf::Vector2f worldPos) {
    // Includes empty roof tiles in the selection. Resolves by nearest roof-tile centre
    // (screenToTileIndex applies the roof offset) for accuracy at tile boundaries.
    if (!_session.map() || _session.map()->getMapFile().tiles.find(_session.currentElevation()) == _session.map()->getMapFile().tiles.end()) {
        return std::nullopt;
    }

    return screenToTileIndex(worldPos.x, worldPos.y, true);
}

selection::SelectionResult EditorWidget::handleRangeSelection(sf::Vector2f worldPos) {
    // Range selection is primarily for tiles. It needs a starting point, so if
    // nothing is selected, fall back to a normal single selection.
    const auto& currentSelection = _session.selectionManager()->getCurrentSelection();

    if (currentSelection.isEmpty()) {
        return _session.selectionManager()->selectAtPosition(worldPos, _currentSelectionMode, _session.currentElevation());
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
        return _session.selectionManager()->selectAtPosition(worldPos, _currentSelectionMode, _session.currentElevation());
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

    auto result = _session.selectionManager()->selectArea(selectionArea, areaMode, _session.currentElevation());

    spdlog::info("Range selection: area ({:.1f}, {:.1f}, {:.1f}, {:.1f})",
        selectionArea.position.x, selectionArea.position.y, selectionArea.size.x, selectionArea.size.y);

    return result;
}

void EditorWidget::updateDragSelectionPreview(sf::Vector2f startWorldPos, sf::Vector2f currentWorldPos, bool isDeselect, bool isAdditive) {
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
        auto toRemove = _session.selectionManager()->itemsToDeselectInArea(selectionArea, _currentSelectionMode, _session.currentElevation());
        selection::SelectionState preview = _session.selectionManager()->getCurrentSelection();
        for (const auto& item : toRemove) {
            preview.removeItem(item);
        }
        clearAllVisualSelections();
        applySelectionVisuals(preview);
        return;
    }

    if (isAdditive) {
        // Alt+drag adds to the selection, so keep what is already selected highlighted while the
        // covered area is tinted below to preview the addition.
        refreshSelectionVisuals();
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
            // Match collectItemsInArea: in ALL mode a hidden roof is not selectable, so do
            // not preview-tint roof tiles the commit would skip.
            if (isRoofVisible()) {
                previewAreaTiles(selectionArea, true, false);
            }
            previewAreaObjects(selectionArea);
            break;

        default:
            break;
    }
}

void EditorWidget::previewAreaTiles(const sf::FloatRect& area, bool roof, bool includeEmpty) {
    auto tiles = includeEmpty ? _session.selectionManager()->getTilesInAreaIncludingEmpty(area, roof, _session.currentElevation())
                              : _session.selectionManager()->getTilesInArea(area, roof, _session.currentElevation());
    auto& sprites = roof ? _session.roofSprites() : _session.floorSprites();
    for (int tileIndex : tiles) {
        if (isValidTileIndex(tileIndex)) {
            applyPreviewHighlight(sprites.at(tileIndex)); // also makes empty (transparent) roof tiles visible
        }
    }
    _previewTiles.insert(_previewTiles.end(), tiles.begin(), tiles.end());
}

void EditorWidget::previewAreaObjects(const sf::FloatRect& area) {
    auto objects = _session.selectionManager()->getObjectsInArea(area, _session.currentElevation());
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
    _previewTiles = _session.selectionManager()->getTilesInArea(selectionArea, isRoof, _session.currentElevation());

    auto& sprites = isRoof ? _session.roofSprites() : _session.floorSprites();
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
            removePreviewHighlight(_session.floorSprites().at(tileIndex));

            // Empty roof sprites must go back to transparent, not white
            if (_session.map() && _session.map()->getMapFile().tiles.find(_session.currentElevation()) != _session.map()->getMapFile().tiles.end()) {
                auto tile = _session.map()->getMapFile().tiles.at(_session.currentElevation()).at(tileIndex);
                if (tile.getRoof() == Map::EMPTY_TILE) {
                    _session.roofSprites().at(tileIndex).setColor(geck::TileColors::transparent());
                } else {
                    removePreviewHighlight(_session.roofSprites().at(tileIndex));
                }
            } else {
                removePreviewHighlight(_session.roofSprites().at(tileIndex));
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
    auto& mapFile = _session.map()->getMapFile();
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
    for (auto& object : _session.objects()) {
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
    if (!_session.map()) {
        spdlog::warn("EditorWidget: Cannot place object - no map loaded");
        return;
    }

    int hexPosition = _viewportController->worldPosToHexIndex(worldPos);
    if (!_session.hexgrid().containsPosition(hexPosition)) {
        spdlog::warn("EditorWidget: Invalid hex position {} for object placement", hexPosition);
        return;
    }

    auto mapObject = std::make_shared<MapObject>();

    mapObject->position = hexPosition;
    mapObject->elevation = _session.currentElevation();
    mapObject->direction = 0;
    mapObject->frame_number = 0;

    auto hexCoords = _session.hexgrid().getHexByPosition(static_cast<uint32_t>(hexPosition));
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

        if (auto hex = _session.hexgrid().getHexByPosition(static_cast<uint32_t>(hexPosition)); hex.has_value()) {
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
    if (!_session.map()) {
        spdlog::warn("EditorWidget::centerViewOnPlayerPosition: No map loaded");
        return;
    }

    uint32_t playerHexPosition = _session.map()->getMapFile().header.player_default_position;

    auto hex = _session.hexgrid().getHexByPosition(playerHexPosition);
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

void EditorWidget::setSelectionColors(const RenderingEngine::SelectionPalette& colors) {
    if (_renderingEngine) {
        _renderingEngine->setSelectionColors(colors);
    }
}

void EditorWidget::setShowLightOverlays(bool show) {
    _session.visibility().showLightOverlays = show;

    int lightObjectCount = 0;
    std::ranges::for_each(_session.objects(), [&lightObjectCount, show](auto& object) {
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
    if (!_session.map()) {
        return;
    }

    auto& selectionState = _session.selectionManager()->getCurrentSelection();
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

    _session.selectionManager()->clearSelection();

    Q_EMIT selectionChanged(_session.selectionManager()->getCurrentSelection(), _session.currentElevation());

    spdlog::info("EditorWidget::deleteSelectedObjects - Successfully deleted {} objects", removedObjects.size());
}

} // namespace geck
