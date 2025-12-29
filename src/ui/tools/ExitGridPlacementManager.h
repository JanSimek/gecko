#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include "../dialogs/ExitGridPropertiesDialog.h"

namespace geck {

// Forward declarations
class EditorWidget;
class Map;
struct MapObject;

/**
 * @brief Manages exit grid placement operations for the map editor
 *
 * This class handles:
 * - Exit grid placement at cursor position
 * - Properties dialog management
 * - Exit grid placement mode state
 * - MISC object creation for exit grids
 */
class ExitGridPlacementManager {
public:
    explicit ExitGridPlacementManager(EditorWidget* editor);
    ~ExitGridPlacementManager() = default;

    // Exit grid placement operations
    void placeExitGridAtPosition(sf::Vector2f worldPos);
    void editExitGridProperties(std::shared_ptr<MapObject> exitGrid);

    // Exit grid placement mode control
    void setExitGridPlacementMode(bool enabled);
    void setMarkExitsMode(bool enabled);

    // State queries
    bool isExitGridPlacementMode() const { return _exitGridPlacementMode; }
    bool isMarkExitsMode() const { return _markExitsMode; }

    // Callback interface for triggering exit grid placement from input events
    void handleExitGridPlacement(sf::Vector2f worldPos);

    // Reset all exit grid placement state
    void resetState();

    // Check if selected objects contain exit grids and edit them
    bool editSelectedExitGrids();

    // Mark exits mode - select and edit exit grids
    void handleMarkExitsSelection(sf::Vector2f worldPos);
    void selectExitGridsInArea(sf::Vector2f startPos, sf::Vector2f endPos);

private:
    // Create exit grid MISC object with properties
    std::shared_ptr<MapObject> createExitGridObject(int hexPosition, const ExitGridPropertiesDialog::ExitGridProperties& properties);

    // Show properties dialog and handle result
    bool showPropertiesDialog(ExitGridPropertiesDialog::ExitGridProperties& properties, const ExitGridPropertiesDialog::ExitGridProperties* existing = nullptr);

    EditorWidget* _editor;

    // State
    bool _exitGridPlacementMode = false;
    bool _markExitsMode = false;
};

} // namespace geck