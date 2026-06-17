#include "ui/rendering/MapRenderer.h"

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "ui/rendering/MapSpriteLoader.h"
#include "ui/rendering/RenderingEngine.h"

#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/View.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace geck {

namespace {
    // Grow the running bounding box to include one sprite's world-space bounds.
    void expandBounds(const sf::Sprite& sprite, float& minX, float& minY, float& maxX, float& maxY) {
        const sf::FloatRect b = sprite.getGlobalBounds();
        minX = std::min(minX, b.position.x);
        minY = std::min(minY, b.position.y);
        maxX = std::max(maxX, b.position.x + b.size.x);
        maxY = std::max(maxY, b.position.y + b.size.y);
    }
} // namespace

MapRenderer::MapRenderer(resource::GameResources& resources)
    : _resources(resources) {
}

sf::Image MapRenderer::renderToImage(Map& map, const Options& options) {
    // Build the map's sprites headlessly — the same loader the editor uses.
    HexagonGrid hexgrid;
    MapSpriteLoader loader(_resources, hexgrid);
    std::vector<sf::Sprite> floorSprites;
    std::vector<sf::Sprite> roofSprites;
    std::vector<std::shared_ptr<Object>> objects;
    std::vector<sf::Sprite> wallBlockerOverlays;
    loader.loadSprites(map, options.elevation, floorSprites, roofSprites, objects, wallBlockerOverlays);

    // Frame the camera to the bounding box of everything we will draw (floor, optional roof, objects).
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (const auto& sprite : floorSprites) {
        expandBounds(sprite, minX, minY, maxX, maxY);
    }
    if (options.showRoof) {
        for (const auto& sprite : roofSprites) {
            expandBounds(sprite, minX, minY, maxX, maxY);
        }
    }
    if (options.showObjects) {
        for (const auto& object : objects) {
            if (object) {
                expandBounds(object->getSprite(), minX, minY, maxX, maxY);
            }
        }
    }
    if (minX > maxX || minY > maxY) {
        throw std::runtime_error("map has nothing to render at elevation " + std::to_string(options.elevation));
    }

    constexpr float pad = 8.0f;
    minX -= pad;
    minY -= pad;
    maxX += pad;
    maxY += pad;
    const float bboxWidth = std::max(1.0f, maxX - minX);
    const float bboxHeight = std::max(1.0f, maxY - minY);

    // Fit the (possibly huge) world bounds into maxDimension; the view still covers the full bounds,
    // so the content is supersampled down into the texture (same approach as ThumbnailRenderer).
    const float maxDim = static_cast<float>(std::max(1u, options.maxDimension));
    const float scale = std::min(1.0f, maxDim / std::max(bboxWidth, bboxHeight));
    const auto width = static_cast<unsigned int>(std::max(1.0f, bboxWidth * scale));
    const auto height = static_cast<unsigned int>(std::max(1.0f, bboxHeight * scale));

    sf::RenderTexture renderTexture;
    if (!renderTexture.resize({ width, height })) {
        throw std::runtime_error("could not create a " + std::to_string(width) + "x" + std::to_string(height)
            + " off-screen render target — no GL context (headless without a display?)");
    }
    renderTexture.setView(sf::View(sf::FloatRect({ minX, minY }, { bboxWidth, bboxHeight })));
    renderTexture.clear(options.background);

    RenderingEngine engine(_resources);
    RenderingEngine::RenderData data;
    data.floorSprites = &floorSprites;
    data.roofSprites = &roofSprites;
    data.objects = &objects;
    data.wallBlockerOverlays = &wallBlockerOverlays;
    data.hexGrid = &hexgrid;
    data.map = &map;
    data.currentElevation = options.elevation;

    // A clean render of the map's artwork: no selection, no debug overlays, no grid.
    RenderingEngine::VisibilitySettings visibility;
    visibility.showObjects = options.showObjects;
    visibility.showRoof = options.showRoof;
    visibility.showScrollBlockers = false;
    visibility.showWallBlockers = false;
    visibility.showHexGrid = false;
    visibility.showLightOverlays = false;
    visibility.showExitGrids = false;

    engine.render(renderTexture, renderTexture.getView(), data, visibility);
    renderTexture.display();
    return renderTexture.getTexture().copyToImage();
}

} // namespace geck
