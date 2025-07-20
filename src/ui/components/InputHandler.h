#pragma once

#include <SFML/Graphics.hpp>
#include <functional>
#include <memory>

namespace geck::ui::components {

/**
 * @brief Handles input events and delegates to appropriate handlers
 * 
 * This component centralizes input handling and provides a clean interface
 * for different input actions like panning, selection, and placement.
 */
class InputHandler {
public:
    // Action types for different input modes
    enum class ActionType {
        NONE,
        PANNING,
        DRAG_SELECTING,
        TILE_PLACING,
        OBJECT_MOVING
    };
    
    // Input event handlers (function pointers for decoupling)
    using MouseClickHandler = std::function<void(sf::Vector2f worldPos, sf::Mouse::Button button)>;
    using MouseMoveHandler = std::function<void(sf::Vector2f worldPos)>;
    using MouseReleaseHandler = std::function<void(sf::Vector2f worldPos, sf::Mouse::Button button)>;
    using KeyPressHandler = std::function<void(sf::Keyboard::Scancode key)>;
    using WheelScrollHandler = std::function<void(float delta)>;
    
    InputHandler();
    ~InputHandler() = default;
    
    // Event processing
    void handleEvent(const sf::Event& event, const sf::View& view, const sf::Vector2u& windowSize);
    void update(float deltaTime);
    
    // Handler registration
    void setMouseClickHandler(MouseClickHandler handler) { _mouseClickHandler = handler; }
    void setMouseMoveHandler(MouseMoveHandler handler) { _mouseMoveHandler = handler; }
    void setMouseReleaseHandler(MouseReleaseHandler handler) { _mouseReleaseHandler = handler; }
    void setKeyPressHandler(KeyPressHandler handler) { _keyPressHandler = handler; }
    void setWheelScrollHandler(WheelScrollHandler handler) { _wheelScrollHandler = handler; }
    
    // State management
    void setCurrentAction(ActionType action) { _currentAction = action; }
    ActionType getCurrentAction() const { return _currentAction; }
    
    // Input state queries
    bool isMousePressed(sf::Mouse::Button button) const;
    bool isKeyPressed(sf::Keyboard::Key key) const;
    sf::Vector2f getMouseWorldPosition() const { return _mouseWorldPos; }
    sf::Vector2f getLastMouseWorldPosition() const { return _lastMouseWorldPos; }
    
    // Modifier key state
    bool isShiftPressed() const { return _shiftPressed; }
    bool isCtrlPressed() const { return _ctrlPressed; }
    bool isAltPressed() const { return _altPressed; }
    
    // Double-click detection
    bool isDoubleClick(sf::Vector2f worldPos) const;
    
    // Drag detection
    bool isDragging() const { return _isDragging; }
    sf::Vector2f getDragStartPosition() const { return _dragStartPos; }
    
private:
    ActionType _currentAction = ActionType::NONE;
    
    // Current input state
    sf::Vector2f _mouseWorldPos;
    sf::Vector2f _lastMouseWorldPos;
    sf::Vector2f _dragStartPos;
    bool _isDragging = false;
    
    // Modifier key states
    bool _shiftPressed = false;
    bool _ctrlPressed = false;
    bool _altPressed = false;
    
    // Double-click detection
    sf::Clock _lastClickTime;
    sf::Vector2f _lastClickPosition;
    static constexpr float DOUBLE_CLICK_TIME = 0.5f;
    static constexpr float DOUBLE_CLICK_DISTANCE = 10.0f;
    
    // Drag detection
    static constexpr float DRAG_THRESHOLD = 5.0f; // pixels
    
    // Event handlers
    MouseClickHandler _mouseClickHandler;
    MouseMoveHandler _mouseMoveHandler;
    MouseReleaseHandler _mouseReleaseHandler;
    KeyPressHandler _keyPressHandler;
    WheelScrollHandler _wheelScrollHandler;
    
    // Helper methods
    sf::Vector2f pixelToWorldPos(sf::Vector2i pixelPos, const sf::View& view, const sf::Vector2u& windowSize) const;
    void updateModifierKeys();
};

} // namespace geck::ui::components