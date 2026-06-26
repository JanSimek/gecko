#include "InputHandler.h"
#include "util/Constants.h"
#include <SFML/System/Time.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>
#include <spdlog/spdlog.h>
#include <cmath>

namespace geck {

void InputHandler::handleEvent(const sf::Event& event,
    sf::RenderTarget& target,
    const sf::View& view) {
    if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        handleMousePressed(*mousePressed, target, view);
    } else if (const auto* mouseReleased = event.getIf<sf::Event::MouseButtonReleased>()) {
        handleMouseReleased(*mouseReleased, target, view);
    } else if (const auto* mouseMoved = event.getIf<sf::Event::MouseMoved>()) {
        handleMouseMoved(*mouseMoved, target, view);
    } else if (const auto* mouseWheelScrolled = event.getIf<sf::Event::MouseWheelScrolled>()) {
        handleMouseWheelScrolled(*mouseWheelScrolled);
    } else if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>()) {
        handleKeyPressed(*keyPressed);
    } else if (const auto* keyReleased = event.getIf<sf::Event::KeyReleased>()) {
        handleKeyReleased(*keyReleased);
    }
}

void InputHandler::handleMousePressed(const sf::Event::MouseButtonPressed& event,
    sf::RenderTarget& target,
    const sf::View& view) {
    sf::Vector2f worldPos = pixelToWorld(event.position, target, view);

    if (event.button == sf::Mouse::Button::Left) {
        if (_mode == EditorMode::SetPlayerPosition) {
            if (_callbacks.onPlayerPositionSelect) {
                _callbacks.onPlayerPositionSelect(worldPos);
            }
            _mode = EditorMode::Select; // Exit mode after selection
            return;
        }

        if (_mode == EditorMode::PlaceTile && _tilePlacementIndex >= 0) {
            _currentAction = EditorAction::TILE_PLACING;
            _dragStartWorldPos = worldPos;
            _isDragging = false;
            _tilePlacementIsRoof = isShiftPressed();
            return;
        }

        if (_mode == EditorMode::PlaceExitGrid) {
            if (_callbacks.onExitGridPlacement) {
                _callbacks.onExitGridPlacement(worldPos);
            }
            return;
        }

        if (_mode == EditorMode::StampPattern) {
            if (_callbacks.onStampPattern) {
                _callbacks.onStampPattern(worldPos);
            }
            return;
        }

        if (_mode == EditorMode::MarkExits) {
            // "Draw region": a click appends a polygon vertex; a double-click finalizes it. The
            // double-click's two presses both arrive here, so the second one both appends the final
            // vertex (the first of the pair was already appended) and then finalizes.
            const sf::Vector2f delta = worldPos - _dragStartWorldPos;
            const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            const bool isDoubleClick = _polygonVertices.size() >= 2 && _doubleClickClock.getElapsedTime().asSeconds() < kDoubleClickSeconds && distance < kDoubleClickWorldDistance;
            _doubleClickClock.restart();
            _dragStartWorldPos = worldPos;

            if (isDoubleClick) {
                finalizeExitGridPolygon();
            } else {
                _polygonVertices.push_back(worldPos);
            }
            return;
        }

        SelectionModifier modifier = getSelectionModifier();
        bool hasModifiers = (modifier != SelectionModifier::NONE);
        bool canDragObject = !hasModifiers && _callbacks.canStartObjectDrag && _callbacks.canStartObjectDrag(worldPos);

        if (canDragObject && _callbacks.onObjectDragStart) {
            if (_callbacks.onObjectDragStart(worldPos)) {
                _currentAction = EditorAction::OBJECT_MOVING;
                _dragStartWorldPos = worldPos;
                _isDragging = false;
            }
            return;
        }
        // Plain drag-select replaces the selection; Ctrl (TOGGLE) drag removes the covered
        // items; Alt (ADD) drag adds them. A no-drag release falls back to a single click.
        const bool modifierAllowsDragSelect = (modifier == SelectionModifier::NONE || modifier == SelectionModifier::TOGGLE || modifier == SelectionModifier::ADD);
        bool canDragSelect = modifierAllowsDragSelect && (_selectionMode == SelectionMode::ALL || _selectionMode == SelectionMode::FLOOR_TILES || _selectionMode == SelectionMode::ROOF_TILES || _selectionMode == SelectionMode::ROOF_TILES_ALL || _selectionMode == SelectionMode::OBJECTS || _selectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE);

        if (canDragSelect) {
            _currentAction = EditorAction::DRAG_SELECTING;
            _dragStartWorldPos = worldPos;
            _isDragging = false;
            _immediateSelectionPerformed = false;
            _dragSelectionModifier = modifier;
        } else {
            if (_callbacks.onSelectionClick) {
                _callbacks.onSelectionClick(worldPos, modifier);
            }
            _immediateSelectionPerformed = true;
        }
    } else if (event.button == sf::Mouse::Button::Right) {
        if (_mode == EditorMode::PlaceTile) {
            _mode = EditorMode::Select;
            _tilePlacementIndex = -1;
            if (_callbacks.onTilePlacementCancel) {
                _callbacks.onTilePlacementCancel();
            }
            spdlog::debug("Tile placement mode cancelled with right-click");
        } else if (_mode == EditorMode::PlaceExitGrid) {
            _mode = EditorMode::Select;
            spdlog::debug("Exit grid placement mode cancelled with right-click");
        } else if (_mode == EditorMode::MarkExits) {
            // Right-click finalizes the polygon when there are enough vertices, otherwise it
            // abandons the in-progress polygon and drops the tool.
            if (_polygonVertices.size() >= 3) {
                finalizeExitGridPolygon();
            } else {
                cancelExitGridPolygon();
            }
            spdlog::debug("Mark exits mode right-click ({} vertices)", _polygonVertices.size());
        } else if (_mode == EditorMode::StampPattern) {
            _mode = EditorMode::Select;
            if (_callbacks.onStampPatternCancel) {
                _callbacks.onStampPatternCancel();
            }
            spdlog::debug("Stamp pattern mode cancelled with right-click");
        } else {
            _currentAction = EditorAction::PANNING;
            _mouseStartPos = event.position;
            _mouseLastPos = _mouseStartPos;
        }
    }
}

void InputHandler::handleMouseReleased(const sf::Event::MouseButtonReleased& event,
    sf::RenderTarget& target,
    const sf::View& view) {
    sf::Vector2f worldPos = pixelToWorld(event.position, target, view);

    if (event.button == sf::Mouse::Button::Left) {
        if (_mode == EditorMode::SetPlayerPosition) {
            return;
        }

        switch (_currentAction) {
            case EditorAction::TILE_PLACING:
                if (_isDragging && _mode == EditorMode::PlaceTile && _callbacks.onTileAreaFill) {
                    _callbacks.onTileAreaFill(_dragStartWorldPos, worldPos, _tilePlacementIsRoof);
                } else if (_mode == EditorMode::PlaceTile && _tilePlacementIndex >= 0 && _callbacks.onTilePlacement) {
                    _callbacks.onTilePlacement(worldPos);
                }
                break;

            case EditorAction::DRAG_SELECTING:
                if (_isDragging) {
                    if (_selectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE && _callbacks.onScrollBlockerRectangle) {
                        float left = std::min(_dragStartWorldPos.x, worldPos.x);
                        float top = std::min(_dragStartWorldPos.y, worldPos.y);
                        float width = std::abs(worldPos.x - _dragStartWorldPos.x);
                        float height = std::abs(worldPos.y - _dragStartWorldPos.y);
                        sf::FloatRect area({ left, top }, { width, height });
                        _callbacks.onScrollBlockerRectangle(area);
                    } else if (_callbacks.onDragSelection) {
                        _callbacks.onDragSelection(_dragStartWorldPos, worldPos, _dragSelectionModifier);
                    }
                } else if (!_immediateSelectionPerformed && _callbacks.onSelectionClick) {
                    // A no-drag release on a Ctrl drag is a Ctrl+click, so pass the modifier.
                    _callbacks.onSelectionClick(worldPos, _dragSelectionModifier);
                }
                break;

            case EditorAction::OBJECT_MOVING:
                if (_isDragging) {
                    if (_callbacks.onObjectDragEnd) {
                        _callbacks.onObjectDragEnd(worldPos);
                    }
                } else {
                    // No movement: this was a click on an already-selected object, not a drag.
                    // Cancel the (no-op) move and cycle the selection to the item underneath.
                    if (_callbacks.onObjectDragCancel) {
                        _callbacks.onObjectDragCancel();
                    }
                    if (_callbacks.onSelectionClick) {
                        _callbacks.onSelectionClick(worldPos, SelectionModifier::NONE);
                    }
                }
                break;

            default:
                break;
        }

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
    sf::RenderTarget& target,
    const sf::View& view) {
    sf::Vector2f worldPos = pixelToWorld(event.position, target, view);

    if (_callbacks.onMouseMove) {
        _callbacks.onMouseMove(worldPos);
    }

    // "Draw region": preview the outline + interior hexes from the committed vertices to the live
    // cursor on every move, independent of any drag action.
    if (_mode == EditorMode::MarkExits && _callbacks.onMarkExitsPolygonPreview) {
        _callbacks.onMarkExitsPolygonPreview(_polygonVertices, worldPos);
    }

    switch (_currentAction) {
        case EditorAction::PANNING: {
            sf::Vector2i delta = event.position - _mouseLastPos;
            if (_callbacks.onPan) {
                _callbacks.onPan(sf::Vector2f(static_cast<float>(-delta.x),
                    static_cast<float>(-delta.y)));
            }
            _mouseLastPos = event.position;
            break;
        }

        case EditorAction::DRAG_SELECTING:
            if (!_isDragging) {
                sf::Vector2f dragDelta = worldPos - _dragStartWorldPos;
                float dragDistance = std::sqrt(dragDelta.x * dragDelta.x + dragDelta.y * dragDelta.y);
                if (dragDistance > 5.0f) { // 5 pixel drag threshold
                    _isDragging = true;
                }
            }
            if (_isDragging && _callbacks.onDragSelectionPreview) {
                _callbacks.onDragSelectionPreview(_dragStartWorldPos, worldPos, _dragSelectionModifier);
            }
            break;

        case EditorAction::TILE_PLACING:
            if (!_isDragging) {
                sf::Vector2f dragDelta = worldPos - _dragStartWorldPos;
                float dragDistance = std::sqrt(dragDelta.x * dragDelta.x + dragDelta.y * dragDelta.y);
                if (dragDistance > 5.0f) { // 5 pixel drag threshold
                    _isDragging = true;
                }
            }
            if (_isDragging && _callbacks.onDragSelectionPreview) {
                _callbacks.onDragSelectionPreview(_dragStartWorldPos, worldPos, SelectionModifier::NONE);
            }
            break;

        case EditorAction::OBJECT_MOVING:
            if (!_isDragging) {
                sf::Vector2f dragDelta = worldPos - _dragStartWorldPos;
                float dragDistance = std::sqrt(dragDelta.x * dragDelta.x + dragDelta.y * dragDelta.y);
                if (dragDistance > 5.0f) { // 5 pixel drag threshold (matches drag-select)
                    _isDragging = true;
                }
            }
            if (_isDragging && _callbacks.onObjectDragUpdate) {
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
        if (_mode == EditorMode::MarkExits) {
            // Esc abandons the in-progress "Draw region" polygon and drops the tool.
            cancelExitGridPolygon();
        } else if (_currentAction == EditorAction::OBJECT_MOVING && _callbacks.onObjectDragCancel) {
            _callbacks.onObjectDragCancel();
            _currentAction = EditorAction::NONE;
            _isDragging = false;
        } else if (_callbacks.onEscape) {
            _callbacks.onEscape();
        }
    } else if (event.code == sf::Keyboard::Key::Enter && _mode == EditorMode::MarkExits) {
        // Enter finalizes the "Draw region" polygon when it has enough vertices.
        if (_polygonVertices.size() >= 3) {
            finalizeExitGridPolygon();
        }
    } else if (event.code == sf::Keyboard::Key::Delete || event.code == sf::Keyboard::Key::Backspace) {
        if (_callbacks.onDeleteObjects) {
            _callbacks.onDeleteObjects();
        }
    } else if (event.code == sf::Keyboard::Key::R) {
        // In stamp mode, R cycles the pattern's orientation variants (the Rotate toolbar
        // shortcut is disabled by the editor while stamping, so the key reaches us here).
        if (_mode == EditorMode::StampPattern && _callbacks.onStampCycleVariant) {
            _callbacks.onStampCycleVariant();
        }
    }
}

void InputHandler::handleKeyReleased(const sf::Event::KeyReleased&) {
    // Currently no key release handling needed
}

void InputHandler::finalizeExitGridPolygon() {
    if (_polygonVertices.size() >= 3 && _callbacks.onMarkExitsPolygon) {
        _callbacks.onMarkExitsPolygon(_polygonVertices);
    }
    _polygonVertices.clear();
    // The tool stays active so the user can immediately draw another region; the caller
    // (EditorWidget) repaints the now-cleared preview.
    if (_callbacks.onMarkExitsPolygonPreview) {
        _callbacks.onMarkExitsPolygonPreview(_polygonVertices, _dragStartWorldPos);
    }
}

void InputHandler::cancelExitGridPolygon() {
    _polygonVertices.clear();
    _mode = EditorMode::Select;
    if (_callbacks.onMarkExitsModeCancelled) {
        _callbacks.onMarkExitsModeCancelled();
    }
}

void InputHandler::setTilePlacementMode(bool enabled, int tileIndex, bool replaceMode) {
    setActiveMode(enabled, EditorMode::PlaceTile);
    _tilePlacementIndex = tileIndex;
    _tilePlacementReplaceMode = replaceMode;
}

InputHandler::SelectionModifier InputHandler::getSelectionModifier() const {
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl)) {
        return SelectionModifier::TOGGLE;
    } else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt)) {
        return SelectionModifier::ADD;
    } else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)) {
        return SelectionModifier::RANGE;
    }
    return SelectionModifier::NONE;
}

sf::Vector2f InputHandler::pixelToWorld(sf::Vector2i pixelPos,
    sf::RenderTarget& target,
    const sf::View& view) {
    return target.mapPixelToCoords(pixelPos, view);
}

bool InputHandler::isShiftPressed() const {
    return sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);
}

} // namespace geck
