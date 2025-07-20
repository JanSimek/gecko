#pragma once

#include <SFML/Graphics.hpp>
#include <functional>

namespace geck::ui::components {

/**
 * @brief Manages viewport operations like zoom and panning
 * 
 * This component handles all view-related operations and provides
 * smooth transitions and proper constraints.
 */
class ViewportManager {
public:
    using ViewChangedCallback = std::function<void(const sf::View&)>;
    
    ViewportManager();
    ~ViewportManager() = default;
    
    // View management
    void setView(const sf::View& view);
    const sf::View& getView() const { return _view; }
    
    // Zoom operations
    void zoom(float factor, sf::Vector2f center = sf::Vector2f(0, 0));
    void setZoom(float level);
    float getZoomLevel() const { return _zoomLevel; }
    
    // Pan operations
    void pan(sf::Vector2f offset);
    void panTo(sf::Vector2f position);
    void centerOn(sf::Vector2f position);
    
    // Constraints
    void setZoomLimits(float minZoom, float maxZoom);
    void setPanLimits(sf::FloatRect bounds);
    void setConstraintsEnabled(bool enabled) { _constraintsEnabled = enabled; }
    
    // Smooth movement
    void setSmoothTransitions(bool enabled) { _smoothTransitions = enabled; }
    void update(float deltaTime);
    
    // Utility methods
    void fitToRect(sf::FloatRect rect, sf::Vector2u viewportSize);
    void resetView();
    
    // Callbacks
    void setViewChangedCallback(ViewChangedCallback callback) { _viewChangedCallback = callback; }
    
    // Constants
    static constexpr float DEFAULT_ZOOM_STEP = 0.1f;
    static constexpr float DEFAULT_MIN_ZOOM = 0.1f;
    static constexpr float DEFAULT_MAX_ZOOM = 5.0f;
    
private:
    sf::View _view;
    sf::View _targetView; // For smooth transitions
    
    float _zoomLevel = 1.0f;
    float _targetZoomLevel = 1.0f;
    float _minZoom = DEFAULT_MIN_ZOOM;
    float _maxZoom = DEFAULT_MAX_ZOOM;
    
    sf::FloatRect _panBounds;
    bool _constraintsEnabled = true;
    bool _smoothTransitions = false;
    
    ViewChangedCallback _viewChangedCallback;
    
    // Smooth transition parameters
    static constexpr float TRANSITION_SPEED = 3.0f; // How fast transitions happen
    static constexpr float TRANSITION_THRESHOLD = 0.01f; // When to consider transition complete
    
    // Helper methods
    void applyConstraints();
    void notifyViewChanged();
    bool isTransitioning() const;
    void updateSmoothTransition(float deltaTime);
};

} // namespace geck::ui::components