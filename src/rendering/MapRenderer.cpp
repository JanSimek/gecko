#include "rendering/MapRenderer.h"

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "editor/Reachability.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "rendering/MapSpriteLoader.h"
#include "rendering/RenderingEngine.h"
#include "util/Constants.h"
#include "util/TileUtils.h"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/PrimitiveType.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/Graphics/View.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace geck {

namespace {
    // A bounding box that grows as content is added; starts inverted so the first point sets it.
    struct Bounds {
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();

        void add(float x0, float y0, float x1, float y1) {
            minX = std::min(minX, x0);
            minY = std::min(minY, y0);
            maxX = std::max(maxX, x1);
            maxY = std::max(maxY, y1);
        }
        void add(const sf::Sprite& sprite) {
            const sf::FloatRect b = sprite.getGlobalBounds();
            add(b.position.x, b.position.y, b.position.x + b.size.x, b.position.y + b.size.y);
        }
        [[nodiscard]] bool empty() const { return minX > maxX || minY > maxY; }
    };

    // Texture size + world-space view that fit `bounds` into `maxDimension` (longest side), padded.
    struct Frame {
        unsigned int width = 1;
        unsigned int height = 1;
        sf::View view;
    };
    Frame computeFrame(Bounds bounds, unsigned int maxDimension) {
        constexpr float pad = 8.0f;
        bounds.add(bounds.minX - pad, bounds.minY - pad, bounds.maxX + pad, bounds.maxY + pad);
        const float bboxWidth = std::max(1.0f, bounds.maxX - bounds.minX);
        const float bboxHeight = std::max(1.0f, bounds.maxY - bounds.minY);
        const float maxDim = static_cast<float>(std::max(1u, maxDimension));
        const float scale = std::min(1.0f, maxDim / std::max(bboxWidth, bboxHeight));
        Frame frame;
        frame.width = static_cast<unsigned int>(std::max(1.0f, bboxWidth * scale));
        frame.height = static_cast<unsigned int>(std::max(1.0f, bboxHeight * scale));
        frame.view = sf::View(sf::FloatRect({ bounds.minX, bounds.minY }, { bboxWidth, bboxHeight }));
        return frame;
    }

    // An off-screen render target sized to `frame`, or a runtime_error when no GL context is available.
    std::unique_ptr<sf::RenderTexture> makeTarget(const Frame& frame) {
        auto target = std::make_unique<sf::RenderTexture>();
        if (!target->resize({ frame.width, frame.height })) {
            throw std::runtime_error("could not create a " + std::to_string(frame.width) + "x"
                + std::to_string(frame.height)
                + " off-screen render target — no GL context (headless without a display?)");
        }
        target->setView(frame.view);
        return target;
    }

    sf::Color hsvToRgb(float h, float s, float v) {
        const float sixth = std::floor(h * 6.0f);
        const float f = h * 6.0f - sixth;
        const float p = v * (1.0f - s);
        const float q = v * (1.0f - f * s);
        const float t = v * (1.0f - (1.0f - f) * s);
        float r = v;
        float g = t;
        float b = p;
        switch (static_cast<int>(sixth) % 6) {
            case 1:
                r = q, g = v, b = p;
                break;
            case 2:
                r = p, g = v, b = t;
                break;
            case 3:
                r = p, g = q, b = v;
                break;
            case 4:
                r = t, g = p, b = v;
                break;
            case 5:
                r = v, g = p, b = q;
                break;
            default:
                break; // case 0
        }
        const auto byte = [](float c) { return static_cast<std::uint8_t>(std::lround(c * 255.0f)); };
        return sf::Color(byte(r), byte(g), byte(b));
    }

    // Visually distinct colours by index: the golden-ratio hue rotation spreads neighbours apart so
    // adjacent ranks don't look alike. Stable for a given index, so the same rank is always the same hue.
    sf::Color distinctColor(int index) {
        const float hue = std::fmod(static_cast<float>(index) * 0.618033988749895f, 1.0f);
        return hsvToRgb(hue, 0.62f, 0.95f);
    }

    // A fixed colour per object category, so the same kind of object always reads the same.
    sf::Color categoryColor(Pro::OBJECT_TYPE type) {
        switch (type) {
            case Pro::OBJECT_TYPE::ITEM:
                return sf::Color(235, 180, 60);
            case Pro::OBJECT_TYPE::CRITTER:
                return sf::Color(224, 80, 80);
            case Pro::OBJECT_TYPE::SCENERY:
                return sf::Color(96, 200, 120);
            case Pro::OBJECT_TYPE::WALL:
                return sf::Color(96, 150, 235);
            case Pro::OBJECT_TYPE::MISC:
                return sf::Color(200, 120, 220);
            default:
                return sf::Color(200, 200, 200);
        }
    }

    // Append one tile cell (two triangles) for the tile whose 80x36 art box has top-left (x, y).
    // The cell is the iso-lattice parallelogram around the tile centre, spanned by the grid's
    // half-basis vectors — the screen step for +1 column (u) and +1 row (v). Using these (not an
    // axis-aligned 80x36 diamond) makes neighbouring cells abut exactly, so regions fill solidly
    // instead of leaving gaps.
    void appendCell(sf::VertexArray& vertices, float x, float y, sf::Color color) {
        const sf::Vector2f center{ x + static_cast<float>(TILE_WIDTH) / 2.0f,
            y + static_cast<float>(TILE_HEIGHT) / 2.0f };
        const sf::Vector2f uHalf{ static_cast<float>(TILE_Y_OFFSET_LARGE) / 2.0f,
            static_cast<float>(TILE_Y_OFFSET_SMALL) / 2.0f }; // +1 column
        const sf::Vector2f vHalf{ -static_cast<float>(TILE_X_OFFSET) / 2.0f,
            static_cast<float>(TILE_Y_OFFSET_TINY) / 2.0f }; // +1 row
        const sf::Vector2f a = center + uHalf + vHalf;
        const sf::Vector2f b = center + uHalf - vHalf;
        const sf::Vector2f c = center - uHalf - vHalf;
        const sf::Vector2f d = center - uHalf + vHalf;
        for (const sf::Vector2f& point : { a, b, c }) {
            vertices.append(sf::Vertex{ point, color });
        }
        for (const sf::Vector2f& point : { a, c, d }) {
            vertices.append(sf::Vertex{ point, color });
        }
    }

    constexpr uint16_t kEmptyTile = static_cast<uint16_t>(Map::EMPTY_TILE);

    // Grow `bounds` to the FULL floor-tile grid's screen extent — the whole iso playable area — using
    // the same tile->screen projection the renderer uses to place floor tiles. The screen X and Y of a
    // tile are both monotonic in (row, col), so the four grid corners bound the entire 100x100 grid.
    // Independent of map content, so an empty/sparse map still frames to the whole grid.
    void addFullGridBounds(Bounds& bounds) {
        constexpr int lastRow = MAP_HEIGHT - 1;
        constexpr int lastCol = MAP_WIDTH - 1;
        for (const int row : { 0, lastRow }) {
            for (const int col : { 0, lastCol }) {
                const ScreenPosition pos = indexToScreenPosition(row * MAP_WIDTH + col);
                const auto x = static_cast<float>(pos.x);
                const auto y = static_cast<float>(pos.y);
                bounds.add(x, y, x + static_cast<float>(TILE_WIDTH), y + static_cast<float>(TILE_HEIGHT));
            }
        }
    }

    // Grow `bounds` to the non-empty tiles of one layer (floor or roof). Empty cells are excluded so
    // the frame fits the map's actual content — the loader emits a blank sprite for every empty cell
    // across the full 100x100 grid, and framing to those would shrink the content into a sea of black.
    void addTileContentBounds(Bounds& bounds, const std::vector<Tile>& tiles, bool roof) {
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            const uint16_t id = roof ? tiles[i].getRoof() : tiles[i].getFloor();
            if (id == kEmptyTile) {
                continue;
            }
            const ScreenPosition pos = indexToScreenPosition(static_cast<int>(i), roof);
            const auto x = static_cast<float>(pos.x);
            const auto y = static_cast<float>(pos.y);
            bounds.add(x, y, x + static_cast<float>(TILE_WIDTH), y + static_cast<float>(TILE_HEIGHT));
        }
    }

    // A stable, distinct colour per floor-tile id, most-common id first, also recorded in the legend.
    std::unordered_map<uint16_t, sf::Color> assignFloorColors(const std::vector<Tile>& tiles, MapRenderer::Legend* legend) {
        std::map<uint16_t, int> counts;
        for (const auto& tile : tiles) {
            if (tile.getFloor() != kEmptyTile) {
                counts[tile.getFloor()]++;
            }
        }
        std::vector<std::pair<uint16_t, int>> ranked(counts.begin(), counts.end());
        std::ranges::sort(ranked, [](const auto& a, const auto& b) { return a.second > b.second; });

        std::unordered_map<uint16_t, sf::Color> colors;
        for (std::size_t rank = 0; rank < ranked.size(); ++rank) {
            const sf::Color color = distinctColor(static_cast<int>(rank));
            colors[ranked[rank].first] = color;
            if (legend != nullptr) {
                legend->floors.push_back({ ranked[rank].first, color, ranked[rank].second });
            }
        }
        return colors;
    }

    // Every present floor id mapped to one muted grey — the floor for the Objects style, so the
    // category-coloured object markers stand out instead of competing with a per-id rainbow.
    std::unordered_map<uint16_t, sf::Color> uniformFloorColors(const std::vector<Tile>& tiles) {
        const sf::Color grey(72, 68, 62);
        std::unordered_map<uint16_t, sf::Color> colors;
        for (const auto& tile : tiles) {
            if (tile.getFloor() != kEmptyTile) {
                colors[tile.getFloor()] = grey;
            }
        }
        return colors;
    }

    // Build the flat-coloured floor mesh and grow `bounds` to the drawn cells.
    void appendFloorMesh(sf::VertexArray& mesh, Bounds& bounds, const std::vector<Tile>& tiles,
        const std::unordered_map<uint16_t, sf::Color>& colors) {
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            const uint16_t floorId = tiles[i].getFloor();
            if (floorId == kEmptyTile) {
                continue;
            }
            const ScreenPosition pos = indexToScreenPosition(static_cast<int>(i));
            const auto x = static_cast<float>(pos.x);
            const auto y = static_cast<float>(pos.y);
            appendCell(mesh, x, y, colors.at(floorId));
            bounds.add(x, y, x + static_cast<float>(TILE_WIDTH), y + static_cast<float>(TILE_HEIGHT));
        }
    }

    // Whether a proto is FLAT (the OBJECT_FLAT flag) — the invisible engine blockers (hex/scroll
    // blockers, exit grids) carry it. Cached per pid since a map repeats the same pids heavily.
    bool isFlatProto(resource::GameResources& resources, uint32_t pid, std::unordered_map<uint32_t, bool>& cache) {
        if (const auto it = cache.find(pid); it != cache.end()) {
            return it->second;
        }
        bool flat = false;
        try {
            if (const Pro* pro = resources.loadPro(pid); pro != nullptr) {
                flat = Pro::hasFlag(pro->header.flags, Pro::ObjectFlags::OBJECT_FLAT);
            }
        } catch (const std::exception&) {
            // unknown proto -> treat as not flat, so it still shows
        }
        cache[pid] = flat;
        return flat;
    }

    // Draw a category-coloured dot at each object's centre, and record the per-category legend.
    // FLAT objects (invisible engine blockers) are skipped unless showBlockers.
    void drawObjectMarkers(sf::RenderTexture& target, const std::vector<std::shared_ptr<Object>>& objects,
        resource::GameResources& resources, bool showBlockers, MapRenderer::Legend* legend) {
        std::unordered_map<uint32_t, bool> flatCache;
        std::map<uint32_t, int> typeCounts; // engine type value -> count
        for (const auto& object : objects) {
            if (!object || !object->hasMapObject()) {
                continue;
            }
            const uint32_t pid = object->getMapObjectPtr()->pro_pid;
            if (!showBlockers && isFlatProto(resources, pid, flatCache)) {
                continue;
            }
            const Pro::OBJECT_TYPE type = Pro::typeOfPid(pid);
            typeCounts[static_cast<uint32_t>(type)]++;
            const sf::FloatRect b = object->getSprite().getGlobalBounds();
            constexpr float radius = 5.0f;
            sf::CircleShape marker(radius);
            marker.setOrigin({ radius, radius });
            marker.setPosition({ b.position.x + b.size.x / 2.0f, b.position.y + b.size.y / 2.0f });
            marker.setFillColor(categoryColor(type));
            marker.setOutlineColor(sf::Color(20, 20, 20, 200));
            marker.setOutlineThickness(1.0f);
            target.draw(marker);
        }
        if (legend != nullptr) {
            for (const auto& [typeValue, count] : typeCounts) {
                const auto type = static_cast<Pro::OBJECT_TYPE>(typeValue);
                legend->objects.push_back({ Pro::typeToString(type), categoryColor(type), count });
            }
            std::ranges::sort(legend->objects, [](const auto& a, const auto& b) { return a.count > b.count; });
        }
    }

    // Semantic overlay: colour each object marker by its *role* — exit grids highlighted (and always
    // shown, though flat), critters by team (group_id), everything else by category — and ring any
    // object that carries a map script. The legend keys on the role so it joins back to describe_map.
    struct SemanticRole {
        sf::Color color;
        std::string label;
        float radius;
    };

    SemanticRole semanticRoleFor(const MapObject& mo, Pro::OBJECT_TYPE type) {
        if (mo.isExitGridMarker()) {
            return { sf::Color(64, 210, 235), "exit grid", 6.0f };
        }
        if (type == Pro::OBJECT_TYPE::CRITTER) {
            return { distinctColor(static_cast<int>(mo.group_id)), "critter team " + std::to_string(mo.group_id), 5.0f };
        }
        return { categoryColor(type), Pro::typeToString(type), 5.0f };
    }

    // Exit grids are flat (engine blockers) but semantically important, so they show even when other
    // flat objects are hidden.
    bool isHiddenFlatObject(const MapObject& mo, bool showBlockers, resource::GameResources& resources,
        std::unordered_map<uint32_t, bool>& flatCache) {
        return !showBlockers && !mo.isExitGridMarker() && isFlatProto(resources, mo.pro_pid, flatCache);
    }

    void fillSemanticLegend(MapRenderer::Legend& legend,
        const std::map<std::string, std::pair<sf::Color, int>>& roles, int scriptedCount) {
        for (const auto& [role, colorCount] : roles) {
            legend.objects.push_back({ role, colorCount.first, colorCount.second });
        }
        if (scriptedCount > 0) {
            legend.objects.push_back({ "scripted (yellow ring)", sf::Color(245, 225, 70), scriptedCount });
        }
        std::ranges::sort(legend.objects, [](const auto& a, const auto& b) { return a.count > b.count; });
    }

    void drawSemanticMarkers(sf::RenderTexture& target, const std::vector<std::shared_ptr<Object>>& objects,
        resource::GameResources& resources, bool showBlockers, MapRenderer::Legend* legend,
        const HexagonGrid& hexgrid) {
        std::unordered_map<uint32_t, bool> flatCache;
        std::map<std::string, std::pair<sf::Color, int>> roles; // role -> {colour, count}
        int scriptedCount = 0;
        for (const auto& object : objects) {
            if (!object || !object->hasMapObject()) {
                continue;
            }
            const auto mo = object->getMapObjectPtr();
            if (isHiddenFlatObject(*mo, showBlockers, resources, flatCache)) {
                continue;
            }

            const sf::FloatRect b = object->getSprite().getGlobalBounds();
            sf::Vector2f center{ b.position.x + b.size.x / 2.0f, b.position.y + b.size.y / 2.0f };
            // Exit-grid markers carry a display slide (their bar is pushed off the hex), so mark the true
            // TRIGGER hex, not the slid sprite's centre — the dot then sits on the saved hex position.
            if (mo->isExitGridMarker()) {
                if (const auto h = hexgrid.getHexByPosition(static_cast<uint32_t>(mo->position)); h.has_value()) {
                    center = { static_cast<float>(h->get().x()), static_cast<float>(h->get().y()) };
                }
            }
            const SemanticRole role = semanticRoleFor(*mo, Pro::typeOfPid(mo->pro_pid));

            sf::CircleShape marker(role.radius);
            marker.setOrigin({ role.radius, role.radius });
            marker.setPosition(center);
            marker.setFillColor(role.color);
            marker.setOutlineColor(sf::Color(20, 20, 20, 200));
            marker.setOutlineThickness(1.0f);
            target.draw(marker);

            if (mo->map_scripts_pid >= 0) { // has an attached map script — ring it
                const float ringRadius = role.radius + 3.0f;
                sf::CircleShape ring(ringRadius);
                ring.setOrigin({ ringRadius, ringRadius });
                ring.setPosition(center);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineColor(sf::Color(245, 225, 70));
                ring.setOutlineThickness(1.5f);
                target.draw(ring);
                ++scriptedCount;
            }

            auto& entry = roles[role.label];
            entry.first = role.color;
            ++entry.second;
        }
        if (legend != nullptr) {
            fillSemanticLegend(*legend, roles, scriptedCount);
        }
    }

    // Overlay a small magenta dot on each exit-grid marker's TRIGGER hex (its saved hex position), so a
    // natural render of the diagonal band shows the hex sitting on the band's outer edge.
    void drawExitGridTriggerDots(sf::RenderTarget& target,
        const std::vector<std::shared_ptr<Object>>& objects, const HexagonGrid& hexgrid) {
        for (const auto& object : objects) {
            if (!object || !object->hasMapObject()) {
                continue;
            }
            const auto mo = object->getMapObjectPtr();
            if (!mo->isExitGridMarker()) {
                continue;
            }
            const auto h = hexgrid.getHexByPosition(static_cast<uint32_t>(mo->position));
            if (!h.has_value()) {
                continue;
            }
            constexpr float radius = 3.0f;
            sf::CircleShape dot(radius);
            dot.setOrigin({ radius, radius });
            dot.setPosition({ static_cast<float>(h->get().x()), static_cast<float>(h->get().y()) });
            dot.setFillColor(sf::Color(255, 0, 255));
            dot.setOutlineColor(sf::Color(20, 20, 20, 220));
            dot.setOutlineThickness(1.0f);
            target.draw(dot);
        }
    }
} // namespace

MapRenderer::MapRenderer(resource::GameResources& resources)
    : _resources(resources) {
}

sf::Image MapRenderer::renderToImage(Map& map, const Options& options, Legend* legend) {
    if (options.style == Style::Schematic || options.style == Style::Objects || options.style == Style::Semantic) {
        return renderSchematic(map, options, legend);
    }
    return renderNatural(map, options);
}

sf::Image MapRenderer::renderNatural(Map& map, const Options& options) {
    // Build the map's sprites headlessly — the same loader the editor uses.
    HexagonGrid hexgrid;
    MapSpriteLoader loader(_resources, hexgrid);
    std::vector<sf::Sprite> floorSprites;
    std::vector<sf::Sprite> roofSprites;
    std::vector<std::shared_ptr<Object>> objects;
    std::vector<sf::Sprite> wallBlockerOverlays;
    loader.loadSprites(map, options.elevation, floorSprites, roofSprites, objects, wallBlockerOverlays);

    // Frame to the map's *content*: non-empty tiles plus objects. (We still draw the blank sprites the
    // loader made for empty cells — they just fall outside this frame instead of dominating it.)
    // With fullExtent, seed the bounds with the whole floor-tile grid so even an empty map shows it all.
    Bounds bounds;
    if (options.fullExtent) {
        addFullGridBounds(bounds);
    }
    const auto& allTiles = map.getMapFile().tiles;
    if (const auto it = allTiles.find(options.elevation); it != allTiles.end()) {
        addTileContentBounds(bounds, it->second, false);
        if (options.showRoof) {
            addTileContentBounds(bounds, it->second, true);
        }
    }
    if (options.showObjects) {
        for (const auto& object : objects) {
            if (object) {
                bounds.add(object->getSprite());
            }
        }
    }
    if (bounds.empty()) {
        throw std::runtime_error("map has nothing to render at elevation " + std::to_string(options.elevation));
    }

    const std::unique_ptr<sf::RenderTexture> target = makeTarget(computeFrame(bounds, options.maxDimension));
    target->clear(options.background);

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

    // Unreachable-areas shading (opt-in): the same geck::reachability flood-fill the editor overlay
    // and the `reachability` tool use, tinted onto the render so walled-off regions are visible.
    std::vector<int> unreachableHexes;
    if (options.showUnreachable) {
        const auto& mf = map.getMapFile();
        static const std::vector<std::shared_ptr<MapObject>> kNoObjects;
        const auto it = mf.map_objects.find(options.elevation);
        const auto& objs = it != mf.map_objects.end() ? it->second : kNoObjects;
        const auto result = reachability::analyzeElevation(_resources,
            static_cast<int>(mf.header.player_default_elevation),
            static_cast<int>(mf.header.player_default_position), options.elevation, objs);
        unreachableHexes = reachability::unreachableWalkableHexes(result);
        data.unreachableHexes = &unreachableHexes;
        visibility.showUnreachable = true;
    }

    engine.render(*target, target->getView(), data, visibility);
    if (options.exitDots) {
        drawExitGridTriggerDots(*target, objects, hexgrid);
    }
    target->display();
    return target->getTexture().copyToImage();
}

sf::Image MapRenderer::renderSchematic(Map& map, const Options& options, Legend* legend) {
    const auto& allTiles = map.getMapFile().tiles;
    const auto tilesIt = allTiles.find(options.elevation);
    const std::vector<Tile> noTiles;
    const std::vector<Tile>& tiles = tilesIt != allTiles.end() ? tilesIt->second : noTiles;

    const bool semantic = options.style == Style::Semantic;
    const std::unordered_map<uint16_t, sf::Color> floorColor = (options.style == Style::Objects || semantic)
        ? uniformFloorColors(tiles)
        : assignFloorColors(tiles, legend);

    // Objects (markers) — reuse the loader for their screen positions; colour by engine category.
    HexagonGrid hexgrid;
    MapSpriteLoader loader(_resources, hexgrid);
    std::vector<std::shared_ptr<Object>> objects;
    if (options.showObjects) {
        std::vector<sf::Sprite> wallBlockerOverlays; // unused for the schematic, but the loader fills it
        loader.loadObjectSprites(map, options.elevation, objects, wallBlockerOverlays);
    }

    // Flat-coloured floor mesh, plus the content bounds (floor cells and object sprites). With
    // fullExtent, seed the bounds with the whole floor-tile grid so even an empty map shows it all.
    sf::VertexArray floor(sf::PrimitiveType::Triangles);
    Bounds bounds;
    if (options.fullExtent) {
        addFullGridBounds(bounds);
    }
    appendFloorMesh(floor, bounds, tiles, floorColor);
    for (const auto& object : objects) {
        if (object) {
            bounds.add(object->getSprite());
        }
    }
    if (bounds.empty()) {
        throw std::runtime_error("map has nothing to render at elevation " + std::to_string(options.elevation));
    }

    const std::unique_ptr<sf::RenderTexture> target = makeTarget(computeFrame(bounds, options.maxDimension));
    target->clear(options.background);
    target->draw(floor);
    if (semantic) {
        drawSemanticMarkers(*target, objects, _resources, options.showBlockers, legend, hexgrid);
    } else {
        drawObjectMarkers(*target, objects, _resources, options.showBlockers, legend);
    }
    target->display();
    return target->getTexture().copyToImage();
}

} // namespace geck
