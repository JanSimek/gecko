#include "AnimationController.h"
#include "ui/theme/ThemeManager.h"

namespace geck {

AnimationController::AnimationController(QObject* parent)
    : QObject(parent)
    , _timer(new QTimer(this)) {
    _timer->setSingleShot(false);
    _timer->setInterval(ui::constants::ANIMATION_TIMER_INTERVAL);
    connect(_timer, &QTimer::timeout, this, &AnimationController::onTick);
}

void AnimationController::loadFrames(std::vector<QPixmap>&& frames) {
    stop();
    _frameCache = std::move(frames);
    _currentFrame = 0;
    if (!_frameCache.empty()) {
        Q_EMIT frameChanged(0);
    }
}

void AnimationController::clearFrames() {
    stop();
    _frameCache.clear();
    _currentFrame = 0;
}

void AnimationController::play() {
    if (_frameCache.size() <= 1) {
        return;
    }
    if (!_isPlaying) {
        _isPlaying = true;
        _timer->start();
        Q_EMIT playStateChanged(true);
    }
}

void AnimationController::pause() {
    if (_isPlaying) {
        _isPlaying = false;
        _timer->stop();
        Q_EMIT playStateChanged(false);
    }
}

void AnimationController::stop() {
    pause();
    if (_currentFrame != 0) {
        _currentFrame = 0;
        if (!_frameCache.empty()) {
            Q_EMIT frameChanged(0);
        }
    }
}

void AnimationController::setFrame(int frame) {
    if (frame < 0 || frame >= static_cast<int>(_frameCache.size())) {
        return;
    }
    if (frame != _currentFrame) {
        _currentFrame = frame;
        Q_EMIT frameChanged(frame);
    }
}

const QPixmap& AnimationController::frame(int index) const {
    if (index < 0 || index >= static_cast<int>(_frameCache.size())) {
        // Function-local static, initialized on first use (after Qt GUI is ready)
        static const QPixmap emptyPixmap;
        return emptyPixmap;
    }
    return _frameCache[index];
}

void AnimationController::onTick() {
    if (_frameCache.size() <= 1) {
        return;
    }
    setFrame((_currentFrame + 1) % static_cast<int>(_frameCache.size()));
}

} // namespace geck
