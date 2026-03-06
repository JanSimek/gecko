#pragma once

#include <QObject>
#include <QTimer>
#include <QPixmap>
#include <vector>

namespace geck {

/**
 * @brief Reusable animation controller for frame-based sprite animations
 *
 * Manages animation state (current frame, direction, play/pause) and timing.
 * Emits signals when frame or direction changes, allowing parent widgets
 * to update their display accordingly.
 *
 * Usage:
 *   AnimationController controller(this);
 *   connect(&controller, &AnimationController::frameChanged, this, &MyWidget::onFrameChanged);
 *   controller.loadFrames(frameCache);
 *   controller.play();
 */
class AnimationController : public QObject {
    Q_OBJECT

public:
    explicit AnimationController(QObject* parent = nullptr);
    ~AnimationController() override = default;

    // Frame management
    void loadFrames(const std::vector<QPixmap>& frames);
    void loadFrames(std::vector<QPixmap>&& frames);
    void clearFrames();

    // Playback control
    void play();
    void pause();
    void stop();
    void toggle();

    // Frame navigation
    void setFrame(int frame);
    void nextFrame();
    void previousFrame();

    // Direction support (for multi-direction sprites)
    void setDirection(int direction);
    void setTotalDirections(int count);
    void nextDirection();

    // Configuration
    void setInterval(int ms);
    int interval() const;

    // State queries
    bool isPlaying() const { return _isPlaying; }
    int currentFrame() const { return _currentFrame; }
    int totalFrames() const { return static_cast<int>(_frameCache.size()); }
    int currentDirection() const { return _currentDirection; }
    int totalDirections() const { return _totalDirections; }
    bool hasFrames() const { return !_frameCache.empty(); }
    bool hasMultipleFrames() const { return _frameCache.size() > 1; }

    // Frame access
    const QPixmap& frame(int index) const;
    const QPixmap& currentPixmap() const;

signals:
    void frameChanged(int frame);
    void directionChanged(int direction);
    void playStateChanged(bool playing);
    void framesLoaded(int count);

private slots:
    void onTick();

private:
    QTimer* _timer;
    std::vector<QPixmap> _frameCache;

    int _currentFrame = 0;
    int _currentDirection = 0;
    int _totalDirections = 1;
    bool _isPlaying = false;
};

} // namespace geck
