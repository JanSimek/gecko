#pragma once

#include "ui/tools/ITool.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

namespace geck {

/// Freehand tile brush: hold the left button and drag to paint the palette's tile onto
/// every tile the cursor crosses; one stroke is one undo entry (the host's command batch
/// opens on press and flushes on release, or on deactivation if the release never
/// arrives). The hover ghost is a ToolPreviewSpec, so this tool is the first real
/// consumer of the spec->sprites preview channel.
///
/// The tool is host-agnostic: everything editor-specific is injected as callbacks, so
/// the stroke mechanics (dedupe, batch lifecycle) are unit-testable without an editor.
class FillBrushTool final : public ITool {
public:
    struct Host {
        /// Resolve a world position to the tile index under it on the given layer
        /// (std::nullopt when off-map). Same projection the paint uses, so the ghost,
        /// the dedupe key, and the painted tile always agree.
        std::function<std::optional<int>(sf::Vector2f worldPos, bool isRoof)> resolveTile;
        /// Paint one tile (undoable; buffered into the open stroke batch).
        std::function<void(int tileId, sf::Vector2f worldPos, bool isRoof)> paintTile;
        /// Stroke = undo batch. beginStroke/endStroke are called strictly paired.
        std::function<void(const std::string& description)> beginStroke;
        std::function<void()> endStroke;
    };

    explicit FillBrushTool(Host host)
        : _host(std::move(host)) {
    }

    /// The palette's current selection; may be re-set while the tool is active (picking a
    /// new tile mid-session re-loads the brush without leaving the tool).
    void setTile(int tileId, bool isRoof) {
        _tileId = tileId;
        _isRoof = isRoof;
    }
    int tileId() const { return _tileId; }
    bool isRoof() const { return _isRoof; }
    bool strokeActive() const { return _strokeActive; }

    std::string_view id() const override { return ID; }

    std::string_view statusHint() const override {
        return "Drag: paint\nEsc / right-click: done";
    }

    void onDeactivate() override {
        // The host guarantees Esc/right-click leave the tool without consulting us, and a
        // release can be lost off-widget — never strand an open batch.
        finishStroke();
    }

    bool onMousePressed(const ToolMouseEvent& event) override {
        if (event.button != sf::Mouse::Button::Left || _tileId < 0) {
            // Unconsumed right-click = the host's cancel path, by design.
            return false;
        }
        _strokeActive = true;
        _paintedThisStroke.clear();
        _host.beginStroke("Fill Brush");
        paintAt(event.worldPos);
        return true;
    }

    bool onMouseMoved(const ToolMouseEvent& event) override {
        if (_strokeActive) {
            paintAt(event.worldPos);
        }
        // Consume moves even between strokes: the brush owns the cursor while active, and
        // the hover ghost comes from buildPreview.
        return true;
    }

    bool onMouseReleased(const ToolMouseEvent& event) override {
        if (event.button != sf::Mouse::Button::Left || !_strokeActive) {
            return false;
        }
        finishStroke();
        return true;
    }

    ToolPreviewSpec buildPreview(const ToolMouseEvent& event) override {
        ToolPreviewSpec spec;
        if (_tileId < 0 || !_host.resolveTile) {
            return spec;
        }
        if (const auto tile = _host.resolveTile(event.worldPos, _isRoof)) {
            auto& layer = _isRoof ? spec.roofTiles : spec.floorTiles;
            layer.push_back({ *tile, static_cast<uint16_t>(_tileId) });
        }
        return spec;
    }

    static constexpr std::string_view ID = "native.fill-brush";

private:
    void paintAt(sf::Vector2f worldPos) {
        if (!_host.resolveTile || !_host.paintTile) {
            return;
        }
        const auto tile = _host.resolveTile(worldPos, _isRoof);
        // Dedupe on the resolved tile so a stroke crossing back over painted ground adds
        // no redundant commands to the batch.
        if (!tile || !_paintedThisStroke.insert(*tile).second) {
            return;
        }
        _host.paintTile(_tileId, worldPos, _isRoof);
    }

    void finishStroke() {
        if (!_strokeActive) {
            return;
        }
        _strokeActive = false;
        _paintedThisStroke.clear();
        if (_host.endStroke) {
            _host.endStroke();
        }
    }

    Host _host;
    int _tileId = -1;
    bool _isRoof = false;
    bool _strokeActive = false;
    std::unordered_set<int> _paintedThisStroke;
};

} // namespace geck
