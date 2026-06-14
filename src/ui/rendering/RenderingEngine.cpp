#include "RenderingEngine.h"
#include "editor/Object.h"
#include "editor/Hex.h"
#include "ui/rendering/ObjectVisibility.h"
#include "ui/viewport/ViewportController.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "resource/GameResources.h"
#include "util/Constants.h"
#include "util/ColorUtils.h"
#include "resource/ResourcePaths.h"
#include "util/Coordinates.h"
#include "util/TileUtils.h"
#include <spdlog/spdlog.h>
#include <array>
#include <unordered_set>

namespace geck {

namespace {
    // Fragment shader that replaces a sprite's RGB with a flat outline colour while keeping its
    // alpha — i.e. a solid-colour silhouette. Drawing this silhouette at small offsets builds an
    // outline ring around the real sprite. Legacy GLSL built-ins (texture2D / gl_TexCoord) are
    // what SFML's fixed-function sprite pipeline provides.
    constexpr const char* kOutlineFragmentShader = R"(
uniform sampler2D texture;
uniform vec4 outlineColor;
void main() {
    float alpha = texture2D(texture, gl_TexCoord[0].xy).a;
    gl_FragColor = vec4(outlineColor.rgb, outlineColor.a * alpha);
}
)";

    // Outline thickness in pixels (the silhouette is drawn at these 8 offsets).
    constexpr float kOutlineThickness = 1.0f;
} // namespace

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

    // Layer 4b: Pattern stamp ghost preview (sprites already carry their semi-transparency):
    // floor tiles, then objects, then roof tiles on top.
    if (renderData.stampPreviewFloorTiles) {
        for (const auto& sprite : *renderData.stampPreviewFloorTiles) {
            target.draw(sprite);
        }
    }
    if (renderData.stampPreviewObjects) {
        for (const auto& object : *renderData.stampPreviewObjects) {
            if (object) {
                target.draw(object->getSprite());
            }
        }
    }
    if (renderData.stampPreviewRoofTiles) {
        for (const auto& sprite : *renderData.stampPreviewRoofTiles) {
            target.draw(sprite);
        }
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

void RenderingEngine::ensureOutlineShader() {
    if (_outlineShaderTried) {
        return;
    }
    _outlineShaderTried = true;

    if (!sf::Shader::isAvailable()) {
        spdlog::warn("Selection outline: shaders unavailable on this GL context; using bounding-box fallback");
        return;
    }
    if (_outlineShader.loadFromMemory(kOutlineFragmentShader, sf::Shader::Type::Fragment)) {
        _outlineShader.setUniform("texture", sf::Shader::CurrentTexture);
        _outlineShaderOk = true;
    } else {
        spdlog::warn("Selection outline: outline shader failed to compile; using bounding-box fallback");
    }
}

void RenderingEngine::drawObjectOutline(sf::RenderTarget& target, const Object& object) {
    ensureOutlineShader();

    const sf::Color outlineColor = ColorUtils::createObjectSelectionColor();
    const sf::Sprite& source = object.getSprite();

    if (!_outlineShaderOk) {
        // Fallback when shaders are unavailable: a simple bounding-box outline.
        const sf::FloatRect bounds = source.getGlobalBounds();
        sf::RectangleShape box(bounds.size);
        box.setPosition(bounds.position);
        box.setFillColor(sf::Color::Transparent);
        box.setOutlineColor(outlineColor);
        box.setOutlineThickness(1.0f);
        target.draw(box);
        return;
    }

    _outlineShader.setUniform("outlineColor", sf::Glsl::Vec4(outlineColor));

    sf::Sprite silhouette = source; // same texture, rect and transform
    sf::RenderStates states;
    states.shader = &_outlineShader;

    constexpr float t = kOutlineThickness;
    const std::array<sf::Vector2f, 8> offsets{ { { -t, 0.f }, { t, 0.f }, { 0.f, -t }, { 0.f, t },
        { -t, -t }, { t, -t }, { -t, t }, { t, t } } };
    const sf::Vector2f base = silhouette.getPosition();
    for (const auto& offset : offsets) {
        silhouette.setPosition(base + offset);
        target.draw(silhouette, states);
    }
}

void RenderingEngine::renderObjects(sf::RenderTarget& target,
    const RenderData& renderData,
    const VisibilitySettings& visibility) {
    if (!renderData.objects) {
        return;
    }

    for (const auto& object : *renderData.objects) {
        // Shared with picking (EditorWidget::getObjectsAtPosition) via isObjectVisible so a
        // hidden object is never drawn nor selectable.
        if (!isObjectVisible(object->getMapObject(), visibility)) {
            continue;
        }

        // Selected objects get a silhouette outline behind the sprite so the artwork keeps its
        // real colours and only gains a coloured border.
        if (object->isSelected()) {
            drawObjectOutline(target, *object);
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

void RenderingEngine::renderTileSelectionOutline(sf::RenderTarget& target,
    const std::vector<int>& selectedTiles, bool roof) {
    if (selectedTiles.empty()) {
        return;
    }

    const std::unordered_set<int> selected(selectedTiles.begin(), selectedTiles.end());
    const sf::Color outlineColor = ColorUtils::createObjectSelectionColor();
    sf::VertexArray edges(sf::PrimitiveType::Lines);

    const auto isSelected = [&](int row, int col) {
        if (row < 0 || row >= MAP_HEIGHT || col < 0 || col >= MAP_WIDTH) {
            return false;
        }
        return selected.contains(row * MAP_WIDTH + col);
    };
    const auto addEdge = [&](sf::Vector2f a, sf::Vector2f b) {
        edges.append(sf::Vertex{ a, outlineColor });
        edges.append(sf::Vertex{ b, outlineColor });
    };

    for (int index : selectedTiles) {
        const int row = index / MAP_WIDTH;
        const int col = index % MAP_WIDTH;
        const auto screen = indexToScreenPosition(index, roof);
        const float sx = static_cast<float>(screen.x);
        const float sy = static_cast<float>(screen.y);

        // The four corners of the (sheared) tile parallelogram.
        const sf::Vector2f top{ sx + 48.f, sy };
        const sf::Vector2f right{ sx + 80.f, sy + 24.f };
        const sf::Vector2f bottom{ sx + 32.f, sy + 36.f };
        const sf::Vector2f left{ sx, sy + 12.f };

        // Draw an edge only when the tile sharing it is not selected (union boundary).
        if (!isSelected(row, col - 1))
            addEdge(top, right); // upper-right edge
        if (!isSelected(row + 1, col))
            addEdge(right, bottom); // lower-right edge
        if (!isSelected(row, col + 1))
            addEdge(bottom, left); // lower-left edge
        if (!isSelected(row - 1, col))
            addEdge(left, top); // upper-left edge
    }

    target.draw(edges);
}

void RenderingEngine::renderSelectionVisuals(sf::RenderTarget& target,
    const RenderData& renderData) {
    if (renderData.selectedFloorTiles) {
        renderTileSelectionOutline(target, *renderData.selectedFloorTiles, false);
    }
    if (renderData.selectedRoofTiles) {
        renderTileSelectionOutline(target, *renderData.selectedRoofTiles, true);
    }

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
    return ViewportController::isHexVisible(hexWorldX, hexWorldY, view);
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
        WorldCoords hexCenter(static_cast<float>(hex.x()), static_cast<float>(hex.y()));

        if (!isHexVisible(static_cast<int>(hexCenter.x()), static_cast<int>(hexCenter.y()), view)) {
            continue;
        }

        exitGridSprite.setPosition(hexCenter.toVector());
        target.draw(exitGridSprite);
    }
}

} // namespace geck
