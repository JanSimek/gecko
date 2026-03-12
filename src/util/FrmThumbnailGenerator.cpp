#include "FrmThumbnailGenerator.h"
#include "../resource/GameResources.h"
#include "../format/frm/Frm.h"
#include "../format/frm/Frame.h"
#include "../format/pal/Pal.h"

#include <QImage>
#include <QPainter>
#include <spdlog/spdlog.h>

namespace geck {

QPixmap FrmThumbnailGenerator::fromFrmPath(resource::GameResources& resources, const std::string& frmPath, const QSize& targetSize) {
    QPixmap thumbnail(targetSize);
    thumbnail.fill(Qt::transparent);

    try {
        const auto* frm = resources.repository().load<Frm>(frmPath);
        if (!frm) {
            return thumbnail;
        }

        // Get the first direction and first frame for preview
        const auto& directions = frm->directions();
        if (directions.empty()) {
            return thumbnail;
        }

        const auto& firstDirection = directions[0];
        const auto& frames = firstDirection.frames();
        if (frames.empty()) {
            return thumbnail;
        }

        // Load default palette for color conversion
        const Pal* palette = resources.repository().load<Pal>("color.pal");
        if (!palette) {
            spdlog::warn("FrmThumbnailGenerator: Could not load color.pal for {}", frmPath);
            return thumbnail;
        }

        // Convert first frame to thumbnail
        return fromFrame(frames[0], palette, targetSize);

    } catch (const std::exception& e) {
        spdlog::warn("FrmThumbnailGenerator: Exception creating thumbnail for {}: {}", frmPath, e.what());
    }

    return thumbnail;
}

QPixmap FrmThumbnailGenerator::fromFrame(const Frame& frame, const Pal* palette, const QSize& targetSize) {
    QPixmap thumbnail(targetSize);
    thumbnail.fill(Qt::transparent);

    // Get frame dimensions
    uint16_t frameWidth = frame.width();
    uint16_t frameHeight = frame.height();

    if (frameWidth == 0 || frameHeight == 0) {
        return thumbnail;
    }

    // Get RGBA data with palette
    uint8_t* rgbaData = const_cast<Frame&>(frame).rgba(const_cast<Pal*>(palette));
    if (!rgbaData) {
        return thumbnail;
    }

    // Create QImage from RGBA data (must copy since rgbaData may be temporary)
    QImage frameImage(rgbaData, frameWidth, frameHeight, QImage::Format_RGBA8888);
    frameImage = frameImage.copy();

    // Calculate optimal scaling with maximum upscale limit
    double scaleX = static_cast<double>(targetSize.width()) / frameWidth;
    double scaleY = static_cast<double>(targetSize.height()) / frameHeight;
    double scale = qMin(scaleX, scaleY);

    // Limit upscaling to prevent blurry thumbnails
    scale = qMin(scale, MAX_SCALE);

    // Calculate final size
    int scaledWidth = static_cast<int>(frameWidth * scale);
    int scaledHeight = static_cast<int>(frameHeight * scale);

    // Scale with high quality
    QImage scaledImage = frameImage.scaled(scaledWidth, scaledHeight,
                                           Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
    QPixmap scaledFrame = QPixmap::fromImage(scaledImage);

    // Center the scaled frame in the thumbnail
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    int x = (targetSize.width() - scaledFrame.width()) / 2;
    int y = (targetSize.height() - scaledFrame.height()) / 2;

    painter.drawPixmap(x, y, scaledFrame);

    return thumbnail;
}

} // namespace geck
