#pragma once

#include <SFML/Graphics.hpp>
#include <memory>

#include "TilePlacementContext.h"

namespace geck {

// Forward declarations
class Map;
namespace selection {
    class SelectionManager;
}

/**
 * @brief Manages tile placement operations for the map editor
 *
 * This class encapsulates all tile placement functionality including:
 * - Single tile placement at cursor position
 * - Area fill operations
 * - Selected tile replacement
 * - Tile placement mode state management
 */
class TilePlacementManager {
public:
    explicit TilePlacementManager(TilePlacementContext& context);
    ~TilePlacementManager() = default;

    // Tile placement operations
    void placeTileAtPosition(int tileIndex, sf::Vector2f worldPos, bool isRoof);
    void fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof);
    void replaceSelectedTiles(int newTileIndex);

    // Tile placement mode control
    void setTilePlacementMode(bool enabled, int tileIndex = -1, bool isRoof = false);

    // State queries
    bool isTilePlacementMode() const { return _tilePlacementMode; }

    // Callback interface for triggering tile placement from input events
    void handleTilePlacement(sf::Vector2f worldPos, bool isRoof);
    void handleTileAreaFill(sf::Vector2f startPos, sf::Vector2f endPos, bool isRoof);

    // Reset all tile placement state
    void resetState();

private:
    TilePlacementContext& _context;

    // Tile placement state
    bool _tilePlacementMode = false;
    int _tilePlacementIndex = -1;
    bool _tilePlacementIsRoof = false;
};

} // namespace geck