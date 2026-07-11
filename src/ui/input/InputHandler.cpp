#include "InputHandler.h"
#include "util/Constants.h"
#include "util/ExitGridDirection.h"
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

    if (_mode == EditorMode::PluginTool && _callbacks.onToolMousePressed
        && _callbacks.onToolMousePressed(worldPos, event.button)) {
        return;
    }
    if (_mode == EditorMode::PluginTool && event.button == sf::Mouse::Button::Right) {
        if (_callbacks.onEscape) {
            _callbacks.onEscape();
        }
        return;
    }

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
            // "Draw edge": a click appends a vertex, a double-click finalizes. Double-click detection
            // uses the RAW cursor (physical mouse stillness); with Shift held the appended vertex is
            // SNAPPED to the nearest clean angle.
            const sf::Vector2f delta = worldPos - _dragStartWorldPos;
            const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            const bool isDoubleClick = _lineVertices.size() >= 2 && _doubleClickClock.getElapsedTime().asSeconds() < kDoubleClickSeconds && distance < kDoubleClickWorldDistance;
            _doubleClickClock.restart();
            _dragStartWorldPos = worldPos;

            if (isDoubleClick) {
                finalizeExitGridLine();
            } else {
                // Append the (Shift-snapped) vertex. The first STARTS a fresh edge; every vertex from
                // the 2nd on CLOSES a segment, which the host freezes at the current flip.
                const sf::Vector2f vertex = maybeSnapMarkExitsCursor(worldPos);
                if (_lineVertices.empty()) {
                    if (_callbacks.onMarkExitsLineReset) {
                        _callbacks.onMarkExitsLineReset();
                    }
                } else if (_callbacks.onMarkExitsSegmentCommitted) {
                    _callbacks.onMarkExitsSegmentCommitted(_lineVertices.back(), vertex, _markExitsFlip);
                }
                _lineVertices.push_back(vertex);
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
            // Right-click finalizes the line with enough vertices, otherwise abandons it and drops the tool.
            if (_lineVertices.size() >= 2) {
                finalizeExitGridLine();
            } else {
                cancelExitGridLine();
            }
            spdlog::debug("Mark exits mode right-click ({} vertices)", _lineVertices.size());
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

    if (_mode == EditorMode::PluginTool && _callbacks.onToolMouseReleased
        && _callbacks.onToolMouseReleased(worldPos, event.button)) {
        _currentAction = EditorAction::NONE;
        _isDragging = false;
        return;
    }

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
                    finishDragSelectRelease(worldPos);
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
    _mouseLastWorldPos = worldPos;

    if (_callbacks.onMouseMove) {
        _callbacks.onMouseMove(worldPos);
    }

    // "Draw edge": preview the polyline + on-line hexes from the committed vertices to the live cursor
    // on every move, independent of any drag. With Shift held, the live cursor snaps to a clean angle.
    if (_mode == EditorMode::MarkExits && _callbacks.onMarkExitsLinePreview) {
        _callbacks.onMarkExitsLinePreview(_lineVertices, maybeSnapMarkExitsCursor(worldPos), _markExitsFlip);
    }

    if (_mode == EditorMode::PluginTool && _callbacks.onToolMouseMoved && _callbacks.onToolMouseMoved(worldPos)) {
        return;
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
    if (_mode == EditorMode::PluginTool && _callbacks.onToolKeyPressed && _callbacks.onToolKeyPressed(event)) {
        return;
    }

    if (event.code == sf::Keyboard::Key::Escape) {
        if (_mode == EditorMode::MarkExits) {
            // Esc abandons the in-progress "Draw edge" line and drops the tool.
            cancelExitGridLine();
        } else if (_currentAction == EditorAction::OBJECT_MOVING && _callbacks.onObjectDragCancel) {
            _callbacks.onObjectDragCancel();
            _currentAction = EditorAction::NONE;
            _isDragging = false;
        } else if (_callbacks.onEscape) {
            _callbacks.onEscape();
        }
    } else if (event.code == sf::Keyboard::Key::Enter && _mode == EditorMode::MarkExits) {
        // Enter finalizes the "Draw edge" line when it has enough vertices.
        if (_lineVertices.size() >= 2) {
            finalizeExitGridLine();
        }
    } else if (event.code == sf::Keyboard::Key::Delete || event.code == sf::Keyboard::Key::Backspace) {
        if (_callbacks.onDeleteObjects) {
            _callbacks.onDeleteObjects();
        }
    } else if (event.code == sf::Keyboard::Key::R) {
        // The Rotate toolbar shortcut is disabled by the editor while stamping (and while a
        // registered tool runs), so R reaches us here: in stamp mode it cycles the pattern's
        // orientation variants. A registered tool sees R first via onToolKeyPressed above.
        if (_mode == EditorMode::StampPattern && _callbacks.onStampCycleVariant) {
            _callbacks.onStampCycleVariant();
        }
    } else if (event.code == sf::Keyboard::Key::P) {
        // Eyedropper: sample whatever is under the cursor. Key events carry no view/target to convert
        // pixels, so reuse the last cursor position tracked on mouse move (like the flip key).
        if (_callbacks.onPick) {
            _callbacks.onPick(_mouseLastWorldPos);
        }
    } else if (event.code == sf::Keyboard::Key::Space) {
        // In "Draw edge" mode, Space flips which side the edge's bars sit on, then re-fires the preview
        // at the last cursor so the flipped side is visible before finalising.
        if (_mode == EditorMode::MarkExits) {
            _markExitsFlip = !_markExitsFlip;
            if (_callbacks.onMarkExitsLinePreview) {
                _callbacks.onMarkExitsLinePreview(_lineVertices, maybeSnapMarkExitsCursor(_mouseLastWorldPos), _markExitsFlip);
            }
        }
    }
}

void InputHandler::handleKeyReleased(const sf::Event::KeyReleased&) {
    // Currently no key release handling needed
}

void InputHandler::finishDragSelectRelease(sf::Vector2f worldPos) {
    if (_selectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE && _callbacks.onScrollBlockerRectangle) {
        const float left = std::min(_dragStartWorldPos.x, worldPos.x);
        const float top = std::min(_dragStartWorldPos.y, worldPos.y);
        const float width = std::abs(worldPos.x - _dragStartWorldPos.x);
        const float height = std::abs(worldPos.y - _dragStartWorldPos.y);
        _callbacks.onScrollBlockerRectangle(sf::FloatRect({ left, top }, { width, height }));
    } else if (_callbacks.onDragSelection) {
        _callbacks.onDragSelection(_dragStartWorldPos, worldPos, _dragSelectionModifier);
    }
}

void InputHandler::finalizeExitGridLine() {
    // onMarkExitsLine places the FROZEN committed segments, so it must run BEFORE the reset that drops them.
    if (_lineVertices.size() >= 2 && _callbacks.onMarkExitsLine) {
        _callbacks.onMarkExitsLine(_lineVertices, _markExitsFlip);
    }
    _lineVertices.clear();
    if (_callbacks.onMarkExitsLineReset) {
        _callbacks.onMarkExitsLineReset(); // drop the frozen segments — the next edge starts fresh
    }
    // The tool stays active for another edge; repaint the now-cleared preview. The flip persists.
    if (_callbacks.onMarkExitsLinePreview) {
        _callbacks.onMarkExitsLinePreview(_lineVertices, _dragStartWorldPos, _markExitsFlip);
    }
}

void InputHandler::cancelExitGridLine() {
    _lineVertices.clear();
    if (_callbacks.onMarkExitsLineReset) {
        _callbacks.onMarkExitsLineReset(); // drop the frozen segments of the abandoned line
    }
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

sf::Vector2f InputHandler::maybeSnapMarkExitsCursor(sf::Vector2f cursor) const {
    // Snap only the LIVE segment, and only while Shift is held (the conventional "constrain to a clean
    // angle" modifier); with no committed vertex there is no segment to align, so leave the cursor raw.
    if (_mode != EditorMode::MarkExits || _lineVertices.empty() || !isShiftPressed()) {
        return cursor;
    }
    const sf::Vector2f last = _lineVertices.back();
    const auto [sx, sy] = snapToExitGridAngle(last.x, last.y, cursor.x, cursor.y);
    return sf::Vector2f{ static_cast<float>(sx), static_cast<float>(sy) };
}

} // namespace geck
