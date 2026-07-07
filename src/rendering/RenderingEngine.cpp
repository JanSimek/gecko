#include "RenderingEngine.h"
#include "editor/Hex.h"
#include "editor/HexGeometry.h"
#include "editor/Object.h"
#include "format/frm/Frm.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "util/BuiltTile.h"
#include "rendering/MapEdgeOverlayGeometry.h"
#include "rendering/ObjectVisibility.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "util/ColorUtils.h"
#include "util/Constants.h"
#include "util/Coordinates.h"
#include "util/TileUtils.h"
#include "viewport/ViewportController.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <optional>
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace geck {

namespace {
    // Fragment shader that emits the outline colour only at a silhouette EDGE — an opaque texel with
    // a transparent neighbour — and is transparent elsewhere. Run over the offscreen selection mask
    // it yields a thin 1px outline that never fills, so the scene underneath stays visible. The mask
    // is clamp-to-edge, so neighbour samples past the mask border read the border texel: a silhouette
    // running off the viewport edge continues rather than gaining a fake outline along that edge.
    // SFML gives normalised gl_TexCoord[0] via its texture matrix.
    constexpr const char* kOutlineFragmentShader = R"(
uniform sampler2D texture;
uniform vec4 outlineColor;
uniform vec2 texel; // one-texel step in texcoord units
void main() {
    vec2 uv = gl_TexCoord[0].xy;
    float a  = texture2D(texture, uv).a;
    float aL = texture2D(texture, vec2(uv.x - texel.x, uv.y)).a;
    float aR = texture2D(texture, vec2(uv.x + texel.x, uv.y)).a;
    float aU = texture2D(texture, vec2(uv.x, uv.y - texel.y)).a;
    float aD = texture2D(texture, vec2(uv.x, uv.y + texel.y)).a;
    float minN = min(min(aL, aR), min(aU, aD));
    // 1px outline: an opaque pixel that has a transparent neighbour (the silhouette's inner edge).
    float edge = step(0.5, a) * (1.0 - step(0.5, minN));
    gl_FragColor = vec4(outlineColor.rgb, outlineColor.a * edge);
}
)";

    // Outline thickness in pixels (texels sampled when detecting the silhouette edge).
    constexpr float kOutlineThickness = 1.0f;

    // Fallout 2 CE light model, used to tint the "Show light overlays" cue like the engine lights a
    // hex (fallout2-ce src/light.cc, src/object.cc `_obj_adjust_light`). The engine stamps light into
    // a per-hex grid: full intensity on the source hex, then a linear per-ring decrement out to the
    // light's radius. We reproduce the falloff value per ring; we do NOT reproduce wall shadowing or
    // the ambient-darkness pass — this is an illustrative overlay, not a lighting simulation.
    namespace light {
        constexpr int FLOOR = 655;    // per-hex ambient floor the engine inits every cell to (~1% of full)
        constexpr int FULL = 65536;   // 0x10000 == 100% brightness (light intensity is clamped to this)
        constexpr int MAX_RADIUS = 8; // object light radius is clamped to 8 hexes

        // Intensity the engine deposits `ring` steps from a source of the given `intensity`/`radius`:
        // the source hex (ring 0) gets full intensity, each further ring loses (intensity - FLOOR) /
        // (radius + 1). Mirrors the `v28[]` distribution built in `_obj_adjust_light`.
        int depositedIntensity(int intensity, int radius, int ring) {
            if (ring <= 0) {
                return intensity;
            }
            const int step = (intensity - FLOOR) / (radius + 1);
            return intensity - ring * step;
        }

        // Overlay alpha for a deposited intensity: nothing at/below the ambient floor, otherwise a warm
        // tint whose opacity tracks the fraction of full brightness contributed, so brighter/closer
        // hexes read stronger and dim or distant ones fade out.
        constexpr int MIN_ALPHA = 30;
        constexpr int MAX_ALPHA = 140;
        int tintAlpha(int deposited) {
            if (deposited <= FLOOR) {
                return 0;
            }
            const float frac = std::min(1.0f,
                static_cast<float>(deposited - FLOOR) / static_cast<float>(FULL - FLOOR));
            return MIN_ALPHA + static_cast<int>(frac * static_cast<float>(MAX_ALPHA - MIN_ALPHA));
        }
    } // namespace light
} // namespace

sf::Color RenderingEngine::objectOutlineColor(const Object& object) const {
    if (const auto mapObject = object.getMapObjectPtr(); mapObject) {
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

    // Layer 1b: Light overlays. Drawn as a ground layer (over the floor, under objects) so each light
    // source's illuminated hexes read like light pooling on the ground with objects standing in it.
    if (visibility.showLightOverlays) {
        renderLightOverlays(target, view, renderData, visibility);
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
    if (renderData.stampPreview.floorTiles) {
        for (const auto& sprite : *renderData.stampPreview.floorTiles) {
            target.draw(sprite);
        }
    }
    if (renderData.stampPreview.objects) {
        for (const auto& object : *renderData.stampPreview.objects) {
            if (object) {
                target.draw(object->getSprite());
            }
        }
    }
    if (renderData.stampPreview.roofTiles) {
        for (const auto& sprite : *renderData.stampPreview.roofTiles) {
            target.draw(sprite);
        }
    }

    // Layer 5: Roof tiles (if enabled)
    renderRoofTiles(target, renderData, visibility.showRoof);

    // Outlines draw on top of roofs so a roof above a selected wall can't clip them; the edge
    // shader paints only the thin border, leaving the scene visible beneath.
    drawSelectedObjectOutlines(target, renderData, visibility);

    // Layer 6: Selection visuals
    renderSelectionVisuals(target, renderData, visibility.showRoof);

    // Layer 7: Exit grids (if enabled)
    if (visibility.showExitGrids && renderData.map) {
        renderExitGrids(target, view, renderData, renderData.map);
    }

    // Layer 7b: Spatial-script trigger markers + radius (if enabled)
    if (visibility.showSpatialScripts && renderData.map) {
        renderSpatialScripts(target, view, renderData);
    }

    // Layer 7c: Map-edge (.edg) scroll zones + v2 clip rect (if enabled)
    if (visibility.showMapEdges && renderData.map) {
        renderMapEdges(target, view, renderData);
    }

    // Layer 8: Hex highlights and markers
    renderHexHighlights(target, renderData);

    // Layer 9: Exit-grid "Draw edge" live preview (polyline + prospective on-line hexes).
    renderExitGridEdgePreview(target, view, renderData);
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

std::map<std::uint32_t, std::vector<const Object*>> RenderingEngine::collectSelectedOutlineGroups(
    const RenderData& renderData,
    const VisibilitySettings& visibility) const {
    std::map<std::uint32_t, std::vector<const Object*>> groups;
    if (!renderData.objects) {
        return groups;
    }
    for (const auto& object : *renderData.objects) {
        if (!object->isSelected() || !isObjectVisible(object->getMapObject(), visibility)) {
            continue;
        }
        groups[objectOutlineColor(*object).toInteger()].push_back(object.get());
    }
    return groups;
}

void RenderingEngine::drawOutlineFallbackBoxes(sf::RenderTarget& target,
    const std::map<std::uint32_t, std::vector<const Object*>>& groups) const {
    for (const auto& [colorValue, objects] : groups) {
        const sf::Color color(colorValue);
        for (const Object* object : objects) {
            const sf::FloatRect bounds = object->getSprite().getGlobalBounds();
            sf::RectangleShape box(bounds.size);
            box.setPosition(bounds.position);
            box.setFillColor(sf::Color::Transparent);
            box.setOutlineColor(color);
            box.setOutlineThickness(1.0f);
            target.draw(box);
        }
    }
}

void RenderingEngine::drawSelectedObjectOutlines(sf::RenderTarget& target,
    const RenderData& renderData,
    const VisibilitySettings& visibility) {
    ensureOutlineShader();

    // Group selected, visible objects by outline colour, then draw each group's sprites alone into
    // the offscreen mask and edge-detect there — a clean silhouette outline on every side.
    const auto groups = collectSelectedOutlineGroups(renderData, visibility);
    if (groups.empty()) {
        return;
    }

    if (!_outlineShaderOk) {
        drawOutlineFallbackBoxes(target, groups);
        return;
    }

    const sf::Vector2u size = target.getSize();
    if (size.x == 0 || size.y == 0) {
        return;
    }
    if (_outlineMask.getSize() != size) {
        if (!_outlineMask.resize(size)) {
            spdlog::warn("Selection outline: could not allocate {}x{} mask; skipping outlines", size.x, size.y);
            return;
        }
        // Clamp-to-edge so the shader's neighbour samples past the mask border read the border
        // texel (a silhouette running off-screen continues rather than gaining a fake outline).
        _outlineMask.setRepeated(false);
    }

    const auto width = static_cast<float>(size.x);
    const auto height = static_cast<float>(size.y);
    // The mask is at screen resolution, so a one-texel edge is a constant 1px outline at any zoom.
    _outlineShader.setUniform("texel", sf::Glsl::Vec2(kOutlineThickness / width, kOutlineThickness / height));

    const sf::View sceneView = target.getView();                               // align silhouettes with the scene
    const sf::View screenView(sf::FloatRect({ 0.f, 0.f }, { width, height })); // map the mask 1:1 onto the target

    for (const auto& [colorValue, objects] : groups) {
        const sf::Color color(colorValue);
        if (visibility.mergeSelectionOutlines) {
            // One mask for the whole colour group: touching silhouettes union into a single outline.
            strokeOutlineGroup(target, sceneView, screenView, color, objects);
        } else {
            // One mask per object: every sprite is outlined on its own, so shared edges show too.
            for (const Object* object : objects) {
                strokeOutlineGroup(target, sceneView, screenView, color, { object });
            }
        }
    }
    target.setView(sceneView);
}

void RenderingEngine::strokeOutlineGroup(sf::RenderTarget& target,
    const sf::View& sceneView,
    const sf::View& screenView,
    sf::Color color,
    const std::vector<const Object*>& objects) {
    _outlineMask.setView(sceneView);
    _outlineMask.clear(sf::Color::Transparent);
    for (const Object* object : objects) {
        _outlineMask.draw(object->getSprite());
    }
    _outlineMask.display();

    _outlineShader.setUniform("outlineColor", sf::Glsl::Vec4(color));
    sf::Sprite maskSprite(_outlineMask.getTexture());
    sf::RenderStates states;
    states.shader = &_outlineShader;
    target.setView(screenView);
    target.draw(maskSprite, states); // shader emits only the silhouette edge of the mask
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
    }

    // Wall blocker overlays render on top of regular objects
    if (visibility.showWallBlockers && renderData.wallBlockerOverlays) {
        for (const auto& overlay : *renderData.wallBlockerOverlays) {
            target.draw(overlay);
        }
    }
}

void RenderingEngine::renderLightOverlays(sf::RenderTarget& target,
    const sf::View& view,
    const RenderData& renderData,
    const VisibilitySettings& visibility) {
    if (!renderData.objects || !renderData.hexGrid) {
        return;
    }

    // Warm light tint; the per-ring alpha (set below) carries the falloff.
    const sf::Color lightTint(255, 224, 130);

    for (const auto& object : *renderData.objects) {
        if (!object->hasLight()) {
            continue;
        }
        const auto mapObject = object->getMapObjectPtr(); // non-null: hasLight() already checked it
        // A light on a hidden layer (e.g. walls off) shows no overlay, matching the object itself —
        // the same visibility rule renderObjects uses, so a hidden source never glows.
        if (!isObjectVisible(*mapObject, visibility)) {
            continue;
        }

        // light_radius / light_intensity are uint32_t; clamp in unsigned space so a corrupt
        // out-of-range value can't become a negative int after the cast.
        const int radius = static_cast<int>(std::min<std::uint32_t>(mapObject->light_radius, light::MAX_RADIUS));
        const int intensity = static_cast<int>(std::min<std::uint32_t>(mapObject->light_intensity, light::FULL));
        const int centerHex = mapObject->position;

        // Group the illuminated hexes by ring so each ring can be tinted by its falloff value; a single
        // renderHexOverlay call per ring keeps the draw count low (radius is at most 8).
        std::vector<std::vector<int>> hexesByRing(static_cast<size_t>(radius) + 1);
        for (const auto& hex : hexgrid::hexDiscByDistance(centerHex, radius)) {
            hexesByRing[static_cast<size_t>(hex.distance)].push_back(hex.position);
        }

        for (int ring = 0; ring <= radius; ++ring) {
            const auto& ringHexes = hexesByRing[static_cast<size_t>(ring)];
            if (ringHexes.empty()) {
                continue;
            }
            const int alpha = light::tintAlpha(light::depositedIntensity(intensity, radius, ring));
            if (alpha <= 0) {
                continue;
            }
            sf::Color color = lightTint;
            color.a = static_cast<std::uint8_t>(alpha);
            _hexRenderer.renderHexOverlay(target, view, *renderData.hexGrid, ringHexes, color);
        }
    }
}

void RenderingEngine::renderRoofTiles(sf::RenderTarget& target,
    const RenderData& renderData,
    bool showRoof) {
    if (!showRoof) {
        return;
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
        const auto sx = static_cast<float>(screen.x);
        const auto sy = static_cast<float>(screen.y);

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
    const RenderData& renderData,
    bool showRoof) {
    if (renderData.selectedFloorTiles) {
        renderTileSelectionOutline(target, *renderData.selectedFloorTiles, false);
    }
    // Only outline selected roof tiles while the roof layer is shown, so hiding the roof hides its
    // outline too (otherwise a roof selection lingers as an outline over the floor-only view).
    if (showRoof && renderData.selectedRoofTiles) {
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
    if (!map || !renderData.objects || !renderData.hexGrid) {
        return;
    }

    // Editor-only "Show exit grids" overlay: the bundled "EG" hex marker (art/misc/exitgrid.frm, shipped
    // under resources/, not in the DATs) on every exit-grid hex. Separate from the player-visible
    // directional exitgrd*/ext2grd* art renderObjects already drew.
    const sf::Texture& exitGridTexture = _resources.textures().get(ResourcePaths::Frm::EXIT_GRID);
    sf::Sprite exitGridSprite(exitGridTexture);
    // Anchor by centre so the marker sits on the hex.
    exitGridSprite.setOrigin(exitGridSprite.getLocalBounds().size / 2.f);

    for (const auto& object : *renderData.objects) {
        const auto mapObject = object ? object->getMapObjectPtr() : nullptr;
        if (!mapObject || !mapObject->isExitGridMarker()) {
            continue;
        }
        const auto hex = renderData.hexGrid->getHexByPosition(static_cast<uint32_t>(mapObject->position));
        if (!hex.has_value()) {
            continue;
        }
        const int cx = hex->get().x();
        const int cy = hex->get().y();
        if (!isHexVisible(cx, cy, view)) {
            continue;
        }
        exitGridSprite.setPosition(sf::Vector2f(static_cast<float>(cx), static_cast<float>(cy)));
        target.draw(exitGridSprite);
    }
}

void RenderingEngine::renderSpatialScripts(sf::RenderTarget& target,
    const sf::View& view,
    const RenderData& renderData) {
    const Map* map = renderData.map;
    if (!map || !renderData.hexGrid) {
        return;
    }

    constexpr int SPATIAL = static_cast<int>(MapScript::ScriptType::SPATIAL);
    const auto& scripts = map->getMapFile().map_scripts[SPATIAL];
    if (scripts.empty()) {
        return;
    }

    // Radius disc: translucent green, matching the marker. hexesWithinRadius reproduces the engine's
    // tileDistanceBetween(centre, h) <= radius trigger zone. The selected script switches to a
    // brighter amber so it reads as highlighted against the green of the others.
    const sf::Color radiusFill(80, 220, 110, 110);
    const sf::Color selectedRadiusFill(255, 205, 70, 150);
    const sf::Color markerTint(255, 255, 255);         // unselected: no colour shift
    const sf::Color selectedMarkerTint(255, 235, 130); // selected: shifts the green marker to amber
    const sf::Color fallbackHex(80, 220, 110, 200);
    const sf::Color selectedFallbackHex(255, 205, 70, 230);

    // The engine's spatial-script marker (interface art msef001 — a green hex) lives in the DATs, so
    // guard the load: if the game art is unavailable, fall back to a solid hex so the centre still shows.
    const sf::Texture* markerTexture = nullptr;
    try {
        markerTexture = &_resources.textures().get(ResourcePaths::Frm::SPATIAL_SCRIPT);
    } catch (const std::exception& e) {
        static bool warned = false;
        if (!warned) {
            spdlog::warn("Spatial-script marker art unavailable ({}); drawing a plain hex instead", e.what());
            warned = true;
        }
    }

    std::optional<sf::Sprite> markerSprite;
    if (markerTexture) {
        markerSprite.emplace(*markerTexture);
        markerSprite->setOrigin(markerSprite->getLocalBounds().size / 2.f); // centre on the hex
    }

    for (const MapScript& script : scripts) {
        if (built_tile::elevationOf(script.timer) != static_cast<uint32_t>(renderData.currentElevation)) {
            continue;
        }
        const bool selected = script.pid == renderData.selectedSpatialScriptSid;
        const int centerHex = static_cast<int>(built_tile::tileOf(script.timer));

        // Radius disc first (viewport-culled per hex inside renderHexOverlay), so the marker sits on top.
        const auto discHexes = hexgrid::hexesWithinRadius(centerHex, static_cast<int>(script.spatial_radius));
        _hexRenderer.renderHexOverlay(target, view, *renderData.hexGrid, discHexes,
            selected ? selectedRadiusFill : radiusFill);

        // Centre marker.
        const auto hex = renderData.hexGrid->getHexByPosition(static_cast<uint32_t>(centerHex));
        if (!hex.has_value()) {
            continue;
        }
        const int cx = hex->get().x();
        const int cy = hex->get().y();
        if (!isHexVisible(cx, cy, view)) {
            continue;
        }
        if (markerSprite) {
            markerSprite->setColor(selected ? selectedMarkerTint : markerTint);
            markerSprite->setPosition(sf::Vector2f(static_cast<float>(cx), static_cast<float>(cy)));
            target.draw(*markerSprite);
        } else {
            _hexRenderer.renderHexOverlay(target, view, *renderData.hexGrid, { centerHex },
                selected ? selectedFallbackHex : fallbackHex);
        }
    }
}

void RenderingEngine::renderMapEdges(sf::RenderTarget& target,
    [[maybe_unused]] const sf::View& view,
    const RenderData& renderData) {
    const Map* map = renderData.map;
    if (!map || !renderData.hexGrid || !map->edge().has_value()) {
        return;
    }

    const int elevation = renderData.currentElevation;
    if (elevation < 0 || elevation >= MapEdge::ELEVATION_COUNT) {
        return;
    }
    const MapEdge& edge = *map->edge();
    const MapEdge::Elevation& data = edge.elevations[elevation];

    const sf::Color zoneColor(230, 70, 70);          // red — the engine's active-rect colour
    const sf::Color selectedZoneColor(255, 205, 70); // amber — matches the selected spatial script
    const sf::Color movingSideColor(90, 220, 120);   // green — the side currently being dragged

    // A zone is a world-space axis-aligned rectangle; draw its four sides as a line loop, with the
    // dragged side of the selected zone overdrawn in green.
    for (size_t i = 0; i < data.zones.size(); ++i) {
        const auto box = mapEdgeZoneWorldBounds(*renderData.hexGrid, data.zones[i]);
        if (!box.has_value()) {
            continue;
        }
        const bool selected = static_cast<int>(i) == renderData.selectedEdgeZone;
        const float l = box->position.x;
        const float t = box->position.y;
        const float r = l + box->size.x;
        const float b = t + box->size.y;

        const sf::Color color = selected ? selectedZoneColor : zoneColor;
        const std::array<sf::Vertex, 5> loop{
            sf::Vertex{ { l, t }, color }, sf::Vertex{ { r, t }, color },
            sf::Vertex{ { r, b }, color }, sf::Vertex{ { l, b }, color },
            sf::Vertex{ { l, t }, color }
        };
        target.draw(loop.data(), loop.size(), sf::PrimitiveType::LineStrip);

        // Highlight the dragged side (0=left,1=top,2=right,3=bottom) of the selected zone.
        if (selected && renderData.activeEdgeSide >= 0) {
            sf::Vector2f a;
            sf::Vector2f c;
            switch (renderData.activeEdgeSide) {
                case 0:
                    a = { l, t };
                    c = { l, b };
                    break; // left
                case 1:
                    a = { l, t };
                    c = { r, t };
                    break; // top
                case 2:
                    a = { r, t };
                    c = { r, b };
                    break; // right
                default:
                    a = { l, b };
                    c = { r, b };
                    break; // bottom
            }
            const std::array<sf::Vertex, 2> seg{ sf::Vertex{ a, movingSideColor }, sf::Vertex{ c, movingSideColor } };
            target.draw(seg.data(), seg.size(), sf::PrimitiveType::Lines);
        }
    }

    // v2: the per-elevation square clip rect — one diagonal edge per side, coloured by clip mode.
    if (edge.isVersion2()) {
        const sf::Color clipAll(90, 220, 120); // side clips all objects
        const sf::Color clipLow(220, 200, 90); // side clips low objects only
        const auto corners = mapEdgeSquareCorners(data.squareRect);
        // Consecutive corners bound the top/left/bottom/right sides in turn.
        const bool sideClipsAll[4] = {
            data.clipSides.top, data.clipSides.left, data.clipSides.bottom, data.clipSides.right
        };
        for (int side = 0; side < 4; ++side) {
            const sf::Color color = sideClipsAll[side] ? clipAll : clipLow;
            const std::array<sf::Vertex, 2> seg{
                sf::Vertex{ corners[side], color }, sf::Vertex{ corners[(side + 1) % 4], color }
            };
            target.draw(seg.data(), seg.size(), sf::PrimitiveType::Lines);
        }
    }
}

void RenderingEngine::renderExitGridEdgePreview(sf::RenderTarget& target,
    const sf::View& view,
    const RenderData& renderData) {
    if (!renderData.exitGridPreview.active || !renderData.hexGrid) {
        return;
    }
    drawExitGridPreviewMarkers(target, view, renderData);
    drawExitGridPreviewLine(target, renderData);
}

std::shared_ptr<Object> RenderingEngine::buildExitGridPreviewObject(const RenderData& renderData,
    int hexIndex, std::uint32_t frmPid) const {
    if (frmPid == 0 || !renderData.hexGrid) {
        return nullptr;
    }
    const auto hexOptional = renderData.hexGrid->getHexByPosition(static_cast<uint32_t>(hexIndex));
    if (!hexOptional.has_value()) {
        return nullptr;
    }

    // FRM resolve/load/texture-upload can fail or throw; contain it here so one unbuildable preview hex
    // never aborts the whole preview — the caller falls back to a plain marker.
    try {
        const std::filesystem::path frmName = _resources.frmResolver().resolve(frmPid);
        if (frmName.empty()) {
            return nullptr;
        }
        const Frm* frm = _resources.repository().find<Frm>(frmName);
        if (!frm) {
            frm = _resources.repository().load<Frm>(frmName);
        }
        if (!frm) {
            return nullptr;
        }

        // Anchor exactly like a committed exit grid: setDirection sets the frame rect, then
        // setHexPosition centers the bar on the hex using that frame's FRM offset. Order matters —
        // setHexPosition reads the current frame's width()/height()/shift, so the rect must be set first.
        auto previewObject = std::make_shared<Object>(frm);
        previewObject->setSprite(sf::Sprite{ _resources.textures().get(frmName) });
        previewObject->setDirection(ObjectDirection(0));
        previewObject->setHexPosition(hexOptional.value().get());
        return previewObject;
    } catch (const std::exception& e) {
        spdlog::warn("Exit-grid preview: could not build marker for frm 0x{:08X}: {}", frmPid, e.what());
        return nullptr;
    }
}

void RenderingEngine::drawExitGridPreviewMarkers(sf::RenderTarget& target, const sf::View& view,
    const RenderData& renderData) {
    // Each prospective hex is drawn with its own directional marker art (the same FRM the commit will
    // place), anchored like a real exit grid and tinted by destination kind. If the sprite can't be
    // built, it falls back to the plain editor overlay marker so the preview never blanks out.
    const auto* hexes = renderData.exitGridPreview.hexes;
    const auto* frmPids = renderData.exitGridPreview.frmPids;
    if (!hexes || hexes->empty() || !frmPids || frmPids->size() != hexes->size()) {
        return;
    }

    for (std::size_t i = 0; i < hexes->size(); ++i) {
        const int hexIndex = (*hexes)[i];
        if (hexIndex < 0 || hexIndex >= static_cast<int>(renderData.hexGrid->size())) {
            continue;
        }
        const auto hexOptional = renderData.hexGrid->getHexByPosition(static_cast<uint32_t>(hexIndex));
        if (!hexOptional.has_value()
            || !isHexVisible(hexOptional.value().get().x(), hexOptional.value().get().y(), view)) {
            continue;
        }
        if (auto previewObject = buildExitGridPreviewObject(renderData, hexIndex, (*frmPids)[i])) {
            previewObject->getSprite().setColor(renderData.exitGridPreview.tint);
            target.draw(previewObject->getSprite());
        } else {
            // Fall back to the bundled editor "EG" marker (tinted) so this hex still previews.
            const sf::Texture& markerTexture = _resources.textures().get(ResourcePaths::Frm::EXIT_GRID);
            sf::Sprite markerSprite(markerTexture);
            markerSprite.setOrigin(markerSprite.getLocalBounds().size / 2.f);
            markerSprite.setColor(renderData.exitGridPreview.tint);
            markerSprite.setPosition(sf::Vector2f(static_cast<float>(hexOptional.value().get().x()),
                static_cast<float>(hexOptional.value().get().y())));
            target.draw(markerSprite);
        }
    }
}

void RenderingEngine::drawExitGridPreviewLine(sf::RenderTarget& target, const RenderData& renderData) {
    // An OPEN polyline vertex->vertex with a trailing segment to the live cursor; unlike a region, it is
    // not closed back to the first vertex.
    const auto* vertices = renderData.exitGridPreview.lineVertices;
    if (!vertices || vertices->empty()) {
        return;
    }
    const sf::Color lineColor(renderData.exitGridPreview.tint.r, renderData.exitGridPreview.tint.g,
        renderData.exitGridPreview.tint.b, 255);
    sf::VertexArray line(sf::PrimitiveType::LineStrip);
    for (const sf::Vector2f& vertex : *vertices) {
        line.append(sf::Vertex{ vertex, lineColor });
    }
    line.append(sf::Vertex{ renderData.exitGridPreview.lineCursor, lineColor });
    target.draw(line);
}

} // namespace geck
