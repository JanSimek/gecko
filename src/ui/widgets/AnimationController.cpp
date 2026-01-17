#include "AnimationController.h"
#include "../UIConstants.h"

namespace geck {

AnimationController::AnimationController(QObject* parent)
    : QObject(parent)
    , _timer(new QTimer(this)) {
    _timer->setSingleShot(false);
    _timer->setInterval(ui::constants::ANIMATION_TIMER_INTERVAL);
    connect(_timer, &QTimer::timeout, this, &AnimationController::onTick);
}

void AnimationController::loadFrames(const std::vector<QPixmap>& frames) {
    stop();
    _frameCache = frames;
    _currentFrame = 0;
    emit framesLoaded(static_cast<int>(_frameCache.size()));
    if (!_frameCache.empty()) {
        emit frameChanged(0);
    }
}

void AnimationController::loadFrames(std::vector<QPixmap>&& frames) {
    stop();
    _frameCache = std::move(frames);
    _currentFrame = 0;
    emit framesLoaded(static_cast<int>(_frameCache.size()));
    if (!_frameCache.empty()) {
        emit frameChanged(0);
    }
}

void AnimationController::clearFrames() {
    stop();
    _frameCache.clear();
    _currentFrame = 0;
    emit framesLoaded(0);
}

void AnimationController::play() {
    if (_frameCache.size() <= 1) {
        return;
    }
    if (!_isPlaying) {
        _isPlaying = true;
        _timer->start();
        emit playStateChanged(true);
    }
}

void AnimationController::pause() {
    if (_isPlaying) {
        _isPlaying = false;
        _timer->stop();
        emit playStateChanged(false);
    }
}

void AnimationController::stop() {
    pause();
    if (_currentFrame != 0) {
        _currentFrame = 0;
        if (!_frameCache.empty()) {
            emit frameChanged(0);
        }
    }
}

void AnimationController::toggle() {
    if (_isPlaying) {
        pause();
    } else {
        play();
    }
}

void AnimationController::setFrame(int frame) {
    if (frame < 0 || frame >= static_cast<int>(_frameCache.size())) {
        return;
    }
    if (frame != _currentFrame) {
        _currentFrame = frame;
        emit frameChanged(frame);
    }
}

void AnimationController::nextFrame() {
    if (_frameCache.empty()) {
        return;
    }
    int next = (_currentFrame + 1) % static_cast<int>(_frameCache.size());
    setFrame(next);
}

void AnimationController::previousFrame() {
    if (_frameCache.empty()) {
        return;
    }
    int prev = (_currentFrame - 1 + static_cast<int>(_frameCache.size())) % static_cast<int>(_frameCache.size());
    setFrame(prev);
}

void AnimationController::setDirection(int direction) {
    if (direction < 0 || direction >= _totalDirections) {
        return;
    }
    if (direction != _currentDirection) {
        _currentDirection = direction;
        emit directionChanged(direction);
    }
}

void AnimationController::setTotalDirections(int count) {
    _totalDirections = qMax(1, count);
    if (_currentDirection >= _totalDirections) {
        setDirection(_totalDirections - 1);
    }
}

void AnimationController::nextDirection() {
    int next = (_currentDirection + 1) % _totalDirections;
    setDirection(next);
}

void AnimationController::setInterval(int ms) {
    _timer->setInterval(ms);
}

int AnimationController::interval() const {
    return _timer->interval();
}

const QPixmap& AnimationController::frame(int index) const {
    if (index < 0 || index >= static_cast<int>(_frameCache.size())) {
        // Function-local static, initialized on first use (after Qt GUI is ready)
        static const QPixmap emptyPixmap;
        return emptyPixmap;
    }
    return _frameCache[index];
}

const QPixmap& AnimationController::currentPixmap() const {
    return frame(_currentFrame);
}

void AnimationController::onTick() {
    if (_frameCache.size() <= 1) {
        return;
    }
    nextFrame();
}

} // namespace geck
