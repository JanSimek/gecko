#include "EditorController.h"

#include "editing/commands/ObjectCommandController.h"
#include "rendering/MapSpriteLoader.h"
#include "viewport/ViewportController.h"

namespace geck {

EditorController::EditorController()
    : _viewport(std::make_unique<ViewportController>(&_session.hexgrid())) {
}

EditorController::~EditorController() = default;

void EditorController::initEditingCore(resource::GameResources& resources, EditingCoreCallbacks callbacks) {
    _spriteLoader = std::make_unique<MapSpriteLoader>(resources, _session.hexgrid());
    _commandController = std::make_unique<ObjectCommandController>(
        resources,
        _session.mapPtr(),
        _session.hexgrid(),
        *_spriteLoader,
        _session.objects(),
        _session.wallBlockerOverlays(),
        _session.undoStack(),
        std::move(callbacks.refreshObjects),
        std::move(callbacks.undoStackChanged),
        std::move(callbacks.ensureElevationTiles),
        std::move(callbacks.currentElevation),
        std::move(callbacks.updateTileSprite),
        std::move(callbacks.loadTileSprites));
}

} // namespace geck
