#include "cli/MapRender.h"

#include "cli/MapLoad.h"
#include "format/map/Map.h"
#include "rendering/MapRenderer.h"

#include <SFML/Graphics/Image.hpp>

#include <exception>
#include <format>
#include <memory>
#include <ostream>

namespace geck::cli {

namespace {
    std::string hexColor(const sf::Color& c) {
        return std::format("#{:02X}{:02X}{:02X}", c.r, c.g, c.b);
    }

    // Print the schematic's colour key so an agent can map what it sees to the analyze JSON: floor
    // entries by raw tile-id (join id -> name via `analyze`), object entries by engine type.
    void printLegend(const MapRenderer::Legend& legend, std::ostream& out) {
        out << "schematic legend (join floor id -> name via `analyze`):\n";
        for (const auto& entry : legend.floors) {
            out << "  floor   " << hexColor(entry.color) << "  id " << entry.id
                << "  (" << entry.count << " tiles)\n";
        }
        for (const auto& entry : legend.objects) {
            out << "  object  " << hexColor(entry.color) << "  " << entry.type
                << "  (" << entry.count << ")\n";
        }
    }
} // namespace

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
    renderOptions.showBlockers = options.showBlockers;
    renderOptions.fullExtent = options.fullExtent;
    renderOptions.style = options.semantic ? MapRenderer::Style::Semantic
        : options.objects                  ? MapRenderer::Style::Objects
        : options.schematic                ? MapRenderer::Style::Schematic
                                           : MapRenderer::Style::Natural;

    const bool wantLegend = options.schematic || options.objects || options.semantic;
    try {
        MapRenderer renderer(resources);
        MapRenderer::Legend legend;
        const sf::Image image = renderer.renderToImage(*map, renderOptions, wantLegend ? &legend : nullptr);
        if (!image.saveToFile(options.outPath)) {
            out << "render: failed to write image: " << options.outPath << "\n";
            return 1;
        }
        const sf::Vector2u size = image.getSize();
        out << "wrote " << options.outPath << " (" << size.x << "x" << size.y << ")\n";
        if (wantLegend) {
            printLegend(legend, out);
        }
    } catch (const std::exception& e) {
        out << "render: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

} // namespace geck::cli
