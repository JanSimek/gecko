#include "InputHandler.h"
#include <cmath>

namespace geck::ui::components {

InputHandler::InputHandler() {
    // Initialize with default empty handlers
    _mouseClickHandler = [](sf::Vector2f, sf::Mouse::Button) { };
    _mouseMoveHandler = [](sf::Vector2f) { };
    _mouseReleaseHandler = [](sf::Vector2f, sf::Mouse::Button) { };
    _keyPressHandler = [](sf::Keyboard::Key) { };
    _wheelScrollHandler = [](float) { };
}

void InputHandler::handleEvent(const sf::Event& event, const sf::View& view, const sf::Vector2u& windowSize) {
    switch (event.type) {
        case sf::Event::MouseButtonPressed: {
            sf::Vector2f worldPos = pixelToWorldPos(sf::Vector2i(event.mouseButton.x, event.mouseButton.y), view, windowSize);
            _mouseWorldPos = worldPos;
            _lastMouseWorldPos = worldPos;

            // Check for double-click
            if (isDoubleClick(worldPos)) {
                // Handle double-click differently if needed
            }

            // Update last click info for double-click detection
            _lastClickTime.restart();
            _lastClickPosition = worldPos;

            // Start drag detection
            _dragStartPos = worldPos;
            _isDragging = false;

            if (_mouseClickHandler) {
                _mouseClickHandler(worldPos, event.mouseButton.button);
            }
            break;
        }

        case sf::Event::MouseButtonReleased: {
            sf::Vector2f worldPos = pixelToWorldPos(sf::Vector2i(event.mouseButton.x, event.mouseButton.y), view, windowSize);
            _mouseWorldPos = worldPos;

            if (_mouseReleaseHandler) {
                _mouseReleaseHandler(worldPos, event.mouseButton.button);
            }

            _isDragging = false;
            break;
        }

        case sf::Event::MouseMoved: {
            sf::Vector2f worldPos = pixelToWorldPos(sf::Vector2i(event.mouseMove.x, event.mouseMove.y), view, windowSize);
            _lastMouseWorldPos = _mouseWorldPos;
            _mouseWorldPos = worldPos;

            // Check if we should start dragging
            if (!_isDragging && isMousePressed(sf::Mouse::Left)) {
                float distance = std::sqrt(std::pow(worldPos.x - _dragStartPos.x, 2) + std::pow(worldPos.y - _dragStartPos.y, 2));
                if (distance > DRAG_THRESHOLD) {
                    _isDragging = true;
                }
            }

            if (_mouseMoveHandler) {
                _mouseMoveHandler(worldPos);
            }
            break;
        }

        case sf::Event::KeyPressed: {
            updateModifierKeys();
            if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
                if (_keyPressHandler) {
                    _keyPressHandler(keyPressed->scancode);
                }
            }
            break;
        }

        case sf::Event::KeyReleased: {
            updateModifierKeys();
            break;
        }

        case sf::Event::MouseWheelScrolled: {
            if (_wheelScrollHandler) {
                _wheelScrollHandler(event.mouseWheelScroll.delta);
            }
            break;
        }

        default:
            break;
    }
}

void InputHandler::update(float deltaTime) {
    // Update modifier key states
    updateModifierKeys();

    // Add any time-based input processing here
}

bool InputHandler::isMousePressed(sf::Mouse::Button button) const {
    return sf::Mouse::isButtonPressed(button);
}

bool InputHandler::isKeyPressed(sf::Keyboard::Key key) const {
    return sf::Keyboard::isKeyPressed(key);
}

bool InputHandler::isDoubleClick(sf::Vector2f worldPos) const {
    if (_lastClickTime.getElapsedTime().asSeconds() > DOUBLE_CLICK_TIME) {
        return false;
    }

    float distance = std::sqrt(std::pow(worldPos.x - _lastClickPosition.x, 2) + std::pow(worldPos.y - _lastClickPosition.y, 2));
    return distance <= DOUBLE_CLICK_DISTANCE;
}

sf::Vector2f InputHandler::pixelToWorldPos(sf::Vector2i pixelPos, const sf::View& view, const sf::Vector2u& windowSize) const {
    // Convert pixel coordinates to world coordinates using the view
    sf::Vector2f normalizedPos(
        static_cast<float>(pixelPos.x) / static_cast<float>(windowSize.x),
        static_cast<float>(pixelPos.y) / static_cast<float>(windowSize.y));

    // Transform to world coordinates
    sf::Vector2f viewSize = view.getSize();
    sf::Vector2f viewCenter = view.getCenter();

    sf::Vector2f worldPos(
        viewCenter.x + (normalizedPos.x - 0.5f) * viewSize.x,
        viewCenter.y + (normalizedPos.y - 0.5f) * viewSize.y);

    return worldPos;
}

void InputHandler::updateModifierKeys() {
    _shiftPressed = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);
    _ctrlPressed = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl);
    _altPressed = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt);
}

} // namespace geck::ui::components