#pragma once

#include <SFML/Window/Event.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/System/Clock.hpp>
#include <functional>
#include <vector>
#include <spdlog/spdlog.h>
#include "ui/core/EditorMode.h"
#include "util/Types.h"

namespace geck {

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
        std::function<void(sf::Vector2f startPos, sf::Vector2f endPos, SelectionModifier modifier)> onDragSelection;
        std::function<void(sf::Vector2f startPos, sf::Vector2f currentPos, SelectionModifier modifier)> onDragSelectionPreview;
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
        std::function<void(sf::Vector2f worldPos)> onExitGridPlacement;
        std::function<void(sf::Vector2f worldPos)> onStampPattern;
        std::function<void()> onStampPatternCancel;
        std::function<void()> onStampCycleVariant;
        // Polyline "Draw edge" mode. onMarkExitsLinePreview fires on every mouse move (and on a flip-
        // key toggle) with the vertices committed so far plus the live cursor (to draw the in-progress
        // edge); onMarkExitsLine fires once on finalize with the finished vertex polyline. `flipSide`
        // is the current state of the flip toggle (the F key) — when true the edge's side is inverted
        // (south<->north, ...), so the preview and the commit both reflect it.
        std::function<void(const std::vector<sf::Vector2f>& vertices, sf::Vector2f cursor, bool flipSide)> onMarkExitsLinePreview;
        std::function<void(const std::vector<sf::Vector2f>& vertices, bool flipSide)> onMarkExitsLine;

        // Hover
        std::function<void(sf::Vector2f worldPos)> onMouseMove;

        // Keyboard
        std::function<void()> onEscape;
        std::function<void()> onDeleteObjects;

        // Mode cancellation
        std::function<void()> onMarkExitsModeCancelled;
    };

    InputHandler() = default;
    ~InputHandler() = default;

    /**
     * @brief Main event handler - processes SFML events
     * @param event The SFML event to process
     * @param window The render window for coordinate conversion
     * @param view The current view for world coordinate mapping
     */
    void handleEvent(const sf::Event& event,
        sf::RenderTarget& target,
        const sf::View& view);

    /// Handle a key press directly. Keys never use the RenderTarget/view (only mouse events convert
    /// pixels), so this is target-free — and public so headless tests can drive key behaviour without
    /// constructing a GL-backed RenderTexture (which aborts on a display-less CI runner).
    void handleKeyPressed(const sf::Event::KeyPressed& event);

    /**
     * @brief Set callbacks for input events
     */
    void setCallbacks(const Callbacks& callbacks) { _callbacks = callbacks; }

    /**
     * @brief State queries
     */
    EditorAction getCurrentAction() const { return _currentAction; }
    bool isDragging() const { return _isDragging; }
    bool isInPlayerPositionMode() const { return _mode == EditorMode::SetPlayerPosition; }
    bool isInTilePlacementMode() const { return _mode == EditorMode::PlaceTile; }
    bool isInExitGridPlacementMode() const { return _mode == EditorMode::PlaceExitGrid; }
    bool isInMarkExitsMode() const { return _mode == EditorMode::MarkExits; }
    // The "Draw edge" flip toggle: false = the auto/outward side, true = the opposite side. Toggled by
    // the flip key (F) while drawing an edge; exposed for tests of the toggle state.
    bool isMarkExitsFlipped() const { return _markExitsFlip; }

    /**
     * @brief Mode setters
     *
     * The active tool is a single EditorMode value, mutually exclusive by
     * construction. Each bool setter enables its mode or falls back to Select.
     */
    void setPlayerPositionMode(bool enabled) { setActiveMode(enabled, EditorMode::SetPlayerPosition); }
    void setExitGridPlacementMode(bool enabled) { setActiveMode(enabled, EditorMode::PlaceExitGrid); }
    void setStampPatternMode(bool enabled) { setActiveMode(enabled, EditorMode::StampPattern); }
    bool isInStampPatternMode() const { return _mode == EditorMode::StampPattern; }
    void setMarkExitsMode(bool enabled) { setActiveMode(enabled, EditorMode::MarkExits); }
    void setTilePlacementMode(bool enabled, int tileIndex = -1, bool replaceMode = false);
    void setSelectionMode(SelectionMode mode) { _selectionMode = mode; }

private:
    // Event handlers
    void handleMousePressed(const sf::Event::MouseButtonPressed& event,
        sf::RenderTarget& target,
        const sf::View& view);
    void handleMouseReleased(const sf::Event::MouseButtonReleased& event,
        sf::RenderTarget& target,
        const sf::View& view);
    void handleMouseMoved(const sf::Event::MouseMoved& event,
        sf::RenderTarget& target,
        const sf::View& view);
    void handleMouseWheelScrolled(const sf::Event::MouseWheelScrolled& event);
    void handleKeyReleased(const sf::Event::KeyReleased& event);

    // Helper methods
    SelectionModifier getSelectionModifier() const;
    sf::Vector2f pixelToWorld(sf::Vector2i pixelPos, sf::RenderTarget& target, const sf::View& view);
    bool isShiftPressed() const;
    static bool isCtrlPressed();
    // In "Draw edge" mode with Ctrl held, snap `cursor` (the live-segment endpoint) to the nearest
    // clean exit-grid angle relative to the last committed vertex, so the live segment is a straight
    // aligned edge. Returns `cursor` unchanged when Ctrl is up or there is no committed vertex yet.
    sf::Vector2f maybeSnapMarkExitsCursor(sf::Vector2f cursor) const;
    void setActiveMode(bool enabled, EditorMode mode) {
        _mode = enabled ? mode : EditorMode::Select;
        _lineVertices.clear();  // any mode change abandons an in-progress exit-grid edge line
        _markExitsFlip = false; // and resets the flip toggle to the default (auto/outward) side
    }

    // "Draw edge" polyline state machine helpers (MarkExits mode).
    void finalizeExitGridLine(); // fires onMarkExitsLine if >=2 vertices, then clears
    void cancelExitGridLine();   // clears vertices and drops the tool (onMarkExitsModeCancelled)

    // A finished left-button drag in DRAG_SELECTING: either a scroll-blocker rectangle or a normal
    // drag selection. Split out of handleMouseReleased to keep that switch's nesting shallow.
    void finishDragSelectRelease(sf::Vector2f worldPos);

    // State
    Callbacks _callbacks;

    EditorAction _currentAction = EditorAction::NONE;
    SelectionMode _selectionMode = SelectionMode::ALL;

    // Mouse state
    sf::Vector2i _mouseStartPos{ 0, 0 };
    sf::Vector2i _mouseLastPos{ 0, 0 };
    // The last cursor position in WORLD coordinates (updated on every mouse move). Key events carry no
    // view/target to convert pixels, so the flip key reuses this to re-fire the live edge preview at
    // the current cursor.
    sf::Vector2f _mouseLastWorldPos;
    sf::Vector2f _dragStartWorldPos;
    bool _isDragging = false;
    bool _immediateSelectionPerformed = false;
    SelectionModifier _dragSelectionModifier = SelectionModifier::NONE; // modifier held when a drag-select began

    // Active tool mode — a single value, mutually exclusive by construction
    // (replaces the former bank of per-mode bools).
    EditorMode _mode = EditorMode::Select;
    // Vertices clicked so far for the in-progress exit-grid "Draw edge" line (MarkExits mode);
    // cleared on finalize/cancel/mode-change.
    std::vector<sf::Vector2f> _lineVertices;
    // The "Draw edge" flip toggle: the F key flips which side the edge's bars sit on (south<->north,
    // left<->right, the two diagonal sides). Default false = the auto/outward side. Reset on mode
    // change; persists across consecutive edges drawn in one MarkExits session so a chosen side sticks.
    bool _markExitsFlip = false;
    // Double-click detection for finalizing a "Draw edge" line (time since the last vertex
    // click + how far the cursor moved). The clock restarts on every line vertex click.
    sf::Clock _doubleClickClock;
    static constexpr float kDoubleClickSeconds = 0.4f;
    static constexpr float kDoubleClickWorldDistance = 12.0f;
    bool _tilePlacementReplaceMode = false;
    int _tilePlacementIndex = -1;
    bool _tilePlacementIsRoof = false;
};

} // namespace geck
