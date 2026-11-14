#pragma once

#include "editor/helper/BlackEdge.h"
#include "editing/tools/ITool.h"

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace geck {

/// Frames a map with Fallout 2's black edge rectangles (see editor/helper/BlackEdge.h), drawn as a
/// polyline like the exit grids' "Draw edge": click to start, click at each corner, and every
/// segment runs straight across or straight down the screen. The line is the inner edge of the
/// black; the black goes on the side away from the middle of the floor, and Space flips the
/// segment being drawn. Right-click, Enter or a second click on the last point finishes;
/// clicking the first point closes the frame. The whole line is one undo step.
class BlackEdgeTool final : public ITool {
public:
    static constexpr std::string_view ID = "native.black-edge";

    struct Host {
        const HexagonGrid* grid = nullptr;
        /// The two pieces' frame sizes and shifts (nullopt if their art can't be loaded).
        std::function<std::optional<black_edge::Shapes>()> shapes;
        /// The middle of the current elevation's floor, which the black faces away from.
        std::function<sf::Vector2f()> floorCenter;
        /// The art fid for a piece's proto, for the ghost (0 if it can't be loaded).
        std::function<uint32_t(uint32_t proPid)> frmPidFor;
        /// Places the pieces as one undo step. Returns how many were placed.
        std::function<int(const std::vector<black_edge::Placement>&)> placePieces;
    };

    explicit BlackEdgeTool(Host host)
        : _host(std::move(host)) {
    }

    std::string_view id() const override { return ID; }

    std::string_view statusHint() const override {
        return "Click: start / add a corner\nSpace: black on the other side\n"
               "Click the last point, Enter or right-click: finish\nClick the first point: close the frame\n"
               "Backspace: undo a corner\nEsc: drop the line";
    }

    void onDeactivate() override {
        clearLine(); // an unfinished line is dropped, never half-placed
    }

    bool onMousePressed(const ToolMouseEvent& event) override {
        if (event.button == sf::Mouse::Button::Right) {
            if (!_runs.empty()) {
                finish();
                return true;
            }
            if (_start) {
                clearLine();
                return true;
            }
            return false; // nothing drawn: let the host leave the tool
        }
        if (event.button != sf::Mouse::Button::Left || !_host.grid) {
            return false;
        }
        if (!_start) {
            if (const int vertex = black_edge::snapVertex(*_host.grid, event.worldPos); vertex >= 0) {
                _start = vertex;
            }
            return true;
        }
        const int from = lastVertex();
        const int end = black_edge::segmentEnd(*_host.grid, from, event.worldPos);
        if (end == from) {
            if (!_runs.empty()) {
                finish(); // clicked the last point again
            }
            return true;
        }
        _runs.push_back(liveRun(from, end));
        _flip = false;
        if (_runs.size() >= 4 && end == *_start) {
            finish(); // closed the frame
        }
        return true;
    }

    bool onMouseMoved(const ToolMouseEvent&) override {
        return true; // the host rebuilds the ghost after every move
    }

    bool onKeyPressed(const ToolKeyEvent& event) override {
        switch (event.code) {
            case sf::Keyboard::Key::Space:
                _flip = !_flip;
                return true;
            case sf::Keyboard::Key::Enter:
                if (!_runs.empty()) {
                    finish();
                    return true;
                }
                return false;
            case sf::Keyboard::Key::Backspace:
                // Always consumed: unhandled, Backspace falls through to "delete the selection".
                if (!_runs.empty()) {
                    _runs.pop_back();
                } else {
                    _start.reset();
                }
                return true;
            case sf::Keyboard::Key::Escape:
                if (_start) {
                    clearLine();
                    return true;
                }
                return false; // nothing drawn: the host leaves the tool
            default:
                return false;
        }
    }

    ToolPreviewSpec buildPreview(const ToolMouseEvent& event) override {
        ToolPreviewSpec spec;
        if (!_host.grid || !_host.frmPidFor || !_start) {
            return spec;
        }
        const auto shapes = _host.shapes ? _host.shapes() : std::nullopt;
        if (!shapes) {
            return spec;
        }
        std::vector<black_edge::Run> runs = _runs;
        const int from = lastVertex();
        if (const int end = black_edge::segmentEnd(*_host.grid, from, event.worldPos); end != from) {
            runs.push_back(liveRun(from, end));
        }
        for (const auto& piece : black_edge::piecesForLine(*_host.grid, runs, *shapes)) {
            if (const uint32_t frmPid = _host.frmPidFor(piece.pid); frmPid != 0) {
                spec.objects.push_back({ .frmPid = frmPid, .hex = piece.hex, .direction = 0 });
            }
        }
        return spec;
    }

    /// The finished segments so far, for tests.
    [[nodiscard]] const std::vector<black_edge::Run>& runs() const { return _runs; }
    [[nodiscard]] bool hasStart() const { return _start.has_value(); }

private:
    int lastVertex() const { return _runs.empty() ? *_start : _runs.back().to; }

    black_edge::Run liveRun(int from, int to) const {
        const sf::Vector2f center = _host.floorCenter ? _host.floorCenter() : sf::Vector2f();
        const bool before = black_edge::autoBlackBefore(*_host.grid, from, to, center);
        return { from, to, _flip ? !before : before };
    }

    void clearLine() {
        _start.reset();
        _runs.clear();
        _flip = false;
    }

    void finish() {
        const auto shapes = _host.shapes ? _host.shapes() : std::nullopt;
        const auto runs = _runs;
        clearLine();
        if (!shapes || !_host.grid || !_host.placePieces) {
            return;
        }
        const auto pieces = black_edge::piecesForLine(*_host.grid, runs, *shapes);
        if (!pieces.empty()) {
            _host.placePieces(pieces);
        }
    }

    Host _host;
    std::optional<int> _start;
    std::vector<black_edge::Run> _runs;
    bool _flip = false; // Space: put the live segment's black on the other side
};

} // namespace geck
