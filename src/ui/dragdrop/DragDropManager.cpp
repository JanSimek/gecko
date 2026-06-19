#include "DragDropManager.h"
#include "DragDropContext.h"
#include "viewport/ViewportController.h"
#include "ui/UIConstants.h"
#include "editor/Object.h"
#include "editor/HexagonGrid.h"
#include "resource/GameResources.h"
#include "format/frm/Frm.h"
#include "format/map/MapObject.h"
#include "util/Constants.h"
#include "selection/SelectionManager.h"
#include "selection/SelectionState.h"
#include "ui/panels/ObjectPalettePanel.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace geck {

DragDropManager::DragDropManager(DragDropContext& context,
    std::function<const ObjectInfo*(int objectIndex, ObjectCategory category)> lookupObjectInfo)
    : _context(context)
    , _lookupObjectInfo(std::move(lookupObjectInfo)) {
}

bool DragDropManager::canStartObjectDrag(sf::Vector2f worldPos) const {
    // A move can be grabbed from any visible part of the selection — a selected object, or (so a
    // region stays movable when its objects are on hidden layers) a selected floor/roof tile.
    // SelectionManager owns that visibility-aware hit test; both selection and drag go through it.
    return _context.getSelectionManager()->isPointOnSelection(worldPos);
}

bool DragDropManager::startObjectDrag(sf::Vector2f worldPos) {
    if (!canStartObjectDrag(worldPos)) {
        return false;
    }

    const auto& selection = _context.getSelectionManager()->getCurrentSelection();
    _draggedObjects.clear();
    _objectDragStartPositions.clear();
    _objectDragStartColors.clear();

    for (const auto& item : selection.items) {
        if (item.type == selection::SelectionType::OBJECT) {
            auto object = item.getObject();
            if (object) {
                _draggedObjects.push_back(object);

                // Store original position/color so the drag can be cancelled or finished cleanly.
                _objectDragStartPositions.push_back(object->getSprite().getPosition());
                _objectDragStartColors.push_back(object->getSprite().getColor());
            }
        }
    }

    // No selected objects is fine for a tiles-only grab (or one whose objects are all hidden): the
    // selected floor/roof tiles still move. Only bail if there's nothing under the cursor to grab.
    if (_draggedObjects.empty() && !_context.getSelectionManager()->isPointOnSelection(worldPos)) {
        return false;
    }

    _isDraggingObjects = true;
    _dragStartWorldPos = worldPos;
    _objectDragOffset = sf::Vector2f(0, 0);

    // Preview the selected floor/roof tiles moving with the objects.
    _context.beginTileDragPreview();

    // Ensures the selection rectangle doesn't remain visible during object drag. This repaints the
    // selection visuals (resetting object sprite colours), so the ghost tint below must come after.
    _context.clearDragSelectionPreview();

    // Show the dragged objects as a translucent ghost (same alpha as the palette drag preview),
    // so it's clear they sit at a preview position until dropped.
    for (const auto& object : _draggedObjects) {
        object->getSprite().setColor(sf::Color(255, 255, 255, ui::constants::sfml::DRAG_PREVIEW_ALPHA));
    }

    spdlog::debug("DragDropManager: Started dragging {} objects", _draggedObjects.size());
    return true;
}

void DragDropManager::updateObjectDrag(sf::Vector2f currentWorldPos) {
    // A tiles-only drag has no dragged objects but still moves the selected tiles, so key the
    // guard off the drag being active rather than off there being objects.
    if (!_isDraggingObjects) {
        return;
    }

    _objectDragOffset = currentWorldPos - _dragStartWorldPos;

    // Apply visual offset to dragged objects (preview)
    for (size_t i = 0; i < _draggedObjects.size(); ++i) {
        sf::Vector2f newPos = _objectDragStartPositions[i] + _objectDragOffset;
        _draggedObjects[i]->getSprite().setPosition(newPos);
    }

    // Preview the selected floor/roof tiles moving by the same offset.
    _context.previewTileDrag(_objectDragOffset);

    // Highlight target hex for the first (representative) object
    if (!_draggedObjects.empty()) {
        auto& mapObject = _draggedObjects[0]->getMapObject();
        int originalHexPosition = mapObject.position;

        const auto* hexGrid = _context.getHexagonGrid();
        if (auto originalHex = hexGrid->getHexByPosition(static_cast<uint32_t>(originalHexPosition)); originalHex.has_value()) {
            sf::Vector2f originalWorldPos(static_cast<float>(originalHex->get().x()), static_cast<float>(originalHex->get().y()));

            sf::Vector2f newWorldPos = originalWorldPos + _objectDragOffset;
            sf::Vector2f snappedPos = _context.getViewportController()->snapToHexGrid(newWorldPos);
            int targetHexPosition = _context.getViewportController()->worldPosToHexIndex(snappedPos);

            if (targetHexPosition >= 0) {
                _context.getCurrentHoverHex() = targetHexPosition;
            }
        }
    }
}

void DragDropManager::finishObjectDrag(sf::Vector2f finalWorldPos) {
    // A tiles-only drag has no dragged objects but still moves the selected tiles, so key the
    // guard off the drag being active rather than off there being objects.
    if (!_isDraggingObjects) {
        return;
    }

    spdlog::debug("DragDropManager::finishObjectDrag - finalWorldPos({:.1f}, {:.1f})", finalWorldPos.x, finalWorldPos.y);

    sf::Vector2f dragOffset = finalWorldPos - _dragStartWorldPos;
    spdlog::debug("DragDropManager::finishObjectDrag - dragOffset({:.1f}, {:.1f})", dragOffset.x, dragOffset.y);

    // For a region drag (tiles selected) align the whole move to the tile grid, so the objects and
    // tiles travel together and the tiles never land off-centre. A free object drag (no tiles
    // selected) keeps the finer per-hex offset.
    sf::Vector2f moveTranslation = dragOffset;
    if (const auto* selectionManager = _context.getSelectionManager()) {
        if (const auto aligned = selectionManager->tileAlignedTranslation(dragOffset)) {
            moveTranslation = *aligned;
        }
    }

    const auto* hexGrid = _context.getHexagonGrid();

    // Move each object by the relative offset to maintain spacing
    std::vector<std::pair<int, int>> movedObjects; // originalHex -> newHex
    movedObjects.reserve(_draggedObjects.size());
    for (size_t i = 0; i < _draggedObjects.size(); ++i) {
        auto& mapObject = _draggedObjects[i]->getMapObject();
        int originalHexPosition = mapObject.position;

        if (auto originalHex = hexGrid->getHexByPosition(static_cast<uint32_t>(originalHexPosition)); originalHex.has_value()) {
            sf::Vector2f originalWorldPos(static_cast<float>(originalHex->get().x()), static_cast<float>(originalHex->get().y()));

            sf::Vector2f newWorldPos = originalWorldPos + moveTranslation;

            // Don't manually apply shift offsets here - setHexPosition() will handle them
            sf::Vector2f snappedPos = _context.getViewportController()->snapToHexGrid(newWorldPos);
            int newHexPosition = _context.getViewportController()->worldPosToHexIndex(snappedPos);

            spdlog::debug("DragDropManager: Object {} - original({:.1f},{:.1f}) + offset({:.1f},{:.1f}) = new({:.1f},{:.1f}) -> hex {}",
                i, originalWorldPos.x, originalWorldPos.y, dragOffset.x, dragOffset.y,
                snappedPos.x, snappedPos.y, newHexPosition);

            if (hexGrid->containsPosition(newHexPosition)) {
                movedObjects.push_back({ originalHexPosition, newHexPosition });
                continue;
            }
            _draggedObjects[i]->getSprite().setPosition(_objectDragStartPositions[i]);
            spdlog::warn("DragDropManager: Invalid drop position hex {}, restored original position", newHexPosition);
            continue;
        }
        _draggedObjects[i]->getSprite().setPosition(_objectDragStartPositions[i]);
        spdlog::warn("DragDropManager: Invalid original hex position {}, restored original position", originalHexPosition);
    }

    // Restore the previewed tile sprites; the committed move repositions them for real below.
    _context.endTileDragPreview();

    // The object move and the tile move from one drag collapse into a single undo entry.
    _context.beginMoveBatch("Move Selection");
    if (!movedObjects.empty()) {
        _context.registerObjectMove(_draggedObjects, movedObjects);
    }
    // Move the selected floor/roof tiles by the same translation so the whole region travels together.
    _context.moveSelectedTilesForDrag(moveTranslation);
    _context.endMoveBatch();

    // Keep the selection on the moved content: the move rebuilt the object sprites and shifted the
    // tiles, so the old selection is stale and would otherwise sit at the original spot.
    _context.reselectAfterDragMove(moveTranslation);

    // End the ghost preview: restore each object's pre-drag (selection) tint.
    for (size_t i = 0; i < _draggedObjects.size(); ++i) {
        _draggedObjects[i]->getSprite().setColor(_objectDragStartColors[i]);
    }

    _isDraggingObjects = false;
    _draggedObjects.clear();
    _objectDragStartPositions.clear();
    _objectDragStartColors.clear();
    _objectDragOffset = sf::Vector2f(0, 0);

    _context.getCurrentHoverHex() = -1;

    spdlog::debug("DragDropManager: Finished object drag operation");
}

void DragDropManager::cancelObjectDrag() {
    // A tiles-only drag has no dragged objects but still moves the selected tiles, so key the
    // guard off the drag being active rather than off there being objects.
    if (!_isDraggingObjects) {
        return;
    }

    // Restore the previewed tile sprites to their original positions.
    _context.endTileDragPreview();

    // Restore original hex positions and the pre-drag (selection) tint.
    for (size_t i = 0; i < _draggedObjects.size(); ++i) {
        _draggedObjects[i]->getSprite().setPosition(_objectDragStartPositions[i]);
        _draggedObjects[i]->getSprite().setColor(_objectDragStartColors[i]);
    }

    _isDraggingObjects = false;
    _draggedObjects.clear();
    _objectDragStartPositions.clear();
    _objectDragStartColors.clear();
    _objectDragOffset = sf::Vector2f(0, 0);

    _context.getCurrentHoverHex() = -1;

    spdlog::debug("DragDropManager: Cancelled object drag operation");
}

void DragDropManager::startDragPreview(int objectIndex, int categoryInt, sf::Vector2f worldPos) {
    cancelDragPreview();

    _isDraggingFromPalette = true;
    _previewObjectIndex = objectIndex;
    _previewObjectCategory = categoryInt;

    try {
        _previewObjectInfo = _lookupObjectInfo(objectIndex, static_cast<ObjectCategory>(categoryInt));

        if (_previewObjectInfo) {
            try {
                std::string frmPath = _previewObjectInfo->frmPath.toStdString();
                auto frm = _context.resources().repository().find<Frm>(frmPath);
                if (!frm) {
                    frm = _context.resources().repository().load<Frm>(frmPath);
                }

                if (frm) {
                    _dragPreviewObject = std::make_shared<Object>(frm);
                    sf::Sprite previewSprite{ _context.resources().textures().get(frmPath) };
                    _dragPreviewObject->setSprite(std::move(previewSprite));
                    _dragPreviewObject->setDirection(ObjectDirection(0)); // Single frame for preview
                    // Semi-transparent so the underlying map stays visible
                    auto& spriteRef = _dragPreviewObject->getSprite();
                    spriteRef.setColor(sf::Color(255, 255, 255, ui::constants::sfml::DRAG_PREVIEW_ALPHA));

                    spdlog::debug("DragDropManager: Created drag preview for object {}", objectIndex);
                    return;
                }
                spdlog::warn("DragDropManager: Failed to load FRM for drag preview");
                cancelDragPreview();
                return;
            } catch (const std::exception& e) {
                spdlog::warn("DragDropManager: Failed to load FRM {}: {}", _previewObjectInfo->frmPath.toStdString(), e.what());
                cancelDragPreview();
                return;
            }
        }
        spdlog::warn("DragDropManager: No ObjectInfo available for drag preview");
        cancelDragPreview();

        updateDragPreview(worldPos);

    } catch (const std::exception& e) {
        spdlog::warn("DragDropManager: Failed to create drag preview: {}", e.what());
        cancelDragPreview();
    }
}

void DragDropManager::updateDragPreview(sf::Vector2f worldPos) {
    if (!_isDraggingFromPalette || !_dragPreviewObject) {
        return;
    }

    int hexPosition = _context.getViewportController()->worldPosToHexIndex(worldPos);
    if (hexPosition >= 0) {
        const auto* hexGrid = _context.getHexagonGrid();
        if (auto hex = hexGrid->getHexByPosition(static_cast<uint32_t>(hexPosition)); hex.has_value()) {
            _dragPreviewObject->setHexPosition(hex->get());
        }
    }
}

void DragDropManager::finishDragPreview(sf::Vector2f worldPos) {
    if (!_isDraggingFromPalette) {
        return;
    }

    int hexPosition = _context.getViewportController()->worldPosToHexIndex(worldPos);
    if (hexPosition >= 0) {
        _context.placeObjectAtPosition(worldPos);
        spdlog::debug("DragDropManager: Placed object from palette at hex {}", hexPosition);
    } else {
        spdlog::warn("DragDropManager: Invalid drop position for palette object");
    }

    cancelDragPreview();
}

void DragDropManager::cancelDragPreview() {
    _isDraggingFromPalette = false;
    _dragPreviewObject.reset();
    _previewObjectIndex = -1;
    _previewObjectCategory = 0;
    _previewObjectInfo = nullptr;
    _context.getCurrentHoverHex() = -1;
}

DragDropManager::DragType DragDropManager::getCurrentDragType() const {
    if (_isDraggingObjects)
        return DragType::OBJECT_MOVE;
    if (_isDraggingFromPalette)
        return DragType::PALETTE_PREVIEW;
    return DragType::NONE;
}

} // namespace geck
