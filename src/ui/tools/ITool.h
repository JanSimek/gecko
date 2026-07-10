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

    virtual bool onMousePressed(const ToolMouseEvent&) { return false; }
    virtual bool onMouseMoved(const ToolMouseEvent&) { return false; }
    virtual bool onMouseReleased(const ToolMouseEvent&) { return false; }
    virtual bool onKeyPressed(const ToolKeyEvent&) { return false; }
    virtual ToolPreview buildPreview(const ToolMouseEvent&) { return {}; }
};

} // namespace geck
