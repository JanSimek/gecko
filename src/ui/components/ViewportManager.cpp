#include "ViewportManager.h"
#include <algorithm>
#include <cmath>

namespace geck::ui::components {

ViewportManager::ViewportManager() {
    // Initialize with default view
    _view = sf::View(sf::Vector2f(0, 0), sf::Vector2f(1024, 768));
    _targetView = _view;
    
    // Set default pan bounds (large area)
    _panBounds = sf::FloatRect(-10000, -10000, 20000, 20000);
}

void ViewportManager::setView(const sf::View& view) {
    _view = view;
    _targetView = view;
    
    // Update zoom level based on view size
    sf::Vector2f size = view.getSize();
    _zoomLevel = 1.0f; // This would need proper calculation based on reference size
    _targetZoomLevel = _zoomLevel;
    
    notifyViewChanged();
}

void ViewportManager::zoom(float factor, sf::Vector2f center) {
    if (factor == 0) return;
    
    float newZoom = _zoomLevel * factor;
    
    // Apply zoom constraints
    if (_constraintsEnabled) {
        newZoom = std::clamp(newZoom, _minZoom, _maxZoom);
    }
    
    // If zoom didn't change, return early
    if (std::abs(newZoom - _zoomLevel) < 0.001f) {
        return;
    }
    
    // Calculate new view size
    sf::Vector2f currentSize = _view.getSize();
    sf::Vector2f newSize = currentSize * (_zoomLevel / newZoom);
    
    if (_smoothTransitions) {
        _targetView.setSize(newSize);
        _targetZoomLevel = newZoom;
    } else {
        _view.setSize(newSize);
        _zoomLevel = newZoom;
        applyConstraints();
        notifyViewChanged();
    }
}

void ViewportManager::setZoom(float level) {
    if (_constraintsEnabled) {
        level = std::clamp(level, _minZoom, _maxZoom);
    }
    
    if (std::abs(level - _zoomLevel) < 0.001f) {
        return;
    }
    
    // Calculate new view size based on zoom level
    sf::Vector2f referenceSize(1024, 768); // Default reference size
    sf::Vector2f newSize = referenceSize / level;
    
    if (_smoothTransitions) {
        _targetView.setSize(newSize);
        _targetZoomLevel = level;
    } else {
        _view.setSize(newSize);
        _zoomLevel = level;
        applyConstraints();
        notifyViewChanged();
    }
}

void ViewportManager::pan(sf::Vector2f offset) {
    sf::Vector2f newCenter = _view.getCenter() + offset;
    
    if (_smoothTransitions) {
        _targetView.setCenter(newCenter);
    } else {
        _view.setCenter(newCenter);
        applyConstraints();
        notifyViewChanged();
    }
}

void ViewportManager::panTo(sf::Vector2f position) {
    if (_smoothTransitions) {
        _targetView.setCenter(position);
    } else {
        _view.setCenter(position);
        applyConstraints();
        notifyViewChanged();
    }
}

void ViewportManager::centerOn(sf::Vector2f position) {
    panTo(position);
}

void ViewportManager::setZoomLimits(float minZoom, float maxZoom) {
    _minZoom = std::max(0.01f, minZoom);
    _maxZoom = std::max(_minZoom, maxZoom);
    
    // Apply constraints to current zoom if necessary
    if (_constraintsEnabled) {
        if (_zoomLevel < _minZoom) {
            setZoom(_minZoom);
        } else if (_zoomLevel > _maxZoom) {
            setZoom(_maxZoom);
        }
    }
}

void ViewportManager::setPanLimits(sf::FloatRect bounds) {
    _panBounds = bounds;
    
    if (_constraintsEnabled) {
        applyConstraints();
    }
}

void ViewportManager::update(float deltaTime) {
    if (_smoothTransitions && isTransitioning()) {
        updateSmoothTransition(deltaTime);
    }
}

void ViewportManager::fitToRect(sf::FloatRect rect, sf::Vector2u viewportSize) {
    // Calculate the zoom level needed to fit the rectangle
    float scaleX = static_cast<float>(viewportSize.x) / rect.width;
    float scaleY = static_cast<float>(viewportSize.y) / rect.height;
    float scale = std::min(scaleX, scaleY);
    
    // Set zoom and center
    setZoom(scale);
    centerOn(sf::Vector2f(rect.left + rect.width / 2, rect.top + rect.height / 2));
}

void ViewportManager::resetView() {
    _view = sf::View(sf::Vector2f(0, 0), sf::Vector2f(1024, 768));
    _targetView = _view;
    _zoomLevel = 1.0f;
    _targetZoomLevel = 1.0f;
    
    notifyViewChanged();
}

void ViewportManager::applyConstraints() {
    if (!_constraintsEnabled) return;
    
    // Apply zoom constraints
    _zoomLevel = std::clamp(_zoomLevel, _minZoom, _maxZoom);
    
    // Apply pan constraints
    sf::Vector2f center = _view.getCenter();
    sf::Vector2f size = _view.getSize();
    sf::Vector2f halfSize = size / 2.0f;
    
    // Calculate constrained center
    sf::Vector2f constrainedCenter = center;
    
    // Check bounds
    if (center.x - halfSize.x < _panBounds.left) {
        constrainedCenter.x = _panBounds.left + halfSize.x;
    } else if (center.x + halfSize.x > _panBounds.left + _panBounds.width) {
        constrainedCenter.x = _panBounds.left + _panBounds.width - halfSize.x;
    }
    
    if (center.y - halfSize.y < _panBounds.top) {
        constrainedCenter.y = _panBounds.top + halfSize.y;
    } else if (center.y + halfSize.y > _panBounds.top + _panBounds.height) {
        constrainedCenter.y = _panBounds.top + _panBounds.height - halfSize.y;
    }
    
    _view.setCenter(constrainedCenter);
}

void ViewportManager::notifyViewChanged() {
    if (_viewChangedCallback) {
        _viewChangedCallback(_view);
    }
}

bool ViewportManager::isTransitioning() const {
    sf::Vector2f currentCenter = _view.getCenter();
    sf::Vector2f targetCenter = _targetView.getCenter();
    sf::Vector2f currentSize = _view.getSize();
    sf::Vector2f targetSize = _targetView.getSize();
    
    float centerDist = std::sqrt(std::pow(currentCenter.x - targetCenter.x, 2) + std::pow(currentCenter.y - targetCenter.y, 2));
    float sizeDist = std::sqrt(std::pow(currentSize.x - targetSize.x, 2) + std::pow(currentSize.y - targetSize.y, 2));
    
    return centerDist > TRANSITION_THRESHOLD || sizeDist > TRANSITION_THRESHOLD;
}

void ViewportManager::updateSmoothTransition(float deltaTime) {
    float lerpFactor = std::min(1.0f, TRANSITION_SPEED * deltaTime);
    
    // Interpolate center
    sf::Vector2f currentCenter = _view.getCenter();
    sf::Vector2f targetCenter = _targetView.getCenter();
    sf::Vector2f newCenter = currentCenter + (targetCenter - currentCenter) * lerpFactor;
    
    // Interpolate size
    sf::Vector2f currentSize = _view.getSize();
    sf::Vector2f targetSize = _targetView.getSize();
    sf::Vector2f newSize = currentSize + (targetSize - currentSize) * lerpFactor;
    
    // Update view
    _view.setCenter(newCenter);
    _view.setSize(newSize);
    
    // Update zoom level
    _zoomLevel = _zoomLevel + (_targetZoomLevel - _zoomLevel) * lerpFactor;
    
    // Apply constraints and notify
    applyConstraints();
    notifyViewChanged();
}

} // namespace geck::ui::components