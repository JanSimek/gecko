#include "EditorWidget.h"
#include "ui/core/EditorHints.h"
#include "ui/widgets/SFMLWidget.h"
#include "ui/input/InputHandler.h"
// The stamp library + MapScriptApi are used by both the (scripting-only) Console and the always-on
// area fill, so these are included unconditionally — only runScript itself stays #ifdef'd.
#include "Application.h"
#include "pattern/PatternLibrary.h"
#include "pattern/PatternSerializer.h"
#include "scripting/MapScriptApi.h"
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include "editing/commands/ObjectCommandController.h"
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
#include "resource/ResourceInitializer.h"
#include "util/TileUtils.h"
#include "ui/QtDialogs.h"
#include "util/ProHelper.h"
#include "util/Coordinates.h"

#include "editor/Object.h"
#include "pattern/PatternStamper.h"
#include "pattern/PatternSprite.h"
#include "editor/HexagonGrid.h"

// Area fill commits its plan through PlacementBatch (MapScriptApi is already included above, used by
// both the scripting console and the always-on area fill; only the Luau producer is #ifdef'd).
#include "pattern/PlacementBatch.h"

#include "format/frm/Frm.h"
#include "format/map/Tile.h"
#include "format/pro/Pro.h"
#include "format/map/MapObject.h"
#include "resource/GameResources.h"

#include "state/MapSaveService.h"
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

    _controller.initEditingCore(_resources,
        EditorController::EditingCoreCallbacks{
            .refreshObjects = [this]() { refreshObjects(); },
            .undoStackChanged = [this]() { Q_EMIT undoStackChanged(); },
            .ensureElevationTiles = [this](int elevation) -> std::vector<Tile>& { return ensureElevationTiles(elevation); },
            .currentElevation = [this]() { return _session.currentElevation(); },
            .updateTileSprite = [this](int hexIndex, bool isRoof, int elevation) { updateTileSprite(hexIndex, isRoof, elevation); },
            .loadTileSprites = [this]() { loadTileSprites(); },
        });
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
        _resources,
        [this](const QString& m) {
            if (auto* mw = getMainWindow())
                mw->showStatusMessage(m);
        });
    setupInputCallbacks();

    setupUI();
    _controller.viewport().centerViewOnMap();
}

// Tile-edit command logic lives in ObjectCommandController (the single command
// owner); these are thin TilePlacementContext delegators. The controller invokes
// ensureElevationTiles/updateTileSprite/getCurrentElevation back through callbacks.
void EditorWidget::applyTileChanges(const std::vector<TileChange>& changes, bool applyAfterState) {
    _controller.commandController().applyTileChanges(changes, applyAfterState);
}

void EditorWidget::registerTileEdit(const QString& description, const std::vector<TileChange>& changes) {
    _controller.commandController().registerTileEdit(description.toStdString(), changes);
}

void EditorWidget::addPlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    _controller.commandController().addPlacedObject(mapObject, object);
}

void EditorWidget::removePlacedObject(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    _controller.commandController().removePlacedObject(mapObject, object);
}

// The register*() helpers below forward to the controller, which emits undoStackChanged
// through its onStackChanged callback (wired in the constructor).
void EditorWidget::registerObjectPlacement(const std::shared_ptr<MapObject>& mapObject, const std::shared_ptr<Object>& object) {
    _controller.commandController().registerObjectPlacement(mapObject, object);
}

void EditorWidget::registerObjectMove(const std::vector<std::shared_ptr<Object>>& objects,
    const std::vector<std::pair<int, int>>& moves) {
    _controller.commandController().registerObjectMove(objects, moves);
}

void EditorWidget::moveSelectedTilesForDrag(sf::Vector2f worldTranslation) {
    if (!_session.selectionManager()) {
        return;
    }
    const auto changes = _session.selectionManager()->planSelectionMoveForTranslation(worldTranslation);
    _controller.commandController().applyTileEdit("Move Tiles", changes);
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
    _controller.commandController().beginBatch(description);
}

void EditorWidget::endMoveBatch() {
    _controller.commandController().endBatch();
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
    capture(false, _controller.visualizer().floorVisuals(), _session.floorSprites());
    if (_session.visibility().showRoof) {
        capture(true, _controller.visualizer().roofVisuals(), _session.roofSprites());
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
    _controller.commandController().registerObjectRotation(objects, beforeDirs, afterDirs);
}

void EditorWidget::applyFrmToObject(const std::shared_ptr<Object>& object, uint32_t frmPid, const std::string& frmPath) {
    _controller.commandController().applyFrmToObject(object, frmPid, frmPath);
}

void EditorWidget::registerObjectFrmChange(const std::shared_ptr<Object>& object, uint32_t oldFrmPid, const std::string& oldFrmPath, uint32_t newFrmPid, const std::string& newFrmPath) {
    _controller.commandController().registerObjectFrmChange(object, oldFrmPid, oldFrmPath, newFrmPid, newFrmPath);
}

void EditorWidget::registerExitGridCreation(const std::vector<std::shared_ptr<MapObject>>& exitGrids, int elevation) {
    _controller.commandController().registerExitGridCreation(exitGrids, elevation);
}

void EditorWidget::registerExitGridEdit(const std::vector<std::shared_ptr<MapObject>>& exitGrids,
    const std::vector<ExitGridState>& beforeStates,
    const std::vector<ExitGridState>& afterStates) {
    _controller.commandController().registerExitGridEdit(exitGrids, beforeStates, afterStates);
}

void EditorWidget::registerInstanceEdit(const std::shared_ptr<MapObject>& mapObject,
    const MapObjectInstanceState& before,
    const MapObjectInstanceState& after,
    const std::string& description) {
    _controller.commandController().registerInstanceEdit(mapObject, before, after, description);
}

void EditorWidget::clearElevationObjects(int elevation) {
    _controller.commandController().clearElevationObjects(elevation);
}

void EditorWidget::copyElevation(int fromElevation, int toElevation) {
    _controller.commandController().copyElevation(fromElevation, toElevation);
}

void EditorWidget::registerInventoryEdit(const std::shared_ptr<MapObject>& container,
    std::vector<std::shared_ptr<MapObject>> before,
    std::vector<std::shared_ptr<MapObject>> after) {
    _controller.commandController().registerInventoryEdit(container, std::move(before), std::move(after));
}

void EditorWidget::attachScript(const std::shared_ptr<MapObject>& object, int scriptType, uint32_t programIndex) {
    _controller.commandController().attachScript(object, scriptType, programIndex);
}

void EditorWidget::detachScript(const std::shared_ptr<MapObject>& object) {
    _controller.commandController().detachScript(object);
}

void EditorWidget::addSpatialScript(uint32_t programIndex, int tile, int elevation, int radius) {
    _controller.commandController().addSpatialScript(programIndex, tile, elevation, radius);
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
        _controller.visualizer().clear();
        _controller.visualizer().apply(selection);
        Q_EMIT selectionChanged(selection, _session.currentElevation());
        // The hint depends on whether anything is selected (e.g. Select mode shows R/Delete
        // only with a selection), so refresh it whenever the selection changes.
        Q_EMIT hintChanged(hintForContext(_mode, !selection.isEmpty()));
    });
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
    _controller.viewport().initialize(windowSize);
}

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

#ifdef GECK_SCRIPTING_ENABLED
ScriptResult EditorWidget::runScript(const std::string& source) {
    if (!_session.map()) {
        return { false, "No map loaded", "" };
    }
    MapScriptApi api(_resources, _session.hexgrid(), _controller.commandController(), *_session.map(), _session.currentElevation());
    // so api:placeStamp(name, ...) finds the user's saved patterns; any unloadable file is reported.
    const std::string stampNotes = registerLibraryStamps(api);
    LuaScriptRuntime runtime;
    // The continuous SFML render loop shows the script's edits on the next frame.
    ScriptResult result = runtime.run(source, api, _controller.commandController(), "Run script");
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
        _controller.visualizer().clearHexPositions();
        if (_mainWindow) {
            _mainWindow->updateMapInfo(_session.map());
        }
        Q_EMIT mapModifiedByScript();
    }
    return result;
}
#endif

bool EditorWidget::canSaveInPlace(const std::filesystem::path& mapPath) {
    // Save-in-place is only valid when the map already lives at a real, existing on-disk file (opened
    // from disk, or saved here before). A map opened from the game data carries a VFS path such as
    // "/maps/arbridge.map" — which looks absolute but is not a writable filesystem location (writing it
    // would try to create "/maps" at the filesystem root) — and a brand-new map has no file yet. Both of
    // those must go to "Save As" instead, which defaults into the writable data path.
    std::error_code ec;
    return mapPath.is_absolute() && std::filesystem::exists(mapPath, ec);
}

bool EditorWidget::saveMap(const std::filesystem::path& defaultDir) {
    if (_session.map() && canSaveInPlace(_session.map()->path())) {
        return writeMapTo(_session.map()->path().string());
    }
    return saveMapAs(defaultDir);
}

bool EditorWidget::saveMapAs(const std::filesystem::path& defaultDir) {
    if (!_session.map()) {
        return false;
    }

    // Default the dialog to <writable data path>/maps/<filename> when a writable location exists;
    // otherwise just the file name (the dialog then seeds its last-used directory). Passing an absolute
    // suggestion deliberately overrides that last-used directory.
    const std::string filename = _session.map()->filename();
    const QString suggested = defaultDir.empty()
        ? QString::fromStdString(filename)
        : QString::fromStdString((defaultDir / filename).string());

    const QString destinationQString = QtDialogs::saveFile(this, "Save Map As",
        "Map Files (*.map);;All Files (*.*)", suggested);
    if (destinationQString.isEmpty()) {
        return false; // user cancelled the dialog
    }
    return writeMapTo(destinationQString.toStdString());
}

bool EditorWidget::writeMapTo(const std::string& destination) {
    try {
        if (const auto bytesWritten = saveMapToFile(_resources, _session.map()->getMapFile(), destination);
            bytesWritten.has_value()) {
            spdlog::info("Saved map {} ({} bytes)", destination, *bytesWritten);
            // Repoint the map at the saved file so the title reflects the name and the next Save targets it.
            _session.map()->setPath(std::filesystem::path(destination));
            return true;
        }
        spdlog::error("Failed to save map {}", destination);
        QtDialogs::showError(this, "Save Failed",
            QString("Failed to save map to:\n%1").arg(QString::fromStdString(destination)));
    } catch (const geck::FileWriterException& e) {
        spdlog::error("Failed to save map {}: {}", destination, e.what());
        QtDialogs::showError(this, "Save Failed",
            QString("Failed to save map to:\n%1\n\n%2").arg(QString::fromStdString(destination), QString::fromStdString(e.what())));
    } catch (const std::exception& e) {
        spdlog::error("Unexpected error saving map {}: {}", destination, e.what());
    }
    return false;
}

void EditorWidget::openMap() {
    QString mapPathQString = QtDialogs::openMapFile(this, "Choose Fallout 2 map to load");

    if (mapPathQString.isEmpty()) {
        spdlog::debug("No map file selected");
        return;
    }

    std::string mapPath = mapPathQString.toStdString();
    spdlog::debug("User requested to open new map: {}", mapPath);

    // MainWindow handles the actual loading
    Q_EMIT mapLoadRequested(mapPath);
}

void EditorWidget::createNewMap() {
    spdlog::debug("Creating new empty map");

    _session.resetToEmptyMap();
    _controller.visualizer().clearHexPositions();

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
    _controller.viewport().centerViewOnMap();

    if (_mainWindow) {
        _mainWindow->updateMapInfo(_session.map());
    }

    spdlog::debug("Created new empty map with {} tiles per elevation", Map::TILES_PER_ELEVATION);
}

std::vector<int> EditorWidget::calculateRectangleBorderHexes(sf::FloatRect rectangle) {
    sf::Vector2f topLeft = sf::Vector2f(rectangle.position.x, rectangle.position.y);
    sf::Vector2f topRight = sf::Vector2f(rectangle.position.x + rectangle.size.x, rectangle.position.y);
    sf::Vector2f bottomLeft = sf::Vector2f(rectangle.position.x, rectangle.position.y + rectangle.size.y);
    sf::Vector2f bottomRight = sf::Vector2f(rectangle.position.x + rectangle.size.x, rectangle.position.y + rectangle.size.y);

    int topLeftHex = _controller.viewport().worldPosToHexIndex(topLeft);
    int topRightHex = _controller.viewport().worldPosToHexIndex(topRight);
    int bottomLeftHex = _controller.viewport().worldPosToHexIndex(bottomLeft);
    int bottomRightHex = _controller.viewport().worldPosToHexIndex(bottomRight);

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

    _controller.spriteLoader().loadTileSprites(*_session.map(), _session.currentElevation(), _session.floorSprites(), _session.roofSprites());
}

void EditorWidget::loadSprites() {
    spdlog::stopwatch sw;
    _controller.spriteLoader().loadSprites(*_session.map(), _session.currentElevation(), _session.floorSprites(), _session.roofSprites(), _session.objects(), _session.wallBlockerOverlays());

    _session.selectionManager()->initializeSpatialIndex();

    spdlog::debug("Map sprites loaded in {:.3} seconds", sw);
}

bool EditorWidget::isObjectSelectable(const std::shared_ptr<Object>& object) const {
    return _controller.picker().isSelectable(object);
}

std::vector<std::shared_ptr<Object>> EditorWidget::getObjectsAtPosition(sf::Vector2f worldPos) {
    return _controller.picker().objectsAtPosition(worldPos);
}

bool EditorWidget::isDoubleClick(sf::Vector2f worldPos) {
    float timeSinceLastClick = _lastClickTime.getElapsedTime().asSeconds();
    float distance = std::sqrt(std::pow(worldPos.x - _lastClickPosition.x, 2) + std::pow(worldPos.y - _lastClickPosition.y, 2));

    bool isDouble = (timeSinceLastClick < DOUBLE_CLICK_TIME) && (distance < DOUBLE_CLICK_DISTANCE);

    _lastClickTime.restart();
    _lastClickPosition = worldPos;

    return isDouble;
}

// SFML event handling interface (called by SFMLWidget)
void EditorWidget::handleEvent(const sf::Event& event) {
    if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        _controller.viewport().updateViewForWindowSize(sf::Vector2u(resized->size.x, resized->size.y));
        spdlog::debug("EditorWidget: Handled window resize to {}x{}", resized->size.x, resized->size.y);
    }

    if (_inputHandler && _sfmlWidget) {
        if (auto* target = _sfmlWidget->getRenderTarget()) {
            _inputHandler->handleEvent(event, *target, _controller.viewport().getView());
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
        sf::Vector2f center = _controller.viewport().getView().getCenter();
        _controller.viewport().getView().setCenter(center + delta);
    };

    callbacks.onZoom = [this](float direction) {
        _controller.viewport().zoomView(direction);
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
        int hexPosition = _controller.viewport().worldPosToHexIndex(worldPos);
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
        spdlog::debug("Tile placement mode cancelled");
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

    callbacks.onMarkExitsLinePreview = [this](const std::vector<sf::Vector2f>& vertices, sf::Vector2f cursor, bool flipSide) {
        updateMarkExitsLinePreview(vertices, cursor, flipSide);
    };

    callbacks.onMarkExitsLine = [this](const std::vector<sf::Vector2f>&, bool) {
        // The placed edge is the manager's FROZEN committed segments (the vertices/flip args are
        // vestigial now that the manager owns the per-segment art); it resets them after placing.
        clearMarkExitsLinePreview();
        _exitGridPlacementManager->selectExitGridsAlongLine();
    };

    // True-freeze hooks: a reset starts a fresh edge; a commit (one per closing click) freezes that
    // segment with the flip at the click.
    callbacks.onMarkExitsLineReset = [this]() {
        _exitGridPlacementManager->beginLine();
    };

    callbacks.onMarkExitsSegmentCommitted = [this](sf::Vector2f from, sf::Vector2f to, bool flipSide) {
        _exitGridPlacementManager->commitSegment(from, to, flipSide);
    };

    callbacks.onMouseMove = [this](sf::Vector2f worldPos) {
        _currentHoverHex = _controller.viewport().updateHoverHex(worldPos);
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
        clearMarkExitsLinePreview();
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

    spdlog::debug("Scroll blocker rectangle: {} scroll blockers created on border", scrollBlockersCreated);
}

void EditorWidget::update([[maybe_unused]] const float dt) {
    // Called by the SFMLWidget's update loop
}

void EditorWidget::render(sf::RenderTarget& target, [[maybe_unused]] const float dt) {
    // Called by the SFMLWidget's render loop
    if (!_controller.hasRenderingEngine()) {
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
    renderData.selectedHexPositions = &_controller.visualizer().hexPositions();
    renderData.selectedFloorTiles = &_controller.visualizer().floorVisuals();
    renderData.selectedRoofTiles = &_controller.visualizer().roofVisuals();
    renderData.dragPreviewObject = &_dragPreviewObject;
    renderData.isDraggingFromPalette = _isDraggingFromPalette;
    // The ghost-overlay field is shared: a fill preview and a stamp ghost are never live at once, so
    // point it at whichever is active (A3 reuses the stamp preview's render path rather than adding a
    // second field).
    if (_fillPreviewActive) {
        renderData.stampPreview.floorTiles = &_fillPreviewFloorTiles;
        renderData.stampPreview.objects = &_fillPreviewObjects;
        renderData.stampPreview.roofTiles = &_fillPreviewRoofTiles;
    } else {
        renderData.stampPreview.floorTiles = &_stampPreviewFloorTiles;
        renderData.stampPreview.objects = &_stampPreviewObjects;
        renderData.stampPreview.roofTiles = &_stampPreviewRoofTiles;
    }
    renderData.selectionRectangle = &_selectionRectangle;
    // Use InputHandler state for drag selection rendering
    renderData.isDragSelecting = _inputHandler && _inputHandler->isDragging();
    renderData.currentSelectionMode = _currentSelectionMode;
    renderData.hexGrid = &_session.hexgrid();
    renderData.currentHoverHex = _currentHoverHex;
    renderData.playerPositionHex = _session.map() ? static_cast<int>(_session.map()->getMapFile().header.player_default_position) : -1;
    renderData.map = _session.map();
    renderData.currentElevation = _session.currentElevation();

    // Exit-grid "Draw edge" live preview (MarkExits mode).
    renderData.exitGridPreview.active = _exitGridLineActive;
    renderData.exitGridPreview.lineVertices = &_exitGridLineVertices;
    renderData.exitGridPreview.lineCursor = _exitGridLineCursor;
    renderData.exitGridPreview.hexes = &_exitGridPreviewHexes;
    renderData.exitGridPreview.frmPids = &_exitGridPreviewFrmPids;
    renderData.exitGridPreview.tint = _exitGridPreviewTint;

    _controller.renderingEngine().render(target, _controller.viewport().getView(), renderData, visibility);
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
            spdlog::debug("Multi-selection: {} items selected", selection.count());
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
    spdlog::debug("Selection mode changed to: {}", selectionModeToString(_currentSelectionMode));
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
    spdlog::debug("Selection mode set to: {}", selectionModeToString(_currentSelectionMode));
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
    spdlog::debug("Selection layers set to: floor={} roof={} objects={}",
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
        spdlog::debug("Scroll blocker rectangle mode disabled, switched to ALL mode");
    } else {
        _currentSelectionMode = SelectionMode::SCROLL_BLOCKER_RECTANGLE;
        // Auto-enable scroll blocker visibility so the user can see what they are placing
        if (!_session.visibility().showScrollBlockers) {
            _session.visibility().showScrollBlockers = true;
            spdlog::debug("Automatically enabled scroll blocker visibility");
        }
        spdlog::debug("Scroll blocker rectangle mode enabled");
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
        spdlog::debug("Rotated {} selected object(s)", objects.size());
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
    if (mode != EditorMode::MarkExits) {
        clearMarkExitsLinePreview();
        // Leaving "Draw edge" abandons any in-progress line: drop its frozen segments. (Finalize/cancel
        // reset via onMarkExitsLineReset and keep the tool active, so they don't reach here.)
        _exitGridPlacementManager->resetLine();
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
    emitHintChanged();
}

void EditorWidget::emitHintChanged() {
    const auto* manager = _session.selectionManager();
    const bool hasSelection = manager && !manager->getCurrentSelection().isEmpty();
    Q_EMIT hintChanged(hintForContext(_mode, hasSelection));
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
    const int hex = _controller.viewport().worldPosToHexIndex(worldPos);
    if (!_session.hexgrid().containsPosition(hex)) {
        return;
    }
    if (_stampVariantIndex < 0 || _stampVariantIndex >= static_cast<int>(_stampPattern->variants.size())) {
        _stampVariantIndex = 0;
    }
    const pattern::PatternVariant& variant = _stampPattern->variants[_stampVariantIndex];

    pattern::PatternStamper stamper(_resources, _session.hexgrid(), _controller.commandController(), *_session.map());
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
    const int hex = _controller.viewport().worldPosToHexIndex(worldPos);
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

// ---- Area fill over the current selection (driven by a Luau fill script) ------------------------

namespace {
    // The bounding hexes covering a set of selected floor/roof tiles, derived from their on-screen sprite
    // bounds — so a floor/area selection (which carries no hexes) can still scatter objects over the same
    // region. Exact for a rectangular drag; a non-rectangular selection over-includes its bounding box
    // (acceptable for a scatter region). Empty when no selected tile has drawable bounds.
    std::vector<int> hexesCoveringTiles(const std::vector<int>& floorTiles, const std::vector<int>& roofTiles,
        const std::vector<sf::Sprite>& floorSprites, const std::vector<sf::Sprite>& roofSprites,
        selection::SelectionManager& selectionManager) {
        // Sentinel-initialised so each contributing tile just min/max-folds in (no first-vs-rest branch).
        constexpr float INF = std::numeric_limits<float>::max();
        float minX = INF;
        float minY = INF;
        float maxX = -INF;
        float maxY = -INF;
        bool any = false;
        const auto extend = [&](const std::vector<sf::Sprite>& sprites, int index) {
            if (index < 0 || index >= static_cast<int>(sprites.size())) {
                return;
            }
            const sf::FloatRect b = sprites[index].getGlobalBounds();
            if (b.size.x <= 0.f || b.size.y <= 0.f) {
                return; // empty tile: no sprite bounds to contribute
            }
            minX = std::min(minX, b.position.x);
            minY = std::min(minY, b.position.y);
            maxX = std::max(maxX, b.position.x + b.size.x);
            maxY = std::max(maxY, b.position.y + b.size.y);
            any = true;
        };
        for (const int tileIndex : floorTiles) {
            extend(floorSprites, tileIndex);
        }
        for (const int tileIndex : roofTiles) {
            extend(roofSprites, tileIndex);
        }
        if (!any) {
            return {};
        }
        const sf::FloatRect bounds(sf::Vector2f(minX, minY), sf::Vector2f(maxX - minX, maxY - minY));
        return selectionManager.getHexesInArea(bounds);
    }
} // namespace

EditArea EditorWidget::selectionFillArea() const {
    EditArea area;
    auto* selectionManager = _session.selectionManager();
    if (!selectionManager) {
        return area;
    }
    const selection::SelectionState& state = selectionManager->getCurrentSelection();
    area.floorTiles = state.getFloorTileIndices();
    area.roofTiles = state.getRoofTileIndices();
    area.hexes = state.getHexIndices();

    // A tile selection carries no hexes, but object scatter iterates the area's hexes. Derive the
    // covering hexes from the selected tiles' on-screen bounds so a floor/area selection can scatter
    // objects over the same region.
    if (area.hexes.empty() && (!area.floorTiles.empty() || !area.roofTiles.empty())) {
        area.hexes = hexesCoveringTiles(area.floorTiles, area.roofTiles,
            _session.floorSprites(), _session.roofSprites(), *selectionManager);
    }

    // EditArea's contract: each list ascending AND unique (areaContains* binary-searches; the seeded
    // fill iterates in this canonical order, so the same selection + seed reproduces the same result,
    // and a duplicated index would be painted/counted twice).
    const auto sortUnique = [](std::vector<int>& v) {
        std::ranges::sort(v);
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    sortUnique(area.floorTiles);
    sortUnique(area.roofTiles);
    sortUnique(area.hexes);
    return area;
}

bool EditorWidget::hasFillableSelection() const {
    auto* selectionManager = _session.selectionManager();
    if (!selectionManager) {
        return false;
    }
    // Any tile or hex makes the selection fillable; a pure-object selection does not (a floor fill
    // needs tiles, scatter needs hexes — objects are neither).
    using enum selection::SelectionType;
    for (const auto& item : selectionManager->getCurrentSelection().items) {
        if (item.type == FLOOR_TILE || item.type == ROOF_TILE || item.type == HEX) {
            return true;
        }
    }
    return false;
}

#ifdef GECK_SCRIPTING_ENABLED
ScriptResult EditorWidget::previewLuaFill(const EditArea& area, const std::string& source, uint32_t seed) {
    // A generous wall-clock cap for a live preview: long enough for a real procedural fill over a
    // large selection, short enough that an accidental infinite loop aborts instead of hanging the UI.
    constexpr unsigned FILL_SCRIPT_BUDGET_MS = 3000;

    clearFillPreview();
    if (!_session.map()) {
        return { false, "No map loaded", "" };
    }
    // Record into the plan sink instead of committing: the script's api:paintFloor/scatter/placeStamp
    // calls buffer into _fillPlan while the live map stays untouched, so the result previews as a
    // ghost and applyFillPreview() replays it as one undo entry. setArea exposes the selection to
    // api:areaFloorTiles()/areaHexes(),
    // and setSeed makes api:rng()/rngInt() reproducible from the dialog's seed.
    MapScriptApi api(_resources, _session.hexgrid(), _controller.commandController(), *_session.map(), _session.currentElevation());
    registerLibraryStamps(api);
    api.setArea(&area);
    api.setPlanSink(&_fillPlan);
    api.setSeed(seed);

    LuaScriptRuntime runtime;
    ScriptArgs args;
    args["seed"] = std::to_string(seed); // also reproducible via math.random / args.seed
    ScriptResult result = runtime.run(
        source, api, _controller.commandController(), "Fill (script)", args, FILL_SCRIPT_BUDGET_MS);

    if (!result.ok) {
        clearFillPreview();
        Q_EMIT statusMessageRequested(QString("Fill script failed: %1").arg(QString::fromStdString(result.error)));
        return result;
    }
    buildFillGhosts();
    _fillPreviewActive = true;
    return result;
}
#endif

void EditorWidget::buildFillGhosts() {
    _fillPreviewFloorTiles.clear();
    _fillPreviewObjects.clear();
    _fillPreviewRoofTiles.clear();

    const auto ghostAlpha = sf::Color(255, 255, 255, ui::constants::sfml::DRAG_PREVIEW_ALPHA);
    for (const TileChange& tc : _fillPlan.tiles) {
        if (auto sprite = pattern::buildTileSprite(_resources, tc.tileIndex, tc.isRoof, tc.after)) {
            sprite->setColor(ghostAlpha);
            (tc.isRoof ? _fillPreviewRoofTiles : _fillPreviewFloorTiles).push_back(std::move(*sprite));
        }
    }
    // Rebuild a fresh ghost per object (don't tint the plan's own Object — that one is committed at
    // full opacity on Apply).
    for (const pattern::FillPlan::Entry& entry : _fillPlan.objects) {
        if (!entry.mapObject) {
            continue;
        }
        auto ghost = pattern::buildSpriteObject(_resources, _session.hexgrid(),
            entry.mapObject->frm_pid, static_cast<int>(entry.mapObject->position), entry.mapObject->direction);
        if (ghost) {
            ghost->getSprite().setColor(ghostAlpha);
            _fillPreviewObjects.push_back(std::move(ghost));
        }
    }
}

void EditorWidget::clearFillPreview() {
    _fillPreviewActive = false;
    _fillPreviewFloorTiles.clear();
    _fillPreviewObjects.clear();
    _fillPreviewRoofTiles.clear();
    _fillPlan = pattern::FillPlan{};
}

void EditorWidget::applyFillPreview(const QString& description) {
    if (!_session.map() || (_fillPlan.objects.empty() && _fillPlan.tiles.empty())) {
        clearFillPreview();
        return;
    }
    // Replay the previewed plan as ONE undo entry — no re-run, so the apply matches the preview
    // exactly (and a multi-thousand-cell fill is a single Ctrl-Z under the UndoStack command cap).
    const pattern::PlacementBatch::Result result = pattern::PlacementBatch::replay(
        _controller.commandController(), _fillPlan, /*buildSprites*/ true, description.toStdString());

    // Post-edit resync (mirrors runScript's mutated() path): the fill changed the map and the header
    // counts, while the selection/visualizer and Map Info still reference the pre-fill state.
    if (_session.selectionManager()) {
        _session.selectionManager()->clearSelection();
    }
    _controller.visualizer().clearHexPositions();
    if (_mainWindow) {
        _mainWindow->updateMapInfo(_session.map());
    }
    Q_EMIT mapModifiedByScript();
    Q_EMIT undoStackChanged();

    QString message = QString("Filled: %1 tiles, %2 objects").arg(result.tilesPainted).arg(result.objectsPlaced);
    if (result.objectsFailed > 0) {
        message += QString(" (%1 missing art)").arg(result.objectsFailed);
    }
    if (_fillPlan.dropped > 0) {
        message += QString(" (%1 off-grid)").arg(_fillPlan.dropped);
    }
    Q_EMIT statusMessageRequested(message);

    clearFillPreview();
}

bool EditorWidget::isTilePlacementMode() const {
    return _tilePlacementManager->isTilePlacementMode();
}

void EditorWidget::refreshObjects() {
    if (!_session.map()) {
        return;
    }

    _controller.spriteLoader().loadObjectSprites(*_session.map(), _session.currentElevation(), _session.objects(), _session.wallBlockerOverlays());

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
    _controller.spriteLoader().updateTileSprite(hexIndex, isRoof, elevation, elevationTiles, _session.floorSprites(), _session.roofSprites());
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

    spdlog::debug("Range selection: area ({:.1f}, {:.1f}, {:.1f}, {:.1f})",
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
        _controller.visualizer().clear();
        _controller.visualizer().apply(preview);
        return;
    }

    if (isAdditive) {
        // Alt+drag adds to the selection, so keep what is already selected highlighted while the
        // covered area is tinted below to preview the addition.
        _controller.visualizer().refresh();
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

void EditorWidget::updateMarkExitsLinePreview(const std::vector<sf::Vector2f>& vertices, sf::Vector2f cursor, bool flipSide) {
    _exitGridLineVertices = vertices;
    _exitGridLineCursor = cursor;
    _exitGridLineActive = true;

    // Tint by the tool's current destination kind: green inter-map, brown world/town map.
    using Kind = ExitGridPlacementManager::DestinationKind;
    const bool worldMap = _exitGridPlacementManager
        && _exitGridPlacementManager->currentDestinationKind() == Kind::WorldMap;
    _exitGridPreviewTint = worldMap ? sf::Color(200, 150, 90, 140) : sf::Color(80, 220, 80, 140);

    // The manager owns the FROZEN committed segments; here we feed it only the ONE live segment (last
    // vertex -> cursor) to append to the frozen run.
    _exitGridPreviewHexes.clear();
    _exitGridPreviewFrmPids.clear();

    if (_exitGridPlacementManager) {
        const bool hasLive = !vertices.empty();
        const sf::Vector2f liveFrom = hasLive ? vertices.back() : cursor;
        auto preview = _exitGridPlacementManager->previewForLine(liveFrom, cursor, hasLive, flipSide);
        _exitGridPreviewHexes = std::move(preview.hexes);
        _exitGridPreviewFrmPids = std::move(preview.frmPids);
    }
}

void EditorWidget::clearMarkExitsLinePreview() {
    _exitGridLineActive = false;
    _exitGridLineVertices.clear();
    _exitGridPreviewHexes.clear();
    _exitGridPreviewFrmPids.clear();
}

void EditorWidget::placeObjectAtPosition(sf::Vector2f worldPos) {
    if (!_session.map()) {
        spdlog::warn("EditorWidget: Cannot place object - no map loaded");
        return;
    }

    int hexPosition = _controller.viewport().worldPosToHexIndex(worldPos);
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

        spdlog::debug("EditorWidget: Successfully placed object at hex {} (pro_pid: {})",
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

void EditorWidget::centerViewOnHex(uint32_t hexPosition) {
    auto hex = _session.hexgrid().getHexByPosition(hexPosition);
    if (!hex) {
        spdlog::warn("EditorWidget::centerViewOnHex: Invalid hex position {}", hexPosition);
        return;
    }
    _controller.viewport().getView().setCenter(
        sf::Vector2f(static_cast<float>(hex->get().x()), static_cast<float>(hex->get().y())));
}

void EditorWidget::centerViewOnPlayerPosition() {
    if (!_session.map()) {
        spdlog::warn("EditorWidget::centerViewOnPlayerPosition: No map loaded");
        return;
    }
    centerViewOnHex(_session.map()->getMapFile().header.player_default_position);
}

int EditorWidget::scriptOwnerElevation(int sid) const {
    if (!_session.map()) {
        return -1;
    }
    // A script's owner is the object whose map_scripts_pid equals the script's SID; objects are keyed by
    // elevation in map_objects. (-1 == no script, so a real sid is always >= 0.)
    for (const auto& [elevation, objects] : _session.map()->getMapFile().map_objects) {
        for (const auto& mapObject : objects) {
            if (mapObject && mapObject->map_scripts_pid == sid) {
                return elevation;
            }
        }
    }
    return -1;
}

std::shared_ptr<Object> EditorWidget::visualObjectForSid(int sid) const {
    for (const auto& object : _session.objects()) {
        if (object && object->hasMapObject() && object->getMapObject().map_scripts_pid == sid) {
            return object;
        }
    }
    return nullptr;
}

bool EditorWidget::revealScriptObject(int sid) {
    const int ownerElevation = scriptOwnerElevation(sid);
    if (ownerElevation < 0) {
        spdlog::debug("EditorWidget::revealScriptObject: No object owns script SID {}", sid);
        return false; // ownerless (spatial / timer / system) — leave the current selection untouched
    }

    // Switch to the owning elevation if needed; changeElevation() rebuilds _session.objects() (fresh
    // visual wrappers) via loadSprites(), so the visual Object is fetched only afterwards. The host
    // re-syncs the elevation menu after this returns true (see MainWindow's scriptObjectActivated handler).
    if (_session.currentElevation() != ownerElevation) {
        changeElevation(ownerElevation);
    }

    const auto object = visualObjectForSid(sid);
    if (!object) {
        // The MapObject exists on the elevation but its visual wrapper is missing (e.g. sprite load
        // failed). Don't disturb the current selection.
        spdlog::warn("EditorWidget::revealScriptObject: Owner of SID {} on elevation {} has no visual object",
            sid, ownerElevation);
        return false;
    }

    // Single-select the object (mirrors reselectAfterDragMove's setSelectedItems path, which notifies the
    // SelectionPanel) and center the view on its hex.
    if (auto* manager = _session.selectionManager()) {
        manager->setSelectedItems({ selection::SelectedItem{ selection::SelectionType::OBJECT, object } });
    }
    centerViewOnHex(static_cast<uint32_t>(object->getMapObject().position));

    spdlog::debug("EditorWidget::revealScriptObject: Revealed object owning script SID {} on elevation {}",
        sid, ownerElevation);
    return true;
}

void EditorWidget::showLoadingErrorsSummary() {
    const auto& loadingErrors = _controller.spriteLoader().lastLoadErrors();
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
    if (_controller.hasRenderingEngine()) {
        _controller.renderingEngine().setSelectionColors(colors);
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

    spdlog::debug("Light overlay display set to: {} (found {} light objects)", show, lightObjectCount);
}

void EditorWidget::clearDragSelectionPreview() {
    clearDragPreview();
    // A Ctrl+drag preview temporarily un-highlights the covered selected items; restore the
    // real selection highlight so cancelling the drag (or clearing after another op) leaves
    // the selection looking correct.
    _controller.visualizer().refresh();

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
        spdlog::debug("EditorWidget::onObjectFrmChanged - updated object visual to FRM PID {} ({})", newFrmPid, newFrmPath);
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
        spdlog::debug("EditorWidget::onObjectFrmPathChanged - updated object visual to FRM path: {}", newFrmPath);

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

    spdlog::debug("EditorWidget::deleteSelectedObjects - Deleting {} selected objects", selectedObjects.size());

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

    _controller.commandController().registerObjectDeletion(removedObjects);

    _session.selectionManager()->clearSelection();

    Q_EMIT selectionChanged(_session.selectionManager()->getCurrentSelection(), _session.currentElevation());

    spdlog::debug("EditorWidget::deleteSelectedObjects - Successfully deleted {} objects", removedObjects.size());
}

} // namespace geck
