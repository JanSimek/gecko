#pragma once

#include <QPixmap>
#include <QSize>
#include <string>

namespace geck {

class Frame;
class Pal;

/**
 * @brief Utility class for generating QPixmap thumbnails from FRM files
 *
 * Provides static methods to create thumbnails from:
 * - FRM file paths (loads first frame automatically)
 * - Raw Frame data with palette
 *
 * Thumbnails are scaled to fit the target size while preserving aspect ratio,
 * with a maximum upscale factor to prevent blurry images.
 */
class FrmThumbnailGenerator {
public:
    // Maximum upscale factor to prevent blurry thumbnails
    static constexpr double MAX_SCALE = 2.0;

    /**
     * @brief Create thumbnail from FRM file path
     * @param frmPath Path to FRM file (loaded via ResourceManager)
     * @param targetSize Target thumbnail size (default 64x64)
     * @return QPixmap thumbnail, or transparent pixmap on failure
     */
    static QPixmap fromFrmPath(const std::string& frmPath,
                               const QSize& targetSize = QSize(64, 64));

    /**
     * @brief Create thumbnail from raw Frame data
     * @param frame Frame to render
     * @param palette Palette for color conversion
     * @param targetSize Target thumbnail size (default 64x64)
     * @return QPixmap thumbnail, or transparent pixmap on failure
     */
    static QPixmap fromFrame(const Frame& frame,
                             const Pal* palette,
                             const QSize& targetSize = QSize(64, 64));
};

} // namespace geck
