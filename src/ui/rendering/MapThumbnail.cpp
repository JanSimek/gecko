#include "ui/rendering/MapThumbnail.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QPixmapCache>
#include <QStandardPaths>
#include <SFML/Graphics/Sprite.hpp>
#include <spdlog/spdlog.h>

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "format/pro/Pro.h"
#include "pattern/PatternSprite.h"
#include "pattern/ThumbnailComposer.h"
#include "reader/map/MapReader.h"
#include "resource/DataFileSystem.h"
#include "resource/GameResources.h"
#include "ui/rendering/ThumbnailRenderer.h"

namespace geck {

namespace {

    // Rendering a thumbnail is a mini map-load (parse + sprites + offscreen GL render), so the
    // result is persisted to disk keyed by the providing source's identity and mtime: a map
    // inside a DAT invalidates when the DAT changes, a loose map when the file itself does.
    // Every browse after the first — including across sessions — then costs a PNG load.
    QString diskCachePath(const QString& vfsPath, int size, resource::GameResources& resources) {
        const auto source = resources.files().sourceInfo(vfsPath.toStdString());
        if (!source) {
            return {};
        }

        QString onDisk = QString::fromStdString(source->sourcePath.generic_string());
        if (source->kind == resource::MountedSourceInfo::Kind::Directory) {
            QString relative = vfsPath;
            while (relative.startsWith('/')) {
                relative = relative.mid(1);
            }
            onDisk += '/' + relative;
        }

        const QFileInfo info(onDisk);
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(vfsPath.toUtf8());
        hash.addData(QByteArray::number(size));
        hash.addData(QByteArray::number(info.size()));
        hash.addData(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));

        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/thumbnails");
        QDir().mkpath(dir);
        return dir + '/' + QString::fromLatin1(hash.result().toHex()) + QStringLiteral(".png");
    }

} // namespace

QPixmap MapThumbnail::forMap(const QString& vfsPath, resource::GameResources& resources,
    const HexagonGrid& hexgrid, int size) {
    const QString key = QStringLiteral("map:") + vfsPath + '|' + QString::number(size);
    if (auto hit = ThumbnailRenderer::cached(key)) {
        return *hit;
    }

    const QString cacheFile = diskCachePath(vfsPath, size, resources);
    if (!cacheFile.isEmpty() && QFileInfo::exists(cacheFile)) {
        QPixmap fromDisk;
        if (fromDisk.load(cacheFile, "PNG")) {
            QPixmapCache::insert(key, fromDisk);
            return fromDisk;
        }
    }

    const std::filesystem::path path = vfsPath.toStdString();
    const auto bytes = resources.files().readRawBytes(path);
    if (!bytes) {
        return {};
    }

    std::unique_ptr<Map> map;
    try {
        const auto proLoad = [&resources](uint32_t pid) {
            return resources.loadPro(pid);
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

    const QPixmap thumbnail = pattern::composeThumbnail(floorSprites, objects, roofSprites, size, key);
    if (!thumbnail.isNull() && !cacheFile.isEmpty() && !thumbnail.save(cacheFile, "PNG")) {
        spdlog::debug("MapThumbnail: could not persist thumbnail for {}", vfsPath.toStdString());
    }
    return thumbnail;
}

} // namespace geck
