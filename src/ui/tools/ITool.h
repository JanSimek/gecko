#pragma once

#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include <optional>
#include <string_view>

#include "rendering/ToolPreview.h"

namespace geck {

struct ToolModifiers {
    bool shift = false;
    bool control = false;
    bool alt = false;
};

/// The host resolves engine coordinates before dispatch: hexIndex/tileIndex are what a
/// tool should act on. worldPos is provided for native tools that need sub-hex precision
/// (cursor ghosts); the future plugin-facing (Lua) binding exposes only the engine
/// coordinates, never raw world pixels.
struct ToolMouseEvent {
    sf::Vector2f worldPos;
    int hexIndex = -1;
    int tileIndex = -1;
    std::optional<sf::Mouse::Button> button;
    ToolModifiers modifiers;
};

struct ToolKeyEvent {
    sf::Keyboard::Key code = sf::Keyboard::Key::Unknown;
    ToolModifiers modifiers;
};

/// A registered editor tool, dispatched while EditorMode::PluginTool is active.
///
/// Event handlers return true to consume the event; an unconsumed event falls through to
/// the default Select-mode behaviour (selection clicks, drag-select), and an unconsumed
/// Escape / right-click is handled by the HOST as "leave the tool" — a tool does not need
/// to implement its own cancel path, only consume these if it wants different behaviour.
///
/// Handlers must not unregister tools (including themselves) from the ToolRegistry: the
/// registry owns the tool, and erasing it destroys the object mid-callback. Deferred
/// teardown belongs to the host that dispatched the event.
class ITool {
public:
    virtual ~ITool() = default;

    virtual std::string_view id() const = 0;
    virtual void onActivate() {
        // Optional lifecycle hook.
    }
    virtual void onDeactivate() {
        // Optional lifecycle hook.
    }

    /// Status-bar key hints while this tool is active, one item per line (e.g.
    /// "Click: place\nR: rotate"); the host joins them with its separator. Empty means
    /// the generic PluginTool hint.
    virtual std::string_view statusHint() const { return {}; }

    virtual bool onMousePressed(const ToolMouseEvent&) { return false; }
    virtual bool onMouseMoved(const ToolMouseEvent&) { return false; }
    virtual bool onMouseReleased(const ToolMouseEvent&) { return false; }
    virtual bool onKeyPressed(const ToolKeyEvent&) { return false; }

    /// What to ghost under the cursor, in engine terms (tile ids / PIDs / hexes). The
    /// host lowers the spec to sprites through the shared pattern art pipeline.
    virtual ToolPreviewSpec buildPreview(const ToolMouseEvent&) { return {}; }
};

} // namespace geck
