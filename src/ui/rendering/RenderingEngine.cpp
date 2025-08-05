#include "RenderingEngine.h"
#include "../../editor/Object.h"
#include "../../editor/Hex.h"
#include "../../format/map/Map.h"
#include "../../format/map/MapObject.h"
#include "../../util/Constants.h"
#include "../../util/ColorUtils.h"
#include "../../util/ResourceManager.h"
#include "../../util/ResourcePaths.h"
#include "../../util/Coordinates.h"
#include <spdlog/spdlog.h>

namespace geck {

RenderingEngine::RenderingEngine() = default;

void RenderingEngine::render(sf::RenderWindow* window, 
                            const sf::View& view,
                            const RenderData& renderData,
                            const VisibilitySettings& visibility) {
    if (!window) {
        return;
    }

    window->setView(view);

    // Layer 1: Floor tiles (always rendered)
    if (renderData.floorSprites) {
        renderFloorTiles(window, *renderData.floorSprites);
    }

    // Layer 2: Hex grid overlay (if enabled)
    if (visibility.showHexGrid) {
        renderHexGrid(window, view, renderData);
    }

    // Layer 3: Objects (with visibility filtering)
    if (visibility.showObjects) {
        renderObjects(window, renderData, visibility);
    }

    // Layer 4: Drag preview object
    if (renderData.isDraggingFromPalette && 
        renderData.dragPreviewObject && 
        *renderData.dragPreviewObject) {
        window->draw((*renderData.dragPreviewObject)->getSprite());
    }

    // Layer 5: Roof tiles (if enabled)
    renderRoofTiles(window, renderData, visibility.showRoof);

    // Layer 6: Selection visuals
    renderSelectionVisuals(window, renderData);

    // Layer 7: Exit grids (if enabled)
    if (visibility.showExitGrids && renderData.map) {
        renderExitGrids(window, view, renderData, renderData.map);
    }

    // Layer 8: Hex highlights and markers
    renderHexHighlights(window, renderData);
}

void RenderingEngine::renderFloorTiles(sf::RenderWindow* window, 
                                      const std::vector<sf::Sprite>& floorSprites) {
    for (const auto& floor : floorSprites) {
        window->draw(floor);
    }
}

void RenderingEngine::renderHexGrid(sf::RenderWindow* window,
                                   const sf::View& view,
                                   const RenderData& renderData) {
    if (!renderData.hexGrid || !renderData.hexSprite) {
        return;
    }

    // Get viewport bounds for culling
    sf::Vector2f viewCenter = view.getCenter();
    sf::Vector2f viewSize = view.getSize();

    // Iterate through hex grid
    for (int y = 0; y < HexagonGrid::GRID_HEIGHT; y++) {
        for (int x = 0; x < HexagonGrid::GRID_WIDTH; x++) {
            // Convert hex coordinates to world coordinates
            int hexIndex = y * HexagonGrid::GRID_WIDTH + x;
            if (hexIndex >= static_cast<int>(renderData.hexGrid->grid().size())) {
                continue;
            }

            // Get the hex data for this grid index
            const auto& hex = renderData.hexGrid->grid().at(hexIndex);
            int actualHexPosition = hex.position();

            // Skip rendering the hex that's currently being highlighted
            if (actualHexPosition == renderData.currentHoverHex) {
                continue;
            }

            int hexWorldX = hex.x();
            int hexWorldY = hex.y();

            // Viewport culling - only render visible hex sprites
            if (isHexVisible(hexWorldX, hexWorldY, view)) {
                // Position hex sprite
                float spriteX = static_cast<float>(hexWorldX - Hex::HEX_WIDTH);
                float spriteY = static_cast<float>(hexWorldY - Hex::HEX_HEIGHT + 4);

                // Create a mutable copy for positioning
                sf::Sprite hexSprite = *renderData.hexSprite;
                hexSprite.setPosition({ spriteX, spriteY });
                window->draw(hexSprite);
            }
        }
    }
}

void RenderingEngine::renderObjects(sf::RenderWindow* window,
                                   const RenderData& renderData,
                                   const VisibilitySettings& visibility) {
    if (!renderData.objects) {
        return;
    }

    for (const auto& object : *renderData.objects) {
        // Filter wall objects based on show walls setting
        if (!visibility.showWalls && object->getMapObject().isWallObject()) {
            continue;
        }
        
        // Filter scroll blockers based on show scroll blockers setting
        if (!visibility.showScrollBlockers && object->getMapObject().isScrollBlocker()) {
            continue;
        }
        
        window->draw(object->getSprite());
        
        // Draw light overlay if enabled and object has light
        if (visibility.showLightOverlays && object->hasLight()) {
            window->draw(object->getLightOverlay());
        }
    }
    
    // Render wall blocker overlays on top of regular objects
    if (visibility.showWallBlockers && renderData.wallBlockerOverlays) {
        for (const auto& overlay : *renderData.wallBlockerOverlays) {
            window->draw(overlay);
        }
    }
}

void RenderingEngine::renderRoofTiles(sf::RenderWindow* window,
                                     const RenderData& renderData,
                                     bool showRoof) {
    if (!showRoof) {
        return;
    }

    // First draw background sprites for selected roof tiles
    if (renderData.selectedRoofTileBackgroundSprites) {
        for (const auto& backgroundSprite : *renderData.selectedRoofTileBackgroundSprites) {
            window->draw(backgroundSprite);
        }
    }

    // Then draw the roof sprites on top
    if (renderData.roofSprites) {
        for (const auto& roof : *renderData.roofSprites) {
            window->draw(roof);
        }
    }
}

void RenderingEngine::renderSelectionVisuals(sf::RenderWindow* window,
                                            const RenderData& renderData) {
    // Render selected hex sprites
    if (renderData.selectedHexSprites) {
        for (const auto& hexSprite : *renderData.selectedHexSprites) {
            window->draw(hexSprite);
        }
    }

    // Render drag selection rectangle
    if (renderData.isDragSelecting && renderData.selectionRectangle) {
        // Create a mutable copy to apply colors
        sf::RectangleShape rectangle = *renderData.selectionRectangle;
        applySelectionRectangleColors(rectangle, renderData.currentSelectionMode);
        window->draw(rectangle);
    }
}

void RenderingEngine::renderHexHighlights(sf::RenderWindow* window,
                                         const RenderData& renderData) {
    if (!renderData.hexGrid) {
        return;
    }

    // Render hex highlight if there's a valid hover hex
    if (renderData.currentHoverHex >= 0 && renderData.hexHighlightSprite) {
        // Find the hex with the matching position
        for (const auto& hex : renderData.hexGrid->grid()) {
            if (hex.position() == static_cast<uint32_t>(renderData.currentHoverHex)) {
                float spriteX = static_cast<float>(hex.x() + SpriteOffset::HEX_HIGHLIGHT_X);
                float spriteY = static_cast<float>(hex.y() + SpriteOffset::HEX_HIGHLIGHT_Y);

                // Create a mutable copy for positioning
                sf::Sprite highlightSprite = *renderData.hexHighlightSprite;
                highlightSprite.setPosition({ spriteX, spriteY });
                window->draw(highlightSprite);
                break;
            }
        }
    }
    
    // Render player default position marker
    if (renderData.map && renderData.playerPositionSprite) {
        // Cast away const to access non-const getMapFile() - this is safe as we're only reading
        uint32_t playerPosition = const_cast<Map*>(renderData.map)->getMapFile().header.player_default_position;
        if (playerPosition < HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT) {
            // Find the hex with the matching position
            for (const auto& hex : renderData.hexGrid->grid()) {
                if (hex.position() == playerPosition) {
                    float spriteX = static_cast<float>(hex.x() + SpriteOffset::HEX_HIGHLIGHT_X);
                    float spriteY = static_cast<float>(hex.y() + SpriteOffset::HEX_HIGHLIGHT_Y);

                    // Create a mutable copy for positioning
                    sf::Sprite playerSprite = *renderData.playerPositionSprite;
                    playerSprite.setPosition({ spriteX, spriteY });
                    window->draw(playerSprite);
                    break;
                }
            }
        }
    }
}

bool RenderingEngine::isHexVisible(int hexWorldX, int hexWorldY, 
                                  const sf::View& view) const {
    sf::Vector2f viewCenter = view.getCenter();
    sf::Vector2f viewSize = view.getSize();
    
    int worldX = static_cast<int>(viewCenter.x - viewSize.x / 2);
    int worldY = static_cast<int>(viewCenter.y - viewSize.y / 2);
    int viewWidth = static_cast<int>(viewSize.x);
    int viewHeight = static_cast<int>(viewSize.y);
    
    return (hexWorldX + Hex::HEX_WIDTH * 2 > worldX && hexWorldX < worldX + viewWidth) && 
           (hexWorldY + Hex::HEX_HEIGHT + 4 > worldY && hexWorldY < worldY + viewHeight);
}

void RenderingEngine::applySelectionRectangleColors(sf::RectangleShape& rectangle, 
                                                   SelectionMode selectionMode) {
    if (selectionMode == SelectionMode::SCROLL_BLOCKER_RECTANGLE) {
        // Green colors for scroll blocker rectangle mode
        rectangle.setFillColor(sf::Color(SelectionColors::SCROLL_BLOCKER_R, 
                                       SelectionColors::SCROLL_BLOCKER_G, 
                                       SelectionColors::SCROLL_BLOCKER_B, 
                                       SelectionColors::SCROLL_BLOCKER_FILL_ALPHA));
        rectangle.setOutlineColor(sf::Color(0, 200, 0, SelectionColors::SCROLL_BLOCKER_OUTLINE_ALPHA));
    } else {
        // Default blue colors for normal selection
        rectangle.setFillColor(ColorUtils::createSelectionFillColor());
        rectangle.setOutlineColor(ColorUtils::createSelectionOutlineColor());
    }
}

void RenderingEngine::renderExitGrids(sf::RenderWindow* window,
                                     const sf::View& view,
                                     const RenderData& renderData,
                                     const Map* map) {
    if (!window || !map || !renderData.hexGrid) {
        return;
    }

    // Load exitgrid.frm texture
    ResourceManager& resourceManager = ResourceManager::getInstance();
    const sf::Texture& exitGridTexture = resourceManager.texture(ResourcePaths::Frm::EXIT_GRID);
    sf::Sprite exitGridSprite(exitGridTexture);
    
    // Render exit grids with the loaded sprite
    renderExitGridsWithSprite(window, view, renderData, map, exitGridSprite);
}

void RenderingEngine::renderExitGridsWithSprite(sf::RenderWindow* window,
                                               const sf::View& view,
                                               const RenderData& renderData,
                                               const Map* map,
                                               sf::Sprite& exitGridSprite) {

    // Get all objects from the map
    const auto& allObjects = map->objects();
    
    // Find objects for current elevation
    auto elevationIt = allObjects.find(renderData.currentElevation);
    if (elevationIt == allObjects.end()) {
        spdlog::debug("No objects found on elevation {}", renderData.currentElevation);
        return; // No objects on this elevation
    }
    
    spdlog::debug("Checking {} objects on elevation {} for exit grid markers", 
                  elevationIt->second.size(), renderData.currentElevation);
    
    int exitGridCount = 0;
    // Iterate through all objects on current elevation
    for (const auto& mapObject : elevationIt->second) {
        if (!mapObject || !mapObject->isExitGridMarker()) {
            continue;
        }

        exitGridCount++;
        spdlog::debug("Found exit grid marker at position {}", mapObject->position);

        // Get hex position from MapObject
        int hexPosition = mapObject->position;
        if (hexPosition < 0 || hexPosition >= HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT) {
            continue;
        }

        // Convert hex position to world coordinates using getHexByPosition
        auto hexOptional = renderData.hexGrid->getHexByPosition(hexPosition);
        if (!hexOptional.has_value()) {
            continue;
        }
        
        const Hex& hex = hexOptional.value().get();
        WorldCoords hexCenter(hex.x(), hex.y());

        // Check if hex is visible in viewport
        if (!isHexVisible(hexCenter.x(), hexCenter.y(), view)) {
            continue;
        }

        // Position the exit grid sprite at the hex center
        exitGridSprite.setPosition(hexCenter.toVector());
        
        // Draw the exit grid marker
        window->draw(exitGridSprite);
        spdlog::debug("Rendered exit grid marker at world position ({}, {})", 
                      hexCenter.x(), hexCenter.y());
    }
    
    spdlog::debug("Total exit grid markers found and processed: {}", exitGridCount);
}

} // namespace geck