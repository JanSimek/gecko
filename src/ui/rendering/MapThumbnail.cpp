#include "ui/rendering/MapThumbnail.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <SFML/Graphics/Sprite.hpp>
#include <spdlog/spdlog.h>

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "format/pro/Pro.h"
#include "pattern/PatternSprite.h"
#include "reader/map/MapReader.h"
#include "resource/GameResources.h"
#include "ui/rendering/ThumbnailRenderer.h"
#include "util/ProHelper.h"

namespace geck {

QPixmap MapThumbnail::forMap(const QString& vfsPath, resource::GameResources& resources,
    const HexagonGrid& hexgrid, int size) {
    const QString key = QStringLiteral("map:") + vfsPath + '|' + QString::number(size);
    if (auto hit = ThumbnailRenderer::cached(key)) {
        return *hit;
    }

    const std::filesystem::path path = vfsPath.toStdString();
    const auto bytes = resources.files().readRawBytes(path);
    if (!bytes) {
        return {};
    }

    std::unique_ptr<Map> map;
    try {
        const auto proLoad = [&resources](uint32_t pid) -> Pro* {
            return resources.repository().load<Pro>(ProHelper::basePath(resources, pid));
        };
        MapReader reader(proLoad);
        map = reader.openFile(path.string(), *bytes);
    } catch (const std::exception& e) {
        spdlog::warn("MapThumbnail: failed to parse {}: {}", vfsPath.toStdString(), e.what());
        return {};
    }
    if (!map) {
        return {};
    }

    constexpr int elevation = 0;
    const Map::MapFile& mapFile = map->getMapFile();

    std::vector<sf::Sprite> floorSprites;
    std::vector<sf::Sprite> roofSprites;
    if (const auto it = mapFile.tiles.find(elevation); it != mapFile.tiles.end()) {
        const std::vector<Tile>& tiles = it->second;
        for (int index = 0; index < static_cast<int>(tiles.size()); ++index) {
            if (auto sprite = pattern::buildTileSprite(resources, index, false, tiles[index].getFloor())) {
                floorSprites.push_back(std::move(*sprite));
            }
            if (auto sprite = pattern::buildTileSprite(resources, index, true, tiles[index].getRoof())) {
                roofSprites.push_back(std::move(*sprite));
            }
        }
    }

    std::vector<std::shared_ptr<Object>> objects;
    if (const auto it = mapFile.map_objects.find(elevation); it != mapFile.map_objects.end()) {
        for (const auto& mapObject : it->second) {
            if (!mapObject) {
                continue;
            }
            if (auto object = pattern::buildSpriteObject(
                    resources, hexgrid, mapObject->frm_pid, mapObject->position, mapObject->direction)) {
                objects.push_back(std::move(object));
            }
        }
    }

    // Flatten into draw order: floor tiles, objects, roof tiles.
    std::vector<const sf::Sprite*> ordered;
    ordered.reserve(floorSprites.size() + objects.size() + roofSprites.size());
    for (const sf::Sprite& sprite : floorSprites) {
        ordered.push_back(&sprite);
    }
    for (const auto& object : objects) {
        ordered.push_back(&object->getSprite());
    }
    for (const sf::Sprite& sprite : roofSprites) {
        ordered.push_back(&sprite);
    }

    return ThumbnailRenderer::render(ordered, size, key);
}

} // namespace geck
