#pragma once

#include "editing/tools/ITool.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace geck {

struct MapObject;
class Object;

/// Click (or drag) to delete every blocker of one kind on the hex under the cursor.
/// Generic over blocker kind — the host callback decides what counts (see EditorWidget's
/// wallBlockersAtHex / scrollBlockersAtHex); this tool only owns the click/drag/undo-batch
/// mechanics, shared with AddBlockerTool and FillBrushTool.
class RemoveBlockerTool final : public ITool {
public:
    struct Host {
        /// The blockers of this kind on this hex (data+visual pair, ready for
        /// ObjectCommandController::registerObjectDeletion), or empty if none / off-map.
        std::function<std::vector<std::pair<std::shared_ptr<MapObject>, std::shared_ptr<Object>>>(int hexIndex)> blockersAtHex;
        /// Removes the given objects: the host is expected to have already erased them from
        /// the map data/sprites (see ObjectCommandController::registerObjectDeletion's
        /// contract) and to record the undo command here.
        std::function<void(const std::vector<std::pair<std::shared_ptr<MapObject>, std::shared_ptr<Object>>>&)> removeObjects;
        /// Stroke = undo batch. beginStroke/endStroke are called strictly paired.
        std::function<void(const std::string& description)> beginStroke;
        std::function<void()> endStroke;
        /// Registered-tool id (must be unique per instance — see ToolRegistry).
        std::string_view id;
        /// Status-bar hint while this tool is active.
        std::string_view hint;
        /// Undo-stack label for one stroke (e.g. "Remove Wall Blocker").
        std::string strokeDescription;
    };

    explicit RemoveBlockerTool(Host host)
        : _host(std::move(host)) {
    }

    std::string_view id() const override { return _host.id; }

    std::string_view statusHint() const override { return _host.hint; }

    void onDeactivate() override {
        // Mirrors FillBrushTool: a release can be lost off-widget, so never strand an open batch.
        finishStroke();
    }

    bool onMousePressed(const ToolMouseEvent& event) override {
        if (event.button != sf::Mouse::Button::Left) {
            return false;
        }
        _strokeActive = true;
        _clearedThisStroke.clear();
        _host.beginStroke(_host.strokeDescription);
        removeAt(event.hexIndex);
        return true;
    }

    bool onMouseMoved(const ToolMouseEvent& event) override {
        if (_strokeActive) {
            removeAt(event.hexIndex);
        }
        // Consume moves even between strokes, matching FillBrushTool: the tool owns the
        // cursor while active.
        return true;
    }

    bool onMouseReleased(const ToolMouseEvent& event) override {
        if (event.button != sf::Mouse::Button::Left || !_strokeActive) {
            return false;
        }
        finishStroke();
        return true;
    }

private:
    void removeAt(int hexIndex) {
        if (!_host.blockersAtHex || !_host.removeObjects) {
            return;
        }
        // Dedupe on hex: a drag can cross the same hex on consecutive moves, and its
        // blockers are already gone after the first pass.
        if (hexIndex < 0 || !_clearedThisStroke.insert(hexIndex).second) {
            return;
        }
        auto blockers = _host.blockersAtHex(hexIndex);
        if (!blockers.empty()) {
            _host.removeObjects(blockers);
        }
    }

    void finishStroke() {
        if (!_strokeActive) {
            return;
        }
        _strokeActive = false;
        _clearedThisStroke.clear();
        if (_host.endStroke) {
            _host.endStroke();
        }
    }

    Host _host;
    bool _strokeActive = false;
    std::unordered_set<int> _clearedThisStroke;
};

} // namespace geck
