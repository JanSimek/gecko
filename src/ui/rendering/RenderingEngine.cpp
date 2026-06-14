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
#include <cstdint>
#include <unordered_set>

namespace geck {

namespace {
    // Fragment shader that emits the outline colour only at the sprite's silhouette EDGE — an
    // opaque pixel that has a transparent neighbour — and is transparent everywhere else. Drawing
    // the sprite through this on top of the scene yields a thin 1px outline that does not fill (so
    // it never hides foreground objects). Samples that fall OUTSIDE the sprite's atlas sub-rect are
    // treated as transparent: this both avoids bleeding into adjacent sprites in the sheet AND lets
    // the silhouette be outlined where the art runs to the cell edge (e.g. a wall filling its cell
    // width). SFML gives normalised gl_TexCoord[0] via its texture matrix.
    constexpr const char* kOutlineFragmentShader = R"(
uniform sampler2D texture;
uniform vec4 outlineColor;
uniform vec2 texel; // one-texel step in texcoord units
uniform vec4 rect;  // sprite sub-rect in texcoords: (minX, minY, maxX, maxY)
float sampleAlpha(vec2 p) {
    vec2 inside = step(rect.xy, p) * step(p, rect.zw); // 0 outside the sub-rect on each axis
    return inside.x * inside.y * texture2D(texture, p).a;
}
void main() {
    vec2 uv = gl_TexCoord[0].xy;
    float a  = texture2D(texture, uv).a;
    float aL = sampleAlpha(vec2(uv.x - texel.x, uv.y));
    float aR = sampleAlpha(vec2(uv.x + texel.x, uv.y));
    float aU = sampleAlpha(vec2(uv.x, uv.y - texel.y));
    float aD = sampleAlpha(vec2(uv.x, uv.y + texel.y));
    float minN = min(min(aL, aR), min(aU, aD));
    // 1px outline: an opaque pixel that has a transparent neighbour (the silhouette's inner edge).
    float edge = step(0.5, a) * (1.0 - step(0.5, minN));
    gl_FragColor = vec4(outlineColor.rgb, outlineColor.a * edge);
}
)";

    // Outline thickness in pixels (texels sampled when detecting the silhouette edge).
    constexpr float kOutlineThickness = 1.0f;
} // namespace

sf::Color RenderingEngine::objectOutlineColor(const Object& object) const {
    const auto mapObject = object.getMapObjectPtr();
    if (mapObject) {
        if (mapObject->isWallObject()) {
            return _selectionColors.wall;
        }
        if (mapObject->objectType() == 1u) { // Pro::OBJECT_TYPE::CRITTER
            return _selectionColors.critter;
        }
    }
    return _selectionColors.object;
}

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

    // Layer 3: Objects. Per-category visibility (objects/critters/walls/scroll blockers) is
    // handled per object inside renderObjects via isObjectVisible — there is no master gate here,
    // otherwise turning off "objects" would also hide critters and walls.
    renderObjects(target, renderData, visibility);

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

    // Selected object outlines go on top of roofs (like the tile selection outlines) so the whole
    // outline shows — a roof above a selected wall must not clip it. The edge shader only paints
    // the thin outline, so the roof/foreground stays visible beneath.
    drawSelectedObjectOutlines(target, renderData, visibility);

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

    const sf::Color outlineColor = objectOutlineColor(object);
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

    // Edge-detect the silhouette in the sprite's atlas sub-rect (normalised texcoords).
    const sf::Vector2u atlas = source.getTexture().getSize();
    const sf::IntRect texRect = source.getTextureRect();
    const float aw = static_cast<float>(atlas.x);
    const float ah = static_cast<float>(atlas.y);

    _outlineShader.setUniform("outlineColor", sf::Glsl::Vec4(outlineColor));
    _outlineShader.setUniform("texel", sf::Glsl::Vec2(kOutlineThickness / aw, kOutlineThickness / ah));
    _outlineShader.setUniform("rect",
        sf::Glsl::Vec4(static_cast<float>(texRect.position.x) / aw,
            static_cast<float>(texRect.position.y) / ah,
            static_cast<float>(texRect.position.x + texRect.size.x) / aw,
            static_cast<float>(texRect.position.y + texRect.size.y) / ah));

    sf::RenderStates states;
    states.shader = &_outlineShader;
    target.draw(source, states); // shader emits only the silhouette edge
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

void RenderingEngine::drawSelectedObjectOutlines(sf::RenderTarget& target,
    const RenderData& renderData,
    const VisibilitySettings& visibility) {
    if (!renderData.objects) {
        return;
    }

    // Draw each selected object's silhouette outline after everything else, so the full outline
    // shows even where a neighbour (e.g. a wall between two other walls) overlaps it. The edge
    // shader only paints the thin outline, so foreground objects stay visible underneath.
    for (const auto& object : *renderData.objects) {
        if (!object->isSelected() || !isObjectVisible(object->getMapObject(), visibility)) {
            continue;
        }
        drawObjectOutline(target, *object);
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
    const sf::Color outlineColor = _selectionColors.tile;
    // A slight translucent fill so the selected region reads as a filled shape (not just an
    // outline). Roof tiles get a marginally different fill so a roof selection is distinguishable
    // from a floor one. Tweak these two alphas to taste.
    const sf::Color fillColor(outlineColor.r, outlineColor.g, outlineColor.b,
        static_cast<std::uint8_t>(roof ? 70 : 45));
    sf::VertexArray fill(sf::PrimitiveType::Triangles);
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
    const auto addFillTriangle = [&](sf::Vector2f a, sf::Vector2f b, sf::Vector2f c) {
        fill.append(sf::Vertex{ a, fillColor });
        fill.append(sf::Vertex{ b, fillColor });
        fill.append(sf::Vertex{ c, fillColor });
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

        // Fill the tile parallelogram (two triangles). Tiles tile the plane exactly, so the
        // covered region fills uniformly with no seams or doubled alpha.
        addFillTriangle(top, right, bottom);
        addFillTriangle(top, bottom, left);

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

    target.draw(fill); // translucent fill under the crisp boundary
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
