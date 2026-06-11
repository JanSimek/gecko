#pragma once

#include <QPixmap>
#include <QString>

namespace geck {
class HexagonGrid;
namespace resource {
    class GameResources;
}
} // namespace geck

namespace geck::pattern {

struct Pattern;

/// Renders a pattern to a thumbnail image for the browser grid.
class PatternThumbnail {
public:
    /// Render variant 0 of `pattern` to a `size`x`size` transparent thumbnail using game
    /// art. Only a few dozen thumbnails are live at a time, so they are rendered on demand
    /// and kept in Qt's in-memory pixmap cache (keyed by `sourcePath` + size/mtime) — no
    /// files are written. Returns a null QPixmap when nothing could be rendered (e.g. no
    /// art loaded or the pattern has no objects).
    static QPixmap forPattern(const Pattern& pattern,
        const QString& sourcePath,
        resource::GameResources& resources,
        const HexagonGrid& hexgrid,
        int size = 96);
};

} // namespace geck::pattern
