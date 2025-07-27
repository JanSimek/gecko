#include "InputHandler.h"
#include "../EditorWidget.h"
#include "../../util/Constants.h"
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>
#include <spdlog/spdlog.h>

namespace geck {

InputHandler::InputHandler(EditorWidget* editor)
    : _editor(editor) {
}

void InputHandler::handleEvent(const sf::Event& event, 
                              sf::RenderWindow* window,
                              const sf::View& view) {
    if (!window) return;

    // Dispatch to specific event handlers
    if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        handleMousePressed(*mousePressed, window, view);
    } else if (const auto* mouseReleased = event.getIf<sf::Event::MouseButtonReleased>()) {
        handleMouseReleased(*mouseReleased, window, view);
    } else if (const auto* mouseMoved = event.getIf<sf::Event::MouseMoved>()) {
        handleMouseMoved(*mouseMoved, window, view);
    } else if (const auto* mouseWheelScrolled = event.getIf<sf::Event::MouseWheelScrolled>()) {
        handleMouseWheelScrolled(*mouseWheelScrolled);
    } else if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
        handleKeyPressed(*keyPressed);
    } else if (const auto* keyReleased = event.getIf<sf::Event::KeyReleased>()) {
        handleKeyReleased(*keyReleased);
    } else if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        handleWindowResized(*resized, window);
    }
}

void InputHandler::handleMousePressed(const sf::Event::MouseButtonPressed& event,
                                     sf::RenderWindow* window,
                                     const sf::View& view) {
    sf::Vector2f worldPos = pixelToWorld(event.position, window, view);

    if (event.button == sf::Mouse::Button::Left) {
        // Handle player position selection mode
        if (_playerPositionMode) {
            if (_callbacks.onPlayerPositionSelect) {
                _callbacks.onPlayerPositionSelect(worldPos);
            }
            _playerPositionMode = false; // Exit mode after selection
            return;
        }

        // Handle tile placement mode
        if (_tilePlacementMode && _tilePlacementIndex >= 0 && !_tilePlacementReplaceMode) {
            _currentAction = EditorAction::TILE_PLACING;
            _dragStartWorldPos = worldPos;
            _isDragging = false;
            _tilePlacementIsRoof = isShiftPressed();
            return;
        }

        // Check for object dragging
        SelectionModifier modifier = getSelectionModifier();
        bool hasModifiers = (modifier != SelectionModifier::NONE);
        bool canDragObject = !hasModifiers && _callbacks.canStartObjectDrag && 
                            _callbacks.canStartObjectDrag(worldPos);

        if (canDragObject && _callbacks.onObjectDragStart) {
            if (_callbacks.onObjectDragStart(worldPos)) {
                _currentAction = EditorAction::OBJECT_MOVING;
                _isDragging = false;
            }
        } else {
            // Determine if we should start drag selection or immediate selection
            bool canDragSelect = !hasModifiers && 
                (_selectionMode == SelectionMode::ALL || 
                 _selectionMode == SelectionMode::FLOOR_TILES || 
                 _selectionMode == SelectionMode::ROOF_TILES || 
                 _selectionMode == SelectionMode::ROOF_TILES_ALL || 
                 _selectionMode == SelectionMode::OBJECTS || 
                 _selectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE);

            if (canDragSelect) {
                _currentAction = EditorAction::DRAG_SELECTING;
                _dragStartWorldPos = worldPos;
                _isDragging = false;
                _immediateSelectionPerformed = false;
            } else {
                // Immediate selection with modifier
                if (_callbacks.onSelectionClick) {
                    _callbacks.onSelectionClick(worldPos, modifier);
                }
                _immediateSelectionPerformed = true;
            }
        }
    } else if (event.button == sf::Mouse::Button::Right) {
        // Handle right-click
        if (_tilePlacementMode) {
            // Cancel tile placement
            _tilePlacementMode = false;
            _tilePlacementIndex = -1;
            if (_callbacks.onTilePlacementCancel) {
                _callbacks.onTilePlacementCancel();
            }
            spdlog::info("Tile placement mode cancelled with right-click");
        } else {
            // Start panning
            _currentAction = EditorAction::PANNING;
            _mouseStartPos = event.position;
            _mouseLastPos = _mouseStartPos;
        }
    }
}

void InputHandler::handleMouseReleased(const sf::Event::MouseButtonReleased& event,
                                      sf::RenderWindow* window,
                                      const sf::View& view) {
    sf::Vector2f worldPos = pixelToWorld(event.position, window, view);

    if (event.button == sf::Mouse::Button::Left) {
        // Don't process during player position mode
        if (_playerPositionMode) {
            return;
        }

        switch (_currentAction) {
            case EditorAction::TILE_PLACING:
                if (_isDragging && _tilePlacementMode && _callbacks.onTileAreaFill) {
                    // Complete tile area fill
                    _callbacks.onTileAreaFill(_dragStartWorldPos, worldPos, _tilePlacementIsRoof);
                } else if (_tilePlacementMode && _tilePlacementIndex >= 0 && _callbacks.onTilePlacement) {
                    // Single tile placement
                    _callbacks.onTilePlacement(_dragStartWorldPos);
                }
                break;

            case EditorAction::DRAG_SELECTING:
                if (_isDragging) {
                    if (_selectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE && 
                        _callbacks.onScrollBlockerRectangle) {
                        // Handle scroll blocker rectangle
                        float left = std::min(_dragStartWorldPos.x, worldPos.x);
                        float top = std::min(_dragStartWorldPos.y, worldPos.y);
                        float width = std::abs(worldPos.x - _dragStartWorldPos.x);
                        float height = std::abs(worldPos.y - _dragStartWorldPos.y);
                        sf::FloatRect area({left, top}, {width, height});
                        _callbacks.onScrollBlockerRectangle(area);
                    } else if (_callbacks.onDragSelection) {
                        // Normal drag selection
                        _callbacks.onDragSelection(_dragStartWorldPos, worldPos);
                    }
                } else if (!_immediateSelectionPerformed && _callbacks.onSelectionClick) {
                    // Click selection (no drag occurred)
                    _callbacks.onSelectionClick(worldPos, SelectionModifier::NONE);
                }
                break;

            case EditorAction::OBJECT_MOVING:
                if (_callbacks.onObjectDragEnd) {
                    _callbacks.onObjectDragEnd(worldPos);
                }
                break;

            default:
                break;
        }

        // Reset state
        _currentAction = EditorAction::NONE;
        _isDragging = false;
        _immediateSelectionPerformed = false;
    } else if (event.button == sf::Mouse::Button::Right) {
        if (_currentAction == EditorAction::PANNING) {
            _currentAction = EditorAction::NONE;
        }
    }
}

void InputHandler::handleMouseMoved(const sf::Event::MouseMoved& event,
                                   sf::RenderWindow* window,
                                   const sf::View& view) {
    sf::Vector2f worldPos = pixelToWorld(event.position, window, view);

    // Always update hover position
    if (_callbacks.onMouseMove) {
        _callbacks.onMouseMove(worldPos);
    }

    switch (_currentAction) {
        case EditorAction::PANNING: {
            // Calculate pan delta
            sf::Vector2i delta = event.position - _mouseLastPos;
            if (_callbacks.onPan) {
                _callbacks.onPan(sf::Vector2f(static_cast<float>(-delta.x), 
                                            static_cast<float>(-delta.y)));
            }
            _mouseLastPos = event.position;
            break;
        }

        case EditorAction::DRAG_SELECTING:
            // Start dragging after minimum movement
            if (!_isDragging) {
                sf::Vector2f dragDelta = worldPos - _dragStartWorldPos;
                float dragDistance = std::sqrt(dragDelta.x * dragDelta.x + dragDelta.y * dragDelta.y);
                if (dragDistance > 5.0f) { // 5 pixel threshold
                    _isDragging = true;
                }
            }
            // Update drag selection preview
            if (_isDragging && _callbacks.onDragSelectionPreview) {
                _callbacks.onDragSelectionPreview(_dragStartWorldPos, worldPos);
            }
            break;
            
        case EditorAction::TILE_PLACING:
            // Start dragging after minimum movement
            if (!_isDragging) {
                sf::Vector2f dragDelta = worldPos - _dragStartWorldPos;
                float dragDistance = std::sqrt(dragDelta.x * dragDelta.x + dragDelta.y * dragDelta.y);
                if (dragDistance > 5.0f) { // 5 pixel threshold
                    _isDragging = true;
                }
            }
            break;

        case EditorAction::OBJECT_MOVING:
            if (!_isDragging) {
                _isDragging = true; // Start dragging on first movement
            }
            if (_callbacks.onObjectDragUpdate) {
                _callbacks.onObjectDragUpdate(worldPos);
            }
            break;

        default:
            break;
    }
}

void InputHandler::handleMouseWheelScrolled(const sf::Event::MouseWheelScrolled& event) {
    if (event.wheel == sf::Mouse::Wheel::Vertical && _callbacks.onZoom) {
        _callbacks.onZoom(event.delta);
    }
}

void InputHandler::handleKeyPressed(const sf::Event::KeyPressed& event) {
    if (event.code == sf::Keyboard::Key::Escape) {
        // Handle escape key
        if (_currentAction == EditorAction::OBJECT_MOVING && _callbacks.onObjectDragCancel) {
            _callbacks.onObjectDragCancel();
            _currentAction = EditorAction::NONE;
            _isDragging = false;
        } else if (_callbacks.onEscape) {
            _callbacks.onEscape();
        }
    }
}

void InputHandler::handleKeyReleased(const sf::Event::KeyReleased& event) {
    // Currently no key release handling needed
}

void InputHandler::handleWindowResized(const sf::Event::Resized& event, sf::RenderWindow* window) {
    if (!window || !_editor) return;
    
    // Keep view size fixed to maintain consistent coordinate system
    // SFML will handle letterboxing/pillarboxing automatically
    sf::View view = window->getView();
    view.setSize({ View::DEFAULT_WIDTH, View::DEFAULT_HEIGHT });
    window->setView(view);
    
    spdlog::debug("InputHandler::handleWindowResized - Maintained fixed view size {:.1f}x{:.1f} (window: {}x{})", 
                 View::DEFAULT_WIDTH, View::DEFAULT_HEIGHT, event.size.x, event.size.y);
}

void InputHandler::setTilePlacementMode(bool enabled, int tileIndex, bool replaceMode) {
    _tilePlacementMode = enabled;
    _tilePlacementIndex = tileIndex;
    _tilePlacementReplaceMode = replaceMode;
}

InputHandler::SelectionModifier InputHandler::getSelectionModifier() const {
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl) || 
        sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl)) {
        return SelectionModifier::TOGGLE;
    } else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt) || 
               sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt)) {
        return SelectionModifier::ADD;
    } else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || 
               sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)) {
        return SelectionModifier::RANGE;
    }
    return SelectionModifier::NONE;
}

sf::Vector2f InputHandler::pixelToWorld(sf::Vector2i pixelPos, 
                                       sf::RenderWindow* window, 
                                       const sf::View& view) {
    return window->mapPixelToCoords(pixelPos, view);
}

bool InputHandler::isShiftPressed() const {
    return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || 
           sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);
}

} // namespace geck