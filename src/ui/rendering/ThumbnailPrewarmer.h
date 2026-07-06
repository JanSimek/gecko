#pragma once

#include <atomic>
#include <filesystem>
#include <vector>

#include <QThread>

namespace geck {

/// Fills the persisted map-thumbnail cache in the background so the map browser's first
/// open finds its previews already rendered. Runs at the lowest thread priority with a
/// PRIVATE resource stack: its own DataFileSystem mounts, its own TextureManager (GPU
/// textures live in this thread's own GL context), its own repository — nothing is shared
/// with the UI thread, which is what makes rendering off it safe. The worker only writes
/// PNG files; the browser picks them up through MapThumbnail's disk-cache lookup.
///
/// The pass is transient: resources are constructed inside run() and torn down there too
/// (GL cleanup must happen on the context's own thread), so the doubled texture memory
/// lasts only as long as the warm-up.
class ThumbnailPrewarmer : public QThread {
    Q_OBJECT

public:
    ThumbnailPrewarmer(std::vector<std::filesystem::path> dataPaths, int thumbnailSize,
        QObject* parent = nullptr);
    ~ThumbnailPrewarmer() override;

    /// Ask the pass to stop after the map it is currently rendering.
    void requestStop() { _stop.store(true); }

protected:
    void run() override;

private:
    std::vector<std::filesystem::path> _dataPaths;
    int _thumbnailSize;
    std::atomic<bool> _stop{ false };
};

} // namespace geck
