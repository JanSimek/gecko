#include "RenderingEngine.h"
#include "../../editor/Object.h"
#include "../../editor/Hex.h"
#include "../../format/map/Map.h"
#include "../../format/map/MapObject.h"
#include "../../resource/GameResources.h"
#include "../../util/Constants.h"
#include "../../util/ColorUtils.h"
#include "../../util/ResourcePaths.h"
#include "../../util/Coordinates.h"
#include <spdlog/spdlog.h>

namespace geck {

RenderingEngine::RenderingEngine(resource::GameResources& resources)
    : _resources(resources)
    , _hexRenderer(resources) {
}

void RenderingEngine::render(sf::RenderTarget& target,
    const sf::View& view,
    const RenderData& renderData,
    const VisibilitySettings& visibility) {
    target.setView(view);

    // Layer 1: Floor tiles (always rendered)
    if (renderData.floorSprites) {
        renderFloorTiles(target, *renderData.floorSprites);
    }

    // Layer 2: Hex grid overlay (if enabled)
    if (visibility.showHexGrid) {
        renderHexGrid(target, view, renderData);
    }

    // Layer 3: Objects (with visibility filtering)
    if (visibility.showObjects) {
        renderObjects(target, renderData, visibility);
    }

    // Layer 4: Drag preview object
    if (renderData.isDraggingFromPalette && renderData.dragPreviewObject && *renderData.dragPreviewObject) {
        target.draw((*renderData.dragPreviewObject)->getSprite());
    }

    // Layer 5: Roof tiles (if enabled)
    renderRoofTiles(target, renderData, visibility.showRoof);

    // Layer 6: Selection visuals
    renderSelectionVisuals(target, renderData);

    // Layer 7: Exit grids (if enabled)
    if (visibility.showExitGrids && renderData.map) {
        renderExitGrids(target, view, renderData, renderData.map);
    }

    // Layer 8: Hex highlights and markers
    renderHexHighlights(target, renderData);
}

void RenderingEngine::renderFloorTiles(sf::RenderTarget& target,
    const std::vector<sf::Sprite>& floorSprites) {
    for (const auto& floor : floorSprites) {
        target.draw(floor);
    }
}

void RenderingEngine::renderHexGrid(sf::RenderTarget& target,
    const sf::View& view,
    const RenderData& renderData) {
    if (!renderData.hexGrid) {
        return;
    }

    _hexRenderer.renderGrid(target, view, *renderData.hexGrid, renderData.currentHoverHex);
}

void RenderingEngine::renderObjects(sf::RenderTarget& target,
    const RenderData& renderData,
    const VisibilitySettings& visibility) {
    if (!renderData.objects) {
        return;
    }

    for (const auto& object : *renderData.objects) {
        if (!visibility.showWalls && object->getMapObject().isWallObject()) {
            continue;
        }

        if (!visibility.showScrollBlockers && object->getMapObject().isScrollBlocker()) {
            continue;
        }

        target.draw(object->getSprite());

        if (visibility.showLightOverlays && object->hasLight()) {
            target.draw(object->getLightOverlay());
        }
    }

    // Wall blocker overlays render on top of regular objects
    if (visibility.showWallBlockers && renderData.wallBlockerOverlays) {
        for (const auto& overlay : *renderData.wallBlockerOverlays) {
            target.draw(overlay);
        }
    }
}

void RenderingEngine::renderRoofTiles(sf::RenderTarget& target,
    const RenderData& renderData,
    bool showRoof) {
    if (!showRoof) {
        return;
    }

    // Background sprites for selected roof tiles must be drawn before the roof sprites on top
    if (renderData.selectedRoofTileBackgroundSprites) {
        for (const auto& backgroundSprite : *renderData.selectedRoofTileBackgroundSprites) {
            target.draw(backgroundSprite);
        }
    }

    if (renderData.roofSprites) {
        for (const auto& roof : *renderData.roofSprites) {
            target.draw(roof);
        }
    }
}

void RenderingEngine::renderSelectionVisuals(sf::RenderTarget& target,
    const RenderData& renderData) {
    if (renderData.selectedHexPositions && renderData.hexGrid) {
        _hexRenderer.renderSelection(target, *renderData.hexGrid, *renderData.selectedHexPositions);
    }

    if (renderData.isDragSelecting && renderData.selectionRectangle) {
        // Mutable copy so selection-mode colors can be applied
        sf::RectangleShape rectangle = *renderData.selectionRectangle;
        applySelectionRectangleColors(rectangle, renderData.currentSelectionMode);
        target.draw(rectangle);
    }
}

void RenderingEngine::renderHexHighlights(sf::RenderTarget& target,
    const RenderData& renderData) {
    if (!renderData.hexGrid) {
        return;
    }

    _hexRenderer.renderHighlights(target, *renderData.hexGrid, renderData.currentHoverHex, renderData.playerPositionHex);
}

bool RenderingEngine::isHexVisible(int hexWorldX, int hexWorldY,
    const sf::View& view) const {
    sf::Vector2f viewCenter = view.getCenter();
    sf::Vector2f viewSize = view.getSize();

    int worldX = static_cast<int>(viewCenter.x - viewSize.x / 2);
    int worldY = static_cast<int>(viewCenter.y - viewSize.y / 2);
    int viewWidth = static_cast<int>(viewSize.x);
    int viewHeight = static_cast<int>(viewSize.y);

    return (hexWorldX + Hex::HEX_WIDTH * 2 > worldX && hexWorldX < worldX + viewWidth) && (hexWorldY + Hex::HEX_HEIGHT + 4 > worldY && hexWorldY < worldY + viewHeight);
}

void RenderingEngine::applySelectionRectangleColors(sf::RectangleShape& rectangle,
    SelectionMode selectionMode) {
    if (selectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE) {
        rectangle.setFillColor(sf::Color(SelectionColors::SCROLL_BLOCKER_R,
            SelectionColors::SCROLL_BLOCKER_G,
            SelectionColors::SCROLL_BLOCKER_B,
            SelectionColors::SCROLL_BLOCKER_FILL_ALPHA));
        rectangle.setOutlineColor(sf::Color(0, 200, 0, SelectionColors::SCROLL_BLOCKER_OUTLINE_ALPHA));
    } else {
        rectangle.setFillColor(ColorUtils::createSelectionFillColor());
        rectangle.setOutlineColor(ColorUtils::createSelectionOutlineColor());
    }
}

void RenderingEngine::renderExitGrids(sf::RenderTarget& target,
    const sf::View& view,
    const RenderData& renderData,
    const Map* map) {
    if (!map || !renderData.hexGrid) {
        return;
    }

    const sf::Texture& exitGridTexture = _resources.textures().get(ResourcePaths::Frm::EXIT_GRID);
    sf::Sprite exitGridSprite(exitGridTexture);

    renderExitGridsWithSprite(target, view, renderData, map, exitGridSprite);
}

void RenderingEngine::renderExitGridsWithSprite(sf::RenderTarget& target,
    const sf::View& view,
    const RenderData& renderData,
    const Map* map,
    sf::Sprite& exitGridSprite) {

    const auto& allObjects = map->objects();

    auto elevationIt = allObjects.find(renderData.currentElevation);
    if (elevationIt == allObjects.end()) {
        return; // No objects on this elevation
    }

    for (const auto& mapObject : elevationIt->second) {
        if (!mapObject || !mapObject->isExitGridMarker()) {
            continue;
        }

        int hexPosition = mapObject->position;
        if (hexPosition < 0 || hexPosition >= static_cast<int>(renderData.hexGrid->size())) {
            continue;
        }

        auto hexOptional = renderData.hexGrid->getHexByPosition(hexPosition);
        if (!hexOptional.has_value()) {
            continue;
        }

        const Hex& hex = hexOptional.value().get();
        WorldCoords hexCenter(hex.x(), hex.y());

        if (!isHexVisible(hexCenter.x(), hexCenter.y(), view)) {
            continue;
        }

        exitGridSprite.setPosition(hexCenter.toVector());
        target.draw(exitGridSprite);
    }
}

} // namespace geck
