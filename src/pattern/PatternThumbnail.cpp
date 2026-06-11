#include "pattern/PatternThumbnail.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPixmapCache>

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "pattern/Pattern.h"
#include "pattern/PatternSprite.h"
#include "pattern/PatternStamper.h"

namespace geck::pattern {

namespace {

    // In-memory cache key, invalidated by the source file's mtime. Only a handful of
    // small thumbnails are live at a time, so they are rendered on demand and kept in
    // Qt's global pixmap cache rather than written to disk.
    QString cacheKey(const QString& sourcePath, int size) {
        const qint64 mtime = QFileInfo(sourcePath).lastModified().toSecsSinceEpoch();
        return sourcePath + '|' + QString::number(mtime) + '|' + QString::number(size);
    }

    // Copy an SFML RGBA image into an owned QImage.
    QImage toQImage(const sf::Image& image) {
        const sf::Vector2u sz = image.getSize();
        return QImage(image.getPixelsPtr(), static_cast<int>(sz.x), static_cast<int>(sz.y),
            QImage::Format_RGBA8888)
            .copy();
    }

} // namespace

QPixmap PatternThumbnail::forPattern(const Pattern& pattern, const QString& sourcePath,
    resource::GameResources& resources, const HexagonGrid& hexgrid, int size) {
    const QString key = sourcePath.isEmpty() ? QString() : cacheKey(sourcePath, size);
    if (!key.isEmpty()) {
        QPixmap cached;
        if (QPixmapCache::find(key, &cached)) {
            return cached;
        }
    }

    if (pattern.variants.empty()) {
        return {};
    }

    // Build positioned sprites for variant 0 at its authored hexes.
    const PatternVariant& variant = pattern.variants.front();
    const PatternStamper::Plan plan = PatternStamper::plan(variant, variant.anchorHex);
    std::vector<std::shared_ptr<Object>> objects;
    for (const PatternStamper::ObjectPlacement& op : plan.objects) {
        if (auto object = buildSpriteObject(resources, hexgrid, op.frmPid, op.hex, op.direction)) {
            objects.push_back(std::move(object));
        }
    }
    if (objects.empty()) {
        return {}; // no resolvable art
    }

    // Union of all sprite bounds, with a small margin.
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (const auto& object : objects) {
        const sf::FloatRect b = object->getSprite().getGlobalBounds();
        minX = std::min(minX, b.position.x);
        minY = std::min(minY, b.position.y);
        maxX = std::max(maxX, b.position.x + b.size.x);
        maxY = std::max(maxY, b.position.y + b.size.y);
    }
    constexpr float pad = 4.0f;
    minX -= pad;
    minY -= pad;
    maxX += pad;
    maxY += pad;
    const auto width = static_cast<unsigned int>(std::max(1.0f, maxX - minX));
    const auto height = static_cast<unsigned int>(std::max(1.0f, maxY - minY));

    sf::RenderTexture renderTexture;
    if (!renderTexture.resize({ width, height })) {
        spdlog::warn("PatternThumbnail: failed to create {}x{} render texture", width, height);
        return {};
    }
    renderTexture.setView(sf::View(sf::FloatRect({ minX, minY },
        { static_cast<float>(width), static_cast<float>(height) })));
    renderTexture.clear(sf::Color::Transparent);
    for (const auto& object : objects) {
        renderTexture.draw(object->getSprite());
    }
    renderTexture.display();

    const QImage rendered = toQImage(renderTexture.getTexture().copyToImage());
    const QImage scaled = rendered.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QImage canvas(size, size, QImage::Format_RGBA8888);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.drawImage((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    painter.end();

    QPixmap pixmap = QPixmap::fromImage(canvas);
    if (!key.isEmpty()) {
        QPixmapCache::insert(key, pixmap);
    }
    return pixmap;
}

} // namespace geck::pattern
