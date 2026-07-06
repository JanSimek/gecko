#include "ui/rendering/ThumbnailRenderer.h"

#include <algorithm>
#include <limits>

#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

#include <QImage>
#include <QPainter>
#include <QPixmapCache>

namespace geck {

namespace {

    // Copy an SFML RGBA image into an owned QImage.
    QImage toQImage(const sf::Image& image) {
        const sf::Vector2u sz = image.getSize();
        return QImage(image.getPixelsPtr(), static_cast<int>(sz.x), static_cast<int>(sz.y),
            QImage::Format_RGBA8888)
            .copy();
    }

} // namespace

std::optional<QPixmap> ThumbnailRenderer::cached(const QString& key) {
    if (key.isEmpty()) {
        return std::nullopt;
    }
    QPixmap pixmap;
    if (QPixmapCache::find(key, &pixmap)) {
        return pixmap;
    }
    return std::nullopt;
}

QImage ThumbnailRenderer::renderImage(const std::vector<const sf::Sprite*>& sprites, int size) {
    // Union of all sprite bounds, with a small margin.
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (const sf::Sprite* sprite : sprites) {
        if (sprite == nullptr) {
            continue;
        }
        const sf::FloatRect b = sprite->getGlobalBounds();
        minX = std::min(minX, b.position.x);
        minY = std::min(minY, b.position.y);
        maxX = std::max(maxX, b.position.x + b.size.x);
        maxY = std::max(maxY, b.position.y + b.size.y);
    }
    if (minX > maxX || minY > maxY) {
        return {}; // nothing to draw
    }

    constexpr float pad = 4.0f;
    minX -= pad;
    minY -= pad;
    maxX += pad;
    maxY += pad;
    const float bboxWidth = std::max(1.0f, maxX - minX);
    const float bboxHeight = std::max(1.0f, maxY - minY);

    // Cap the render texture so huge content (whole maps) stays bounded; the world-space
    // view still covers the full bounds, so the content is supersampled down into it.
    constexpr float maxRenderDim = 512.0f;
    const float scale = std::min(1.0f, maxRenderDim / std::max(bboxWidth, bboxHeight));
    const auto width = static_cast<unsigned int>(std::max(1.0f, bboxWidth * scale));
    const auto height = static_cast<unsigned int>(std::max(1.0f, bboxHeight * scale));

    sf::RenderTexture renderTexture;
    if (!renderTexture.resize({ width, height })) {
        spdlog::warn("ThumbnailRenderer: failed to create {}x{} render texture", width, height);
        return {};
    }
    renderTexture.setView(sf::View(sf::FloatRect({ minX, minY }, { bboxWidth, bboxHeight })));
    renderTexture.clear(sf::Color::Transparent);
    for (const sf::Sprite* sprite : sprites) {
        if (sprite != nullptr) {
            renderTexture.draw(*sprite);
        }
    }
    renderTexture.display();

    const QImage rendered = toQImage(renderTexture.getTexture().copyToImage());
    const QImage scaled = rendered.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QImage canvas(size, size, QImage::Format_RGBA8888);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.drawImage((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    painter.end();

    return canvas;
}

QPixmap ThumbnailRenderer::render(const std::vector<const sf::Sprite*>& sprites, int size,
    const QString& cacheKey) {
    const QImage canvas = renderImage(sprites, size);
    if (canvas.isNull()) {
        return {};
    }
    QPixmap pixmap = QPixmap::fromImage(canvas);
    if (!cacheKey.isEmpty()) {
        QPixmapCache::insert(cacheKey, pixmap);
    }
    return pixmap;
}

} // namespace geck
