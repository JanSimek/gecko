#include "SelectionVisualizer.h"

#include <algorithm>

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "selection/SelectionManager.h"
#include "ui/core/EditorSession.h"
#include "util/Coordinates.h"

namespace geck {

void SelectionVisualizer::apply(const selection::SelectionState& selection) {
    for (const auto& item : selection.items) {
        switch (item.type) {
            case selection::SelectionType::OBJECT: {
                auto object = item.getObject();
                if (object) {
                    object->select();
                }
                break;
            }

            case selection::SelectionType::ROOF_TILE:
                applyRoofTile(item.getTileIndex());
                break;

            case selection::SelectionType::FLOOR_TILE: {
                int tileIndex = item.getTileIndex();
                if (isValidTileIndex(tileIndex)) {
                    // The renderer outlines tiles from their geometry; just record the index.
                    _floorVisuals.push_back(tileIndex);
                }
                break;
            }

            case selection::SelectionType::HEX: {
                int hexIndex = item.getHexIndex();
                if (hexIndex >= 0 && hexIndex < static_cast<int>(_session.hexgrid().size())) {
                    _hexPositions.push_back(hexIndex);
                }
                break;
            }
        }
    }
}

void SelectionVisualizer::applyRoofTile(int tileIndex) {
    // The renderer outlines roof tiles from their geometry, so even empty (transparent) tiles get
    // a boundary; just record the index.
    if (isValidTileIndex(tileIndex)) {
        _roofVisuals.push_back(tileIndex);
    }
}

void SelectionVisualizer::clear() {
    std::ranges::for_each(_session.objects(), [](auto& object) {
        if (object) {
            object->unselect();
            // The drag preview tints object sprites; reset the colour so no preview tint lingers.
            object->getSprite().setColor(sf::Color::White);
        }
    });

    // Tiles are outlined, so there's nothing to un-tint — just drop the tracked sets
    // (preview tints are cleared by clearDragPreview).
    _floorVisuals.clear();
    _roofVisuals.clear();
    _hexPositions.clear();
}

void SelectionVisualizer::clearHexPositions() {
    _hexPositions.clear();
}

void SelectionVisualizer::refresh() {
    clear();
    apply(_session.selectionManager()->getCurrentSelection());
}

} // namespace geck
