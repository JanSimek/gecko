#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

#include "editing/input/InputHandler.h"
#include "util/ExitGridDirection.h"

#include "support/GraphicsTestEnv.h"

using geck::InputHandler;

namespace {

class HeadlessRenderTarget final : public sf::RenderTarget {
public:
    [[nodiscard]] sf::Vector2u getSize() const override { return { 800, 600 }; }
};

struct Harness {
    HeadlessRenderTarget target;
    sf::View view{ sf::FloatRect({ 0.f, 0.f }, { 800.f, 600.f }) };
    InputHandler handler;

    Harness() = default;

    void leftClick(int x, int y) {
        sf::Event::MouseButtonPressed press;
        press.button = sf::Mouse::Button::Left;
        press.position = { x, y };
        handler.handleEvent(sf::Event(press), target, view);
    }

    void rightClick(int x, int y) {
        sf::Event::MouseButtonPressed press;
        press.button = sf::Mouse::Button::Right;
        press.position = { x, y };
        handler.handleEvent(sf::Event(press), target, view);
    }

    void mouseMove(int x, int y) {
        sf::Event::MouseMoved move;
        move.position = { x, y };
        handler.handleEvent(sf::Event(move), target, view);
    }

    void pressKey(sf::Keyboard::Key key) {
        sf::Event::KeyPressed press;
        press.code = key;
        handler.handleEvent(sf::Event(press), target, view);
    }
};

} // namespace

// These cases characterize how InputHandler routes a left click for each active
// tool mode. They are the safety net for collapsing the per-mode bool flags into
// the single EditorMode enum: the same callbacks must fire afterwards.

TEST_CASE("InputHandler routes a click to the active placement mode's callback", "[input_handler]") {
    using EnterMode = void (InputHandler::*)(bool);
    using ClickSlot = std::function<void(sf::Vector2f)> InputHandler::Callbacks::*;
    struct Route {
        const char* mode;
        EnterMode enter;
        ClickSlot callback;
    };
    const Route route = GENERATE(values<Route>({
        { "player position", &InputHandler::setPlayerPositionMode, &InputHandler::Callbacks::onPlayerPositionSelect },
        { "exit-grid placement", &InputHandler::setExitGridPlacementMode, &InputHandler::Callbacks::onExitGridPlacement },
        { "stamp pattern", &InputHandler::setStampPatternMode, &InputHandler::Callbacks::onStampPattern },
    }));
    INFO(route.mode);

    Harness h;
    int calls = 0;
    InputHandler::Callbacks cb;
    cb.*route.callback = [&calls](sf::Vector2f) { ++calls; };
    h.handler.setCallbacks(cb);
    (h.handler.*route.enter)(true);

    h.leftClick(100, 100);

    CHECK(calls == 1);
    // Setting the player position is one click: the mode exits after it.
    CHECK_FALSE(h.handler.isInPlayerPositionMode());
}

TEST_CASE("InputHandler forwards mouse events to the active registered-tool branch", "[input_handler]") {
    Harness h;
    int pressed = 0;
    int moved = 0;
    int released = 0;
    sf::Mouse::Button lastButton = sf::Mouse::Button::Middle;
    InputHandler::Callbacks cb;
    cb.onToolMousePressed = [&pressed, &lastButton](sf::Vector2f, sf::Mouse::Button button) {
        ++pressed;
        lastButton = button;
        return true;
    };
    cb.onToolMouseMoved = [&moved](sf::Vector2f) {
        ++moved;
        return true;
    };
    cb.onToolMouseReleased = [&released](sf::Vector2f, sf::Mouse::Button) {
        ++released;
        return true;
    };
    h.handler.setCallbacks(cb);
    h.handler.setPluginToolMode(true);

    h.leftClick(100, 100);
    h.mouseMove(120, 130);

    sf::Event::MouseButtonReleased release;
    release.button = sf::Mouse::Button::Left;
    release.position = { 120, 130 };
    h.handler.handleEvent(sf::Event(release), h.target, h.view);

    CHECK(pressed == 1);
    CHECK(moved == 1);
    CHECK(released == 1);
    CHECK(lastButton == sf::Mouse::Button::Left);
}

TEST_CASE("InputHandler falls back when a registered-tool mouse press is not consumed", "[input_handler]") {
    GECK_SKIP_IF_NO_KEYBOARD_STATE();
    Harness h;
    InputHandler::Callbacks cb;
    cb.onToolMousePressed = [](sf::Vector2f, sf::Mouse::Button) {
        return false;
    };
    h.handler.setCallbacks(cb);
    h.handler.setPluginToolMode(true);

    h.leftClick(100, 100);

    CHECK(h.handler.getCurrentAction() == InputHandler::EditorAction::DRAG_SELECTING);
}

TEST_CASE("InputHandler cancels the active registered-tool branch on unconsumed right-click", "[input_handler]") {
    Harness h;
    bool cancelled = false;
    InputHandler::Callbacks cb;
    cb.onToolMousePressed = [](sf::Vector2f, sf::Mouse::Button) {
        return false;
    };
    cb.onEscape = [&cancelled]() {
        cancelled = true;
    };
    h.handler.setCallbacks(cb);
    h.handler.setPluginToolMode(true);

    h.rightClick(100, 100);

    CHECK(cancelled);
    CHECK(h.handler.getCurrentAction() == InputHandler::EditorAction::NONE);
}

TEST_CASE("InputHandler enters tile-placing action on click in tile mode", "[input_handler]") {
    GECK_SKIP_IF_NO_KEYBOARD_STATE();
    Harness h;
    h.handler.setCallbacks(InputHandler::Callbacks{});
    h.handler.setTilePlacementMode(true, /*tileIndex*/ 5);

    h.leftClick(100, 100);

    CHECK(h.handler.getCurrentAction() == InputHandler::EditorAction::TILE_PLACING);
}

TEST_CASE("InputHandler begins drag-selection on a plain click in select mode", "[input_handler]") {
    GECK_SKIP_IF_NO_KEYBOARD_STATE();
    Harness h;
    h.handler.setCallbacks(InputHandler::Callbacks{});
    // Default mode is Select with SelectionMode::ALL.

    h.leftClick(100, 100);

    CHECK(h.handler.getCurrentAction() == InputHandler::EditorAction::DRAG_SELECTING);
}

// The "Draw edge" polyline state machine: clicks append vertices, moves fire a live preview, right-click
// with >=2 vertices finalizes.
TEST_CASE("InputHandler builds an exit-grid edge line and finalizes it on right-click", "[input_handler]") {
    GECK_SKIP_IF_NO_KEYBOARD_STATE();
    Harness h;
    std::vector<sf::Vector2f> finalized;
    int previewCalls = 0;
    std::size_t lastPreviewVertexCount = 0;
    InputHandler::Callbacks cb;
    cb.onMarkExitsLine = [&finalized](const std::vector<sf::Vector2f>& verts, bool) { finalized = verts; };
    cb.onMarkExitsLinePreview = [&previewCalls, &lastPreviewVertexCount](
                                    const std::vector<sf::Vector2f>& verts, sf::Vector2f, bool) {
        ++previewCalls;
        lastPreviewVertexCount = verts.size();
    };
    h.handler.setCallbacks(cb);
    h.handler.setMarkExitsMode(true);
    REQUIRE(h.handler.isInMarkExitsMode());

    // Two vertices via two (well-separated) left clicks — a line needs only two points.
    h.leftClick(100, 100);
    h.leftClick(300, 100);

    // A mouse move fires the live preview with the two committed vertices.
    h.mouseMove(220, 220);
    CHECK(previewCalls >= 1);
    CHECK(lastPreviewVertexCount == 2);

    // Right-click finalizes (>=2 vertices) and the line is reported, then the vertices clear.
    h.rightClick(220, 220);
    CHECK(finalized.size() == 2);
    CHECK(h.handler.isInMarkExitsMode()); // still active so another edge can be drawn
}

TEST_CASE("InputHandler cancels an exit-grid edge line on right-click with too few vertices", "[input_handler]") {
    GECK_SKIP_IF_NO_KEYBOARD_STATE();
    Harness h;
    bool finalized = false;
    bool cancelled = false;
    InputHandler::Callbacks cb;
    cb.onMarkExitsLine = [&finalized](const std::vector<sf::Vector2f>&, bool) { finalized = true; };
    cb.onMarkExitsModeCancelled = [&cancelled]() { cancelled = true; };
    h.handler.setCallbacks(cb);
    h.handler.setMarkExitsMode(true);

    h.leftClick(100, 100); // only one vertex

    h.rightClick(220, 220);

    CHECK_FALSE(finalized);
    CHECK(cancelled);
    CHECK_FALSE(h.handler.isInMarkExitsMode()); // tool dropped
}

TEST_CASE("InputHandler's flip key toggles the Draw-edge side and re-fires the preview", "[input_handler]") {
    GECK_SKIP_IF_NO_KEYBOARD_STATE();
    Harness h;
    int previewCalls = 0;
    bool lastPreviewFlip = false;
    InputHandler::Callbacks cb;
    cb.onMarkExitsLinePreview = [&](const std::vector<sf::Vector2f>&, sf::Vector2f, bool flipSide) {
        ++previewCalls;
        lastPreviewFlip = flipSide;
    };
    h.handler.setCallbacks(cb);
    h.handler.setMarkExitsMode(true);

    // Default side is the auto/outward side (not flipped).
    CHECK_FALSE(h.handler.isMarkExitsFlipped());

    // Space flips to the opposite side and re-fires the preview so the new side is visible immediately.
    h.pressKey(sf::Keyboard::Key::Space);
    CHECK(h.handler.isMarkExitsFlipped());
    CHECK(previewCalls == 1);
    CHECK(lastPreviewFlip == true);

    // Space again toggles back to the default side.
    h.pressKey(sf::Keyboard::Key::Space);
    CHECK_FALSE(h.handler.isMarkExitsFlipped());
    CHECK(previewCalls == 2);
    CHECK(lastPreviewFlip == false);
}

TEST_CASE("InputHandler forwards keys to the active registered-tool branch before global handlers", "[input_handler]") {
    Harness h;
    int toolKeys = 0;
    bool deleteCalled = false;
    InputHandler::Callbacks cb;
    cb.onToolKeyPressed = [&toolKeys](const sf::Event::KeyPressed& event) {
        if (event.code == sf::Keyboard::Key::Delete) {
            ++toolKeys;
            return true;
        }
        return false;
    };
    cb.onDeleteObjects = [&deleteCalled]() { deleteCalled = true; };
    h.handler.setCallbacks(cb);
    h.handler.setPluginToolMode(true);

    h.pressKey(sf::Keyboard::Key::Delete);

    CHECK(toolKeys == 1);
    CHECK_FALSE(deleteCalled);
}

// Shift-snap wiring: the live cursor passes through maybeSnapMarkExitsCursor before preview/commit.
// Holding Shift can't be driven headlessly (sf::Keyboard reads the OS), so the snapped behaviour is
// GUI-only — see the pure snapToExitGridAngle tests for the geometry. Here we pin the no-op path: with
// a live segment that Shift would snap, Shift up leaves the vertices and the preview cursor raw.
TEST_CASE("InputHandler's Draw-edge preview leaves the cursor unsnapped when Shift is up", "[input_handler]") {
    GECK_SKIP_IF_NO_KEYBOARD_STATE();
    Harness h;
    std::vector<sf::Vector2f> lastVertices;
    sf::Vector2f lastCursor{ -1.f, -1.f };
    InputHandler::Callbacks cb;
    cb.onMarkExitsLinePreview = [&](const std::vector<sf::Vector2f>& vertices, sf::Vector2f cursor, bool) {
        lastVertices = vertices;
        lastCursor = cursor;
    };
    h.handler.setCallbacks(cb);
    h.handler.setMarkExitsMode(true);

    // Two vertices make a committed segment, so the live one from the last vertex is snappable.
    // Clicks map pixel -> world through the view, so positions compare within a float tolerance.
    const auto isNear = [](sf::Vector2f a, sf::Vector2f b) { return std::abs(a.x - b.x) < 0.01f && std::abs(a.y - b.y) < 0.01f; };
    h.leftClick(100, 100);
    h.leftClick(300, 100);
    const sf::Vector2f raw{ 337.f, 161.f };
    const auto [snappedX, snappedY] = geck::snapToExitGridAngle(300.0, 100.0, raw.x, raw.y);
    REQUIRE_FALSE(isNear({ static_cast<float>(snappedX), static_cast<float>(snappedY) }, raw)); // Shift would move it

    h.mouseMove(static_cast<int>(raw.x), static_cast<int>(raw.y));
    REQUIRE(lastVertices.size() == 2);
    CHECK(isNear(lastVertices[0], { 100.f, 100.f }));
    CHECK(isNear(lastVertices[1], { 300.f, 100.f }));
    CHECK(isNear(lastCursor, raw));

    // The flip key's re-fire passes the same raw cursor.
    lastCursor = { -1.f, -1.f };
    h.pressKey(sf::Keyboard::Key::Space);
    CHECK(isNear(lastCursor, raw));
}

TEST_CASE("InputHandler's flip key does nothing outside Draw-edge mode, and resets on mode change", "[input_handler]") {
    GECK_SKIP_IF_NO_KEYBOARD_STATE();
    Harness h;
    int previewCalls = 0;
    InputHandler::Callbacks cb;
    cb.onMarkExitsLinePreview = [&](const std::vector<sf::Vector2f>&, sf::Vector2f, bool) { ++previewCalls; };
    h.handler.setCallbacks(cb);

    // Not in MarkExits mode: Space is ignored (no toggle, no preview).
    h.pressKey(sf::Keyboard::Key::Space);
    CHECK_FALSE(h.handler.isMarkExitsFlipped());
    CHECK(previewCalls == 0);

    // Enter the mode, flip, then leave: the toggle resets to the default.
    h.handler.setMarkExitsMode(true);
    h.pressKey(sf::Keyboard::Key::Space);
    CHECK(h.handler.isMarkExitsFlipped());
    h.handler.setMarkExitsMode(false); // back to Select
    CHECK_FALSE(h.handler.isMarkExitsFlipped());
}
