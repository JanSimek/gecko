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

// Identity of a thumbnail: the VFS path and requested size, plus the byte-size and mtime
// of the file that PROVIDES the map — the .map itself for a loose file, the archive for a
// DAT-contained one. Editing or re-saving a map therefore mints a new identity (loose maps
// individually, DAT maps together with their archive), and both the in-memory and the
// on-disk cache key off it, so a mid-session save is re-rendered rather than served stale.
// Deliberately not a content hash: a cache hit must not need to read the map's bytes.
QString MapThumbnail::identity(const QString& vfsPath, int size, resource::GameResources& resources) {
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
    hash.addData("thumb-v2"); // renderer-version salt: bump to orphan all previously cached output
    hash.addData(vfsPath.toUtf8());
    hash.addData(QByteArray::number(size));
    hash.addData(QByteArray::number(info.size()));
    hash.addData(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
    return QString::fromLatin1(hash.result().toHex());
}

QString MapThumbnail::diskCachePath(const QString& identity) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/thumbnails");
    QDir().mkpath(dir);
    return dir + '/' + identity + QStringLiteral(".png");
}

std::optional<QPixmap> MapThumbnail::fromCache(const QString& vfsPath,
    resource::GameResources& resources, int size) {
    const QString id = identity(vfsPath, size, resources);
    const QString key = id.isEmpty()
        ? QStringLiteral("map:") + vfsPath + '|' + QString::number(size)
        : QStringLiteral("map:") + id;
    if (auto hit = ThumbnailRenderer::cached(key)) {
        return hit;
    }

    const QString cacheFile = id.isEmpty() ? QString() : diskCachePath(id);
    if (!cacheFile.isEmpty() && QFileInfo::exists(cacheFile)) {
        QPixmap fromDisk;
        if (fromDisk.load(cacheFile, "PNG")) {
            QPixmapCache::insert(key, fromDisk);
            return fromDisk;
        }
    }
    return std::nullopt;
}

QPixmap MapThumbnail::forMap(const QString& vfsPath, resource::GameResources& resources,
    const HexagonGrid& hexgrid, int size) {
    if (auto hit = fromCache(vfsPath, resources, size)) {
        return *hit;
    }

    const QString id = identity(vfsPath, size, resources);
    const QString key = id.isEmpty()
        ? QStringLiteral("map:") + vfsPath + '|' + QString::number(size)
        : QStringLiteral("map:") + id;
    const QString cacheFile = id.isEmpty() ? QString() : diskCachePath(id);

    const QImage rendered = renderImage(vfsPath, resources, hexgrid, size);
    if (rendered.isNull()) {
        return {};
    }

    QPixmap pixmap = QPixmap::fromImage(rendered);
    QPixmapCache::insert(key, pixmap);
    if (!cacheFile.isEmpty() && !rendered.save(cacheFile, "PNG")) {
        spdlog::debug("MapThumbnail: could not persist thumbnail for {}", vfsPath.toStdString());
    }
    return pixmap;
}

QImage MapThumbnail::renderImage(const QString& vfsPath, resource::GameResources& resources,
    const HexagonGrid& hexgrid, int size) {
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

    return pattern::composeThumbnailImage(floorSprites, objects, roofSprites, size);
}

} // namespace geck
