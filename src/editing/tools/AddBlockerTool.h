#pragma once

#include "editing/tools/ITool.h"

#include <functional>
#include <string>
#include <unordered_set>

namespace geck {

/// Click (or drag) to drop a blocker marker on the hex under the cursor. Generic over
/// blocker kind — the host callback decides what actually gets placed (see EditorWidget's
/// placeWallBlockerAtHex / placeScrollBlockerAtHex); this tool only owns the click/drag/
/// undo-batch mechanics, shared with RemoveBlockerTool and FillBrushTool.
class AddBlockerTool final : public ITool {
public:
    struct Host {
        /// Places a blocker on the hex unless one of this kind is already there. Returns
        /// true iff it placed one. Undoable (registerObjectPlacement), buffered into the
        /// open stroke batch.
        std::function<bool(int hexIndex)> placeBlockerAtHex;
        /// Stroke = undo batch. beginStroke/endStroke are called strictly paired.
        std::function<void(const std::string& description)> beginStroke;
        std::function<void()> endStroke;
        /// Registered-tool id (must be unique per instance — see ToolRegistry).
        std::string_view id;
        /// Status-bar hint while this tool is active.
        std::string_view hint;
        /// Undo-stack label for one stroke (e.g. "Add Wall Blocker").
        std::string strokeDescription;
    };

    explicit AddBlockerTool(Host host)
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
        _stampedThisStroke.clear();
        _host.beginStroke(_host.strokeDescription);
        placeAt(event);
        return true;
    }

    bool onMouseMoved(const ToolMouseEvent& event) override {
        if (_strokeActive) {
            placeAt(event);
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
    void placeAt(const ToolMouseEvent& event) {
        if (!_host.placeBlockerAtHex) {
            return;
        }
        // Dedupe on hex: a drag can cross the same hex on consecutive moves, and it's
        // already stamped after the first pass.
        if (event.hexIndex < 0 || !_stampedThisStroke.insert(event.hexIndex).second) {
            return;
        }
        _host.placeBlockerAtHex(event.hexIndex);
    }

    void finishStroke() {
        if (!_strokeActive) {
            return;
        }
        _strokeActive = false;
        _stampedThisStroke.clear();
        if (_host.endStroke) {
            _host.endStroke();
        }
    }

    Host _host;
    bool _strokeActive = false;
    std::unordered_set<int> _stampedThisStroke;
};

} // namespace geck
