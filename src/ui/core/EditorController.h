#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "ui/core/EditorSession.h"
#include "ui/core/ObjectPicker.h"
#include "ui/core/SelectionVisualizer.h"

namespace geck {

class ObjectCommandController;
class MapSpriteLoader;
class Tile;

namespace resource {
    class GameResources;
}

/**
 * @brief Qt-free owner of the editor's per-map state and the helpers that act on it.
 *
 * The editor's non-Qt core lives here: the per-map EditorSession, the ObjectPicker
 * (hit-testing) and SelectionVisualizer (selection highlight state) that read it, and
 * — after initEditingCore() — the MapSpriteLoader and ObjectCommandController that
 * drive sprite loading and undoable edits. EditorWidget remains the QWidget shell
 * (Qt managers, rendering, input, signals) and delegates here.
 *
 * MapSpriteLoader and ObjectCommandController are owned together because the command
 * controller holds a reference to the loader; co-locating them keeps that reference
 * valid for the controller's whole lifetime.
 */
class EditorController {
public:
    // Callbacks the command controller invokes back into the editor widget (refresh,
    // tile access, sprite updates). Passed in by EditorWidget at init time.
    struct EditingCoreCallbacks {
        std::function<void()> refreshObjects;
        std::function<void()> undoStackChanged;
        std::function<std::vector<Tile>&(int)> ensureElevationTiles;
        std::function<int()> currentElevation;
        std::function<void(int hexIndex, bool isRoof, int elevation)> updateTileSprite;
        std::function<void()> loadTileSprites;
    };

    EditorController();
    ~EditorController();

    // Non-copyable / non-movable: the helpers hold references into _session (and the
    // command controller into the loader), so any copy/move would dangle them.
    EditorController(const EditorController&) = delete;
    EditorController& operator=(const EditorController&) = delete;
    EditorController(EditorController&&) = delete;
    EditorController& operator=(EditorController&&) = delete;

    /// Builds the MapSpriteLoader and ObjectCommandController (which need runtime deps
    /// and callbacks into EditorWidget). Call once from EditorWidget::init().
    void initEditingCore(resource::GameResources& resources, EditingCoreCallbacks callbacks);

    EditorSession& session() { return _session; }
    const EditorSession& session() const { return _session; }

    ObjectPicker& picker() { return _picker; }
    const ObjectPicker& picker() const { return _picker; }
    SelectionVisualizer& visualizer() { return _visualizer; }
    const SelectionVisualizer& visualizer() const { return _visualizer; }

    MapSpriteLoader& spriteLoader() { return *_spriteLoader; }
    ObjectCommandController& commandController() { return *_commandController; }
    const ObjectCommandController& commandController() const { return *_commandController; }

private:
    EditorSession _session;
    ObjectPicker _picker{ _session };
    SelectionVisualizer _visualizer{ _session };
    // Declared after the loader so the command controller (which references the loader)
    // is destroyed first.
    std::unique_ptr<MapSpriteLoader> _spriteLoader;
    std::unique_ptr<ObjectCommandController> _commandController;
};

} // namespace geck
