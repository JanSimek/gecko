#pragma once

#include <SFML/Window/Event.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/View.hpp>
#include <functional>
#include "../../util/Types.h"

namespace geck {

// Forward declarations
class EditorWidget;

/**
 * @brief Handles all input events for the editor
 * 
 * This class encapsulates input event processing that was previously
 * in EditorWidget, providing clean separation between input handling
 * and editor logic.
 */
class InputHandler {
public:
    /**
     * @brief Input state and modifiers
     */
    enum class SelectionModifier {
        NONE,   // Normal single selection (clear and select)
        ADD,    // Alt+Click - add to selection
        TOGGLE, // Ctrl+Click - toggle selection
        RANGE   // Shift+Click - range selection for tiles
    };

    enum class EditorAction {
        NONE,
        PANNING,
        DRAG_SELECTING,
        TILE_PLACING,
        OBJECT_MOVING
    };

    /**
     * @brief Callbacks for different input events
     */
    struct Callbacks {
        // Mouse events
        std::function<void(sf::Vector2f worldPos, SelectionModifier modifier)> onSelectionClick;
        std::function<void(sf::Vector2f startPos, sf::Vector2f endPos)> onDragSelection;
        std::function<void(sf::Vector2f startPos, sf::Vector2f currentPos)> onDragSelectionPreview;
        std::function<void(sf::Vector2f worldPos)> onTilePlacement;
        std::function<void(sf::Vector2f startPos, sf::Vector2f endPos, bool isRoof)> onTileAreaFill;
        std::function<void(sf::Vector2f delta)> onPan;
        std::function<void(float direction)> onZoom;
        
        // Object dragging
        std::function<bool(sf::Vector2f worldPos)> canStartObjectDrag;
        std::function<bool(sf::Vector2f worldPos)> onObjectDragStart;
        std::function<void(sf::Vector2f worldPos)> onObjectDragUpdate;
        std::function<void(sf::Vector2f worldPos)> onObjectDragEnd;
        std::function<void()> onObjectDragCancel;
        
        // Special modes
        std::function<void(sf::Vector2f worldPos)> onPlayerPositionSelect;
        std::function<void(sf::FloatRect area)> onScrollBlockerRectangle;
        std::function<void()> onTilePlacementCancel;
        
        // Hover
        std::function<void(sf::Vector2f worldPos)> onMouseMove;
        
        // Keyboard
        std::function<void()> onEscape;
        std::function<void()> onDeleteObjects;
    };

    explicit InputHandler(EditorWidget* editor);
    ~InputHandler() = default;

    /**
     * @brief Main event handler - processes SFML events
     * @param event The SFML event to process
     * @param window The render window for coordinate conversion
     * @param view The current view for world coordinate mapping
     */
    void handleEvent(const sf::Event& event, 
                    sf::RenderWindow* window,
                    const sf::View& view);

    /**
     * @brief Set callbacks for input events
     */
    void setCallbacks(const Callbacks& callbacks) { _callbacks = callbacks; }

    /**
     * @brief State queries
     */
    EditorAction getCurrentAction() const { return _currentAction; }
    bool isDragging() const { return _isDragging; }
    bool isInPlayerPositionMode() const { return _playerPositionMode; }
    bool isInTilePlacementMode() const { return _tilePlacementMode; }
    
    /**
     * @brief Mode setters
     */
    void setPlayerPositionMode(bool enabled) { _playerPositionMode = enabled; }
    void setTilePlacementMode(bool enabled, int tileIndex = -1, bool replaceMode = false);
    void setSelectionMode(SelectionMode mode) { _selectionMode = mode; }

private:
    // Event handlers
    void handleMousePressed(const sf::Event::MouseButtonPressed& event, 
                           sf::RenderWindow* window,
                           const sf::View& view);
    void handleMouseReleased(const sf::Event::MouseButtonReleased& event,
                            sf::RenderWindow* window,
                            const sf::View& view);
    void handleMouseMoved(const sf::Event::MouseMoved& event,
                         sf::RenderWindow* window,
                         const sf::View& view);
    void handleMouseWheelScrolled(const sf::Event::MouseWheelScrolled& event);
    void handleKeyPressed(const sf::Event::KeyPressed& event);
    void handleKeyReleased(const sf::Event::KeyReleased& event);
    void handleWindowResized(const sf::Event::Resized& event, sf::RenderWindow* window);

    // Helper methods
    SelectionModifier getSelectionModifier() const;
    sf::Vector2f pixelToWorld(sf::Vector2i pixelPos, sf::RenderWindow* window, const sf::View& view);
    bool isShiftPressed() const;

    // State
    EditorWidget* _editor;
    Callbacks _callbacks;
    
    EditorAction _currentAction = EditorAction::NONE;
    SelectionMode _selectionMode = SelectionMode::ALL;
    
    // Mouse state
    sf::Vector2i _mouseStartPos{0, 0};
    sf::Vector2i _mouseLastPos{0, 0};
    sf::Vector2f _dragStartWorldPos;
    bool _isDragging = false;
    bool _immediateSelectionPerformed = false;
    
    // Mode flags
    bool _playerPositionMode = false;
    bool _tilePlacementMode = false;
    bool _tilePlacementReplaceMode = false;
    int _tilePlacementIndex = -1;
    bool _tilePlacementIsRoof = false;
};

} // namespace geck