#pragma once

#include <SFML/Graphics.hpp>
#include <memory>

namespace geck {

// Forward declarations
class EditorWidget;
class Map;
namespace selection { class SelectionManager; }

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
    explicit TilePlacementManager(EditorWidget* editor);
    ~TilePlacementManager() = default;

    // Tile placement operations
    void placeTileAtPosition(int tileIndex, sf::Vector2f worldPos, bool isRoof);
    void fillAreaWithTile(int tileIndex, const sf::FloatRect& area, bool isRoof);
    void replaceSelectedTiles(int newTileIndex);

    // Tile placement mode control
    void setTilePlacementMode(bool enabled, int tileIndex = -1, bool isRoof = false);
    void setTilePlacementAreaFill(bool enabled);
    void setTilePlacementReplaceMode(bool enabled);
    
    // State queries
    bool isTilePlacementMode() const { return _tilePlacementMode; }
    bool isTilePlacementAreaFill() const { return _tilePlacementAreaFill; }
    bool isTilePlacementReplaceMode() const { return _tilePlacementReplaceMode; }
    
    int getTilePlacementIndex() const { return _tilePlacementIndex; }
    bool getTilePlacementIsRoof() const { return _tilePlacementIsRoof; }

    // Callback interface for triggering tile placement from input events
    void handleTilePlacement(sf::Vector2f worldPos, bool isRoof);
    void handleTileAreaFill(sf::Vector2f startPos, sf::Vector2f endPos, bool isRoof);

    // Reset all tile placement state
    void resetState();

private:
    // Helper methods
    void updateTileSprite(int hexIndex, bool isRoof);
    int worldPosToHexPosition(sf::Vector2f worldPos) const;

private:
    EditorWidget* _editor;

    // Tile placement state
    bool _tilePlacementMode = false;
    bool _tilePlacementAreaFill = false;
    bool _tilePlacementReplaceMode = false;
    int _tilePlacementIndex = -1;
    bool _tilePlacementIsRoof = false;
};

} // namespace geck