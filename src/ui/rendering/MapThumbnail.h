#pragma once

#include <QImage>
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

    /// The uncached render: parse + sprites + offscreen GL, returning a QImage. Safe off the
    /// UI thread when `resources` is private to the calling thread (the prewarmer's setup);
    /// GPU textures land in that thread's own GL context via its own TextureManager.
    static QImage renderImage(const QString& vfsPath,
        resource::GameResources& resources,
        const HexagonGrid& hexgrid,
        int size);

    /// Cache identity: vfs path + size + providing file's byte-size and mtime. Empty when the
    /// path resolves to no mounted source.
    static QString identity(const QString& vfsPath, int size, resource::GameResources& resources);

    /// Absolute path of the persisted PNG for an identity (the directory is created on demand).
    static QString diskCachePath(const QString& identity);
};

} // namespace geck
