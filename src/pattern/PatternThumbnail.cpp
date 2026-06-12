#include "pattern/PatternThumbnail.h"

#include <memory>
#include <vector>

#include <SFML/Graphics/Sprite.hpp>

#include <QDateTime>
#include <QFileInfo>

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "pattern/Pattern.h"
#include "pattern/PatternSprite.h"
#include "pattern/PatternStamper.h"
#include "ui/rendering/ThumbnailRenderer.h"

namespace geck::pattern {

namespace {

    // In-memory cache key, invalidated by the source file's mtime. Only a handful of
    // small thumbnails are live at a time, so they are rendered on demand and kept in
    // the shared in-memory cache rather than written to disk.
    QString cacheKey(const QString& sourcePath, int size) {
        const qint64 mtime = QFileInfo(sourcePath).lastModified().toSecsSinceEpoch();
        return sourcePath + '|' + QString::number(mtime) + '|' + QString::number(size);
    }

} // namespace

QPixmap PatternThumbnail::forPattern(const Pattern& pattern, const QString& sourcePath,
    resource::GameResources& resources, const HexagonGrid& hexgrid, int size) {
    const QString key = sourcePath.isEmpty() ? QString() : cacheKey(sourcePath, size);
    if (auto hit = ThumbnailRenderer::cached(key)) {
        return *hit;
    }

    if (pattern.variants.empty()) {
        return {};
    }

    // Build positioned sprites for variant 0 at its authored positions: floor and roof
    // tiles plus objects. They are kept alive here while ThumbnailRenderer draws them.
    const PatternVariant& variant = pattern.variants.front();
    const PatternStamper::Plan plan = PatternStamper::plan(variant, variant.anchorHex);

    std::vector<sf::Sprite> floorSprites;
    std::vector<sf::Sprite> roofSprites;
    for (const PatternStamper::TilePlacement& tp : plan.tiles) {
        if (auto sprite = buildTileSprite(resources, tp.tileIndex, tp.isRoof, tp.tileId)) {
            (tp.isRoof ? roofSprites : floorSprites).push_back(std::move(*sprite));
        }
    }

    std::vector<std::shared_ptr<Object>> objects;
    for (const PatternStamper::ObjectPlacement& op : plan.objects) {
        if (auto object = buildSpriteObject(resources, hexgrid, op.frmPid, op.hex, op.direction)) {
            objects.push_back(std::move(object));
        }
    }

    return composeThumbnail(floorSprites, objects, roofSprites, size, key);
}

} // namespace geck::pattern
