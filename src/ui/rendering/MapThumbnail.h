#pragma once

#include <QPixmap>
#include <QString>

namespace geck {

class HexagonGrid;
namespace resource {
    class GameResources;
}

/// Renders a map file to a thumbnail for the map-loader grid. Mirrors PatternThumbnail:
/// loads the map, builds its ground-elevation floor/object/roof sprites, and hands them to
/// the shared ThumbnailRenderer.
class MapThumbnail {
public:
    /// Render elevation 0 of the map at `vfsPath` (a path into the mounted VFS, e.g.
    /// "maps/sfshutl2.map") to a `size`x`size` thumbnail. Loads + parses the map on a
    /// cache miss; returns a null QPixmap if it can't be read/parsed. Cached in memory.
    static QPixmap forMap(const QString& vfsPath,
        resource::GameResources& resources,
        const HexagonGrid& hexgrid,
        int size = 128);
};

} // namespace geck
