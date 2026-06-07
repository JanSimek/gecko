#include "DragDropManager.h"
#include "../core/EditorWidget.h"
#include "../viewport/ViewportController.h"
#include "../UIConstants.h"
#include "../../editor/Object.h"
#include "../../editor/HexagonGrid.h"
#include "../../resource/GameResources.h"
#include "../../format/frm/Frm.h"
#include "../../format/map/MapObject.h"
#include "../../util/Constants.h"
#include "../../selection/SelectionState.h"
#include "../panels/ObjectPalettePanel.h"
#include "../core/MainWindow.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace geck {

DragDropManager::DragDropManager(EditorWidget* editor)
    : _editor(editor) {
}

bool DragDropManager::canStartObjectDrag(sf::Vector2f worldPos) const {
    if (!_editor)
        return false;

    const auto& selection = _editor->getSelectionManager()->getCurrentSelection();

    spdlog::debug("DragDropManager::canStartObjectDrag - worldPos({:.1f}, {:.1f}), selection has {} items",
        worldPos.x, worldPos.y, selection.items.size());

    auto objectsAtPos = _editor->getObjectsAtPosition(worldPos);

    for (const auto& item : selection.items) {
        if (item.type == selection::SelectionType::OBJECT) {
            auto selectedObject = item.getObject();
            if (selectedObject && selectedObject->hasMapObject()) {
                for (const auto& objectAtPos : objectsAtPos) {
                    if (selectedObject == objectAtPos) {
                        spdlog::debug("DragDropManager::canStartObjectDrag - found selected object at clicked position");
                        return true;
                    }
                }
            }
        }
    }

    spdlog::debug("DragDropManager::canStartObjectDrag - no selected objects found at clicked position");
    return false;
}

bool DragDropManager::startObjectDrag(sf::Vector2f worldPos) {
    if (!canStartObjectDrag(worldPos)) {
        return false;
    }

    const auto& selection = _editor->getSelectionManager()->getCurrentSelection();
    _draggedObjects.clear();
    _objectDragStartPositions.clear();

    for (const auto& item : selection.items) {
        if (item.type == selection::SelectionType::OBJECT) {
            auto object = item.getObject();
            if (object) {
                _draggedObjects.push_back(object);

                // Store original position for potential cancel
                sf::Vector2f originalPos = object->getSprite().getPosition();
                _objectDragStartPositions.push_back(originalPos);
            }
        }
    }

    if (_draggedObjects.empty()) {
        return false;
    }

    _isDraggingObjects = true;
    _dragStartWorldPos = worldPos;
    _objectDragOffset = sf::Vector2f(0, 0);

    // Ensures the selection rectangle doesn't remain visible during object drag
    _editor->clearDragSelectionPreview();

    spdlog::debug("DragDropManager: Started dragging {} objects", _draggedObjects.size());
    return true;
}

void DragDropManager::updateObjectDrag(sf::Vector2f currentWorldPos) {
    if (!_isDraggingObjects || _draggedObjects.empty()) {
        return;
    }

    _objectDragOffset = currentWorldPos - _dragStartWorldPos;

    // Apply visual offset to dragged objects (preview)
    for (size_t i = 0; i < _draggedObjects.size(); ++i) {
        sf::Vector2f newPos = _objectDragStartPositions[i] + _objectDragOffset;
        _draggedObjects[i]->getSprite().setPosition(newPos);
    }

    // Highlight target hex for the first (representative) object
    if (!_draggedObjects.empty()) {
        auto& mapObject = _draggedObjects[0]->getMapObject();
        int originalHexPosition = mapObject.position;

        const auto* hexGrid = _editor->getHexagonGrid();
        if (auto originalHex = hexGrid->getHexByPosition(static_cast<uint32_t>(originalHexPosition)); originalHex.has_value()) {
            sf::Vector2f originalWorldPos(static_cast<float>(originalHex->get().x()), static_cast<float>(originalHex->get().y()));

            sf::Vector2f newWorldPos = originalWorldPos + _objectDragOffset;
            sf::Vector2f snappedPos = _editor->getViewportController()->snapToHexGrid(newWorldPos);
            int targetHexPosition = _editor->getViewportController()->worldPosToHexIndex(snappedPos);

            if (targetHexPosition >= 0) {
                _editor->getCurrentHoverHex() = targetHexPosition;
            }
        }
    }
}

void DragDropManager::finishObjectDrag(sf::Vector2f finalWorldPos) {
    if (!_isDraggingObjects || _draggedObjects.empty()) {
        return;
    }

    spdlog::debug("DragDropManager::finishObjectDrag - finalWorldPos({:.1f}, {:.1f})", finalWorldPos.x, finalWorldPos.y);

    sf::Vector2f dragOffset = finalWorldPos - _dragStartWorldPos;
    spdlog::debug("DragDropManager::finishObjectDrag - dragOffset({:.1f}, {:.1f})", dragOffset.x, dragOffset.y);

    const auto* hexGrid = _editor->getHexagonGrid();

    // Move each object by the relative offset to maintain spacing
    std::vector<std::pair<int, int>> movedObjects; // originalHex -> newHex
    movedObjects.reserve(_draggedObjects.size());
    for (size_t i = 0; i < _draggedObjects.size(); ++i) {
        auto& mapObject = _draggedObjects[i]->getMapObject();
        int originalHexPosition = mapObject.position;

        if (auto originalHex = hexGrid->getHexByPosition(static_cast<uint32_t>(originalHexPosition)); originalHex.has_value()) {
            sf::Vector2f originalWorldPos(static_cast<float>(originalHex->get().x()), static_cast<float>(originalHex->get().y()));

            sf::Vector2f newWorldPos = originalWorldPos + dragOffset;

            // Don't manually apply shift offsets here - setHexPosition() will handle them
            sf::Vector2f snappedPos = _editor->getViewportController()->snapToHexGrid(newWorldPos);
            int newHexPosition = _editor->getViewportController()->worldPosToHexIndex(snappedPos);

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

    if (!movedObjects.empty()) {
        _editor->registerObjectMove(_draggedObjects, movedObjects);
    }

    _isDraggingObjects = false;
    _draggedObjects.clear();
    _objectDragStartPositions.clear();
    _objectDragOffset = sf::Vector2f(0, 0);

    _editor->getCurrentHoverHex() = -1;

    spdlog::debug("DragDropManager: Finished object drag operation");
}

void DragDropManager::cancelObjectDrag() {
    if (!_isDraggingObjects || _draggedObjects.empty()) {
        return;
    }

    // Restore original hex positions
    for (size_t i = 0; i < _draggedObjects.size(); ++i) {
        _draggedObjects[i]->getSprite().setPosition(_objectDragStartPositions[i]);
    }

    _isDraggingObjects = false;
    _draggedObjects.clear();
    _objectDragStartPositions.clear();
    _objectDragOffset = sf::Vector2f(0, 0);

    _editor->getCurrentHoverHex() = -1;

    spdlog::debug("DragDropManager: Cancelled object drag operation");
}

void DragDropManager::startDragPreview(int objectIndex, int categoryInt, sf::Vector2f worldPos) {
    cancelDragPreview();

    _isDraggingFromPalette = true;
    _previewObjectIndex = objectIndex;
    _previewObjectCategory = categoryInt;

    try {
        auto mainWindow = _editor->getMainWindow();
        if (!mainWindow) {
            spdlog::warn("DragDropManager: No MainWindow reference for drag preview");
            cancelDragPreview();
            return;
        }

        auto palette = mainWindow->getObjectPalettePanel();
        if (!palette) {
            spdlog::warn("DragDropManager: No ObjectPalettePanel for drag preview");
            cancelDragPreview();
            return;
        }

        _previewObjectInfo = palette->getObjectInfo(objectIndex, static_cast<ObjectCategory>(categoryInt));

        if (_previewObjectInfo) {
            try {
                std::string frmPath = _previewObjectInfo->frmPath.toStdString();
                auto frm = _editor->resources().repository().find<Frm>(frmPath);
                if (!frm) {
                    frm = _editor->resources().repository().load<Frm>(frmPath);
                }

                if (frm) {
                    _dragPreviewObject = std::make_shared<Object>(frm);
                    sf::Sprite previewSprite{ _editor->resources().textures().get(frmPath) };
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

    int hexPosition = _editor->getViewportController()->worldPosToHexIndex(worldPos);
    if (hexPosition >= 0) {
        const auto* hexGrid = _editor->getHexagonGrid();
        if (auto hex = hexGrid->getHexByPosition(static_cast<uint32_t>(hexPosition)); hex.has_value()) {
            _dragPreviewObject->setHexPosition(hex->get());
        }
    }
}

void DragDropManager::finishDragPreview(sf::Vector2f worldPos) {
    if (!_isDraggingFromPalette) {
        return;
    }

    int hexPosition = _editor->getViewportController()->worldPosToHexIndex(worldPos);
    if (hexPosition >= 0) {
        _editor->placeObjectAtPosition(worldPos);
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
    _editor->getCurrentHoverHex() = -1;
}

DragDropManager::DragType DragDropManager::getCurrentDragType() const {
    if (_isDraggingObjects)
        return DragType::OBJECT_MOVE;
    if (_isDraggingFromPalette)
        return DragType::PALETTE_PREVIEW;
    return DragType::NONE;
}

} // namespace geck
