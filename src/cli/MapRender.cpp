#include "cli/MapRender.h"

#include "cli/MapLoad.h"
#include "format/map/Map.h"
#include "ui/rendering/MapRenderer.h"

#include <SFML/Graphics/Image.hpp>

#include <exception>
#include <memory>
#include <ostream>

namespace geck::cli {

int renderMap(resource::GameResources& resources, const RenderOptions& options, std::ostream& out) {
    const std::unique_ptr<Map> map = loadMap(resources, options.mapPath);
    if (!map) {
        out << "render: could not read or parse map: " << options.mapPath << "\n";
        return 1;
    }

    MapRenderer::Options renderOptions;
    renderOptions.elevation = options.elevation;
    renderOptions.maxDimension = options.maxDimension;
    renderOptions.showRoof = options.showRoof;
    renderOptions.showObjects = options.showObjects;

    try {
        MapRenderer renderer(resources);
        const sf::Image image = renderer.renderToImage(*map, renderOptions);
        if (!image.saveToFile(options.outPath)) {
            out << "render: failed to write image: " << options.outPath << "\n";
            return 1;
        }
        const sf::Vector2u size = image.getSize();
        out << "wrote " << options.outPath << " (" << size.x << "x" << size.y << ")\n";
    } catch (const std::exception& e) {
        out << "render: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

} // namespace geck::cli
