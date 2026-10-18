#pragma once

#include <QObject>
#include <QTimer>
#include <QPixmap>
#include <vector>

namespace geck {

/**
 * @brief Reusable animation controller for frame-based sprite animations
 *
 * Manages animation state (current frame, play/pause) and timing.
 * Emits signals when the frame or play state changes, allowing parent widgets
 * to update their display accordingly.
 *
 * Usage:
 *   AnimationController controller(this);
 *   connect(&controller, &AnimationController::frameChanged, this, &MyWidget::onFrameChanged);
 *   controller.loadFrames(std::move(frameCache));
 *   controller.play();
 */
class AnimationController : public QObject {
    Q_OBJECT

public:
    explicit AnimationController(QObject* parent = nullptr);
    ~AnimationController() override = default;

    // Frame management
    void loadFrames(std::vector<QPixmap>&& frames);
    void clearFrames();

    // Playback control
    void play();
    void stop();

    // State queries
    bool isPlaying() const { return _isPlaying; }
    bool hasFrames() const { return !_frameCache.empty(); }
    bool hasMultipleFrames() const { return _frameCache.size() > 1; }

    // Frame access
    const QPixmap& frame(int index) const;

signals:
    void frameChanged(int frame);
    void playStateChanged(bool playing);

private slots:
    void onTick();

private:
    void pause();
    void setFrame(int frame);

    QTimer* _timer;
    std::vector<QPixmap> _frameCache;

    int _currentFrame = 0;
    bool _isPlaying = false;
};

} // namespace geck
