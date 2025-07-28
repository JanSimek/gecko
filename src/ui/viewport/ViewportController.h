#pragma once

#include <SFML/Graphics.hpp>
#include "../../editor/HexagonGrid.h"

namespace geck {

/**
 * @brief Manages viewport operations like zooming, panning, and coordinate conversions
 * 
 * This class handles all viewport-related functionality that was previously
 * embedded in EditorWidget, providing a clean separation of concerns for
 * camera/view management operations.
 */
class ViewportController {
public:
    explicit ViewportController(const HexagonGrid* hexGrid);
    ~ViewportController() = default;

    /**
     * @brief Initialize the viewport with default settings
     * @param windowSize The size of the SFML window
     */
    void initialize(sf::Vector2u windowSize);
    
    /**
     * @brief Update viewport for window resize with aspect ratio preservation
     * @param windowSize New window size
     */
    void updateViewForWindowSize(sf::Vector2u windowSize);

    /**
     * @brief Get the current view
     */
    const sf::View& getView() const { return _view; }
    sf::View& getView() { return _view; }

    /**
     * @brief Center the view on the map
     */
    void centerViewOnMap();

    /**
     * @brief Zoom the view in or out
     * @param direction Positive for zoom in, negative for zoom out
     */
    void zoomView(float direction);

    /**
     * @brief Update hover hex based on world position
     * @param worldPos World position to check
     * @return The hex index under the position, -1 if none
     */
    int updateHoverHex(sf::Vector2f worldPos);

    /**
     * @brief Convert world position to hex grid index
     * @param worldPos World position to convert
     * @return Hex index, -1 if invalid
     */
    int worldPosToHexIndex(sf::Vector2f worldPos) const;


    /**
     * @brief Snap world position to nearest hex grid center
     * @param worldPos World position to snap
     * @return Snapped position
     */
    sf::Vector2f snapToHexGrid(sf::Vector2f worldPos) const;

    /**
     * @brief Get current zoom level
     */
    float getZoomLevel() const { return _zoomLevel; }

    /**
     * @brief Set zoom level directly
     * @param zoom New zoom level (clamped to valid range)
     */
    void setZoomLevel(float zoom);

private:
    const HexagonGrid* _hexGrid;
    sf::View _view;
    
    // Zoom level tracking and limits
    float _zoomLevel = 1.0f;
    static constexpr float MIN_ZOOM = 0.1f;   // Can zoom out to 10% of original size
    static constexpr float MAX_ZOOM = 5.0f;   // Can zoom in to 500% of original size
    static constexpr float ZOOM_STEP = 0.05f; // 5% zoom steps for smooth zooming
};

} // namespace geck