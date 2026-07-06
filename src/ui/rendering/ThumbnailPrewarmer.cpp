#include "ui/rendering/ThumbnailPrewarmer.h"

#include <chrono>

#include <QFileInfo>
#include <QImage>
#include <QString>
#include <spdlog/spdlog.h>

#include "editor/HexagonGrid.h"
#include "resource/DataFileSystem.h"
#include "resource/GameResources.h"
#include "ui/rendering/MapThumbnail.h"

namespace geck {

ThumbnailPrewarmer::ThumbnailPrewarmer(std::vector<std::filesystem::path> dataPaths,
    int thumbnailSize, QObject* parent)
    : QThread(parent)
    , _dataPaths(std::move(dataPaths))
    , _thumbnailSize(thumbnailSize) {
}

ThumbnailPrewarmer::~ThumbnailPrewarmer() {
    requestStop();
    wait();
}

void ThumbnailPrewarmer::run() {
    const auto start = std::chrono::steady_clock::now();

    // Private, thread-local resource stack — see the class comment. Constructed (and
    // destructed) here so every GL object lives and dies with this thread's context.
    resource::GameResources resources;
    for (const auto& path : _dataPaths) {
        if (_stop.load()) {
            return;
        }
        resources.files().addDataPath(path);
    }

    const HexagonGrid hexgrid;

    int rendered = 0;
    int skipped = 0;
    // "*.map" (not "maps/*.map"): listed VFS paths carry a leading slash and the glob is
    // anchored, so the same pattern the map browser itself uses is the one that matches.
    for (const auto& entry : resources.files().list("*.map")) {
        if (_stop.load()) {
            break;
        }

        const QString vfsPath = QString::fromStdString(entry.generic_string());
        const QString id = MapThumbnail::identity(vfsPath, _thumbnailSize, resources);
        if (id.isEmpty()) {
            continue;
        }
        const QString cacheFile = MapThumbnail::diskCachePath(id);
        if (QFileInfo::exists(cacheFile)) {
            ++skipped;
            continue;
        }

        const QImage image = MapThumbnail::renderImage(vfsPath, resources, hexgrid, _thumbnailSize);
        if (image.isNull() || !image.save(cacheFile, "PNG")) {
            continue;
        }
        ++rendered;
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start)
                        .count();
    spdlog::info("ThumbnailPrewarmer: {} thumbnail(s) rendered, {} already cached, in {}ms{}",
        rendered, skipped, ms, _stop.load() ? " (stopped early)" : "");
}

} // namespace geck
