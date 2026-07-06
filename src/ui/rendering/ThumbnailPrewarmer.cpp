#include "ui/rendering/ThumbnailPrewarmer.h"

#include <chrono>

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QImage>
#include <QString>
#include <spdlog/spdlog.h>

#include "editor/HexagonGrid.h"
#include "resource/DataFileSystem.h"
#include "resource/GameResources.h"
#include "resource/ResourceInitializer.h"
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
    // Grace period: startup work (an auto-loaded map, the file browser's indexing) should win
    // the disk and the logging mutex first; the cache can wait a few seconds.
    for (int i = 0; i < 50 && !_stop.load(); ++i) {
        msleep(100);
    }
    if (_stop.load()) {
        return;
    }

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

    // The sprite builders read tiles.lst (and friends) with repository().find() — present
    // only after the initializer has loaded them, which the app does in DataPathLoader.
    // Without this every floor tile is silently skipped and thumbnails come out as white,
    // object-only frames.
    try {
        ResourceInitializer::loadEssentialLstFiles(resources);
    } catch (const std::exception& e) {
        spdlog::info("ThumbnailPrewarmer: essential lists unavailable, skipping warm-up: {}", e.what());
        return;
    }

    const HexagonGrid hexgrid;

    int rendered = 0;
    int skipped = 0;
    QSet<QString> expected;
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
        expected.insert(QFileInfo(cacheFile).fileName());
        if (QFileInfo::exists(cacheFile)) {
            ++skipped;
            continue;
        }

        const QImage image = MapThumbnail::renderImage(vfsPath, resources, hexgrid, _thumbnailSize);
        if (image.isNull() || !image.save(cacheFile, "PNG")) {
            continue;
        }
        ++rendered;

        msleep(25); // stay polite: this pass shares the disk and the log mutex with the app
    }

    // A completed pass knows every identity the current mounts can produce, so anything else
    // in the cache directory is an orphan — an old renderer version, a re-saved map's previous
    // identity, or an unmounted data set — and gets removed. Only a full pass may prune.
    if (!_stop.load() && !expected.isEmpty()) {
        const QFileInfo probe(MapThumbnail::diskCachePath(QStringLiteral("probe")));
        QDir cacheDir(probe.absolutePath());
        int pruned = 0;
        for (const QString& file : cacheDir.entryList({ QStringLiteral("*.png") }, QDir::Files)) {
            if (!expected.contains(file) && cacheDir.remove(file)) {
                ++pruned;
            }
        }
        if (pruned > 0) {
            spdlog::info("ThumbnailPrewarmer: pruned {} stale thumbnail(s)", pruned);
        }
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start)
                        .count();
    spdlog::info("ThumbnailPrewarmer: {} thumbnail(s) rendered, {} already cached, in {}ms{}",
        rendered, skipped, ms, _stop.load() ? " (stopped early)" : "");
}

} // namespace geck
