#include <catch2/catch_test_macros.hpp>

#include <QtGlobal>

#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <cstddef>
#include <vector>

#include "ui/input/InputHandler.h"

using geck::InputHandler;

namespace {

bool keyboardStateUnavailable() {
#if defined(Q_OS_LINUX)
    return qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
#else
    return false;
#endif
}

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

TEST_CASE("InputHandler routes a click to the player-position callback and exits the mode", "[input]") {
    Harness h;
    bool called = false;
    InputHandler::Callbacks cb;
    cb.onPlayerPositionSelect = [&](sf::Vector2f) { called = true; };
    h.handler.setCallbacks(cb);
    h.handler.setPlayerPositionMode(true);

    h.leftClick(100, 100);

    CHECK(called);
    CHECK_FALSE(h.handler.isInPlayerPositionMode());
}

TEST_CASE("InputHandler routes a click to the exit-grid placement callback", "[input]") {
    Harness h;
    int calls = 0;
    InputHandler::Callbacks cb;
    cb.onExitGridPlacement = [&](sf::Vector2f) { ++calls; };
    h.handler.setCallbacks(cb);
    h.handler.setExitGridPlacementMode(true);

    h.leftClick(100, 100);

    CHECK(calls == 1);
}

TEST_CASE("InputHandler routes a click to the stamp-pattern callback", "[input]") {
    Harness h;
    int calls = 0;
    InputHandler::Callbacks cb;
    cb.onStampPattern = [&](sf::Vector2f) { ++calls; };
    h.handler.setCallbacks(cb);
    h.handler.setStampPatternMode(true);

    h.leftClick(100, 100);

    CHECK(calls == 1);
}

TEST_CASE("InputHandler forwards mouse events to the active registered-tool branch", "[input][tools]") {
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

TEST_CASE("InputHandler falls back when a registered-tool mouse press is not consumed", "[input][tools]") {
    if (keyboardStateUnavailable()) {
        SKIP("This path queries live keyboard state, which SFML cannot do on headless Linux");
    }
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

TEST_CASE("InputHandler cancels the active registered-tool branch on unconsumed right-click", "[input][tools]") {
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

TEST_CASE("InputHandler enters tile-placing action on click in tile mode", "[input]") {
    if (keyboardStateUnavailable()) {
        SKIP("This path queries live keyboard state, which SFML cannot do on headless Linux");
    }
    Harness h;
    h.handler.setCallbacks(InputHandler::Callbacks{});
    h.handler.setTilePlacementMode(true, /*tileIndex*/ 5);

    h.leftClick(100, 100);

    CHECK(h.handler.getCurrentAction() == InputHandler::EditorAction::TILE_PLACING);
}

TEST_CASE("InputHandler begins drag-selection on a plain click in select mode", "[input]") {
    if (keyboardStateUnavailable()) {
        SKIP("This path queries live keyboard state, which SFML cannot do on headless Linux");
    }
    Harness h;
    h.handler.setCallbacks(InputHandler::Callbacks{});
    // Default mode is Select with SelectionMode::ALL.

    h.leftClick(100, 100);

    CHECK(h.handler.getCurrentAction() == InputHandler::EditorAction::DRAG_SELECTING);
}

// The "Draw edge" polyline state machine: clicks append vertices, moves fire a live preview, right-click
// with >=2 vertices finalizes.
TEST_CASE("InputHandler builds an exit-grid edge line and finalizes it on right-click", "[input][line]") {
    if (keyboardStateUnavailable()) {
        SKIP("This path queries live keyboard state, which SFML cannot do on headless Linux");
    }
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

TEST_CASE("InputHandler cancels an exit-grid edge line on right-click with too few vertices", "[input][line]") {
    if (keyboardStateUnavailable()) {
        SKIP("This path queries live keyboard state, which SFML cannot do on headless Linux");
    }
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

// KeyPressed never touches the RenderTarget/view, so drive key behavior directly in tests.
namespace {
void pressKeyDirect(InputHandler& handler, sf::Keyboard::Key key) {
    sf::Event::KeyPressed press;
    press.code = key;
    handler.handleKeyPressed(press);
}
} // namespace

TEST_CASE("InputHandler's flip key toggles the Draw-edge side and re-fires the preview", "[input][line][flip]") {
    if (keyboardStateUnavailable()) {
        SKIP("This path queries live keyboard state, which SFML cannot do on headless Linux");
    }
    InputHandler handler;
    int previewCalls = 0;
    bool lastPreviewFlip = false;
    InputHandler::Callbacks cb;
    cb.onMarkExitsLinePreview = [&](const std::vector<sf::Vector2f>&, sf::Vector2f, bool flipSide) {
        ++previewCalls;
        lastPreviewFlip = flipSide;
    };
    handler.setCallbacks(cb);
    handler.setMarkExitsMode(true);

    // Default side is the auto/outward side (not flipped).
    CHECK_FALSE(handler.isMarkExitsFlipped());

    // Space flips to the opposite side and re-fires the preview so the new side is visible immediately.
    pressKeyDirect(handler, sf::Keyboard::Key::Space);
    CHECK(handler.isMarkExitsFlipped());
    CHECK(previewCalls == 1);
    CHECK(lastPreviewFlip == true);

    // Space again toggles back to the default side.
    pressKeyDirect(handler, sf::Keyboard::Key::Space);
    CHECK_FALSE(handler.isMarkExitsFlipped());
    CHECK(previewCalls == 2);
    CHECK(lastPreviewFlip == false);
}

TEST_CASE("InputHandler forwards keys to the active registered-tool branch before global handlers", "[input][tools]") {
    InputHandler handler;
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
    handler.setCallbacks(cb);
    handler.setPluginToolMode(true);

    pressKeyDirect(handler, sf::Keyboard::Key::Delete);

    CHECK(toolKeys == 1);
    CHECK_FALSE(deleteCalled);
}

// Shift-snap wiring: the live cursor passes through maybeSnapMarkExitsCursor before preview/commit.
// Holding Shift can't be driven headlessly (sf::Keyboard reads the OS), so the snapped behaviour is
// GUI-only — see the pure snapToExitGridAngle tests for the geometry. Here we pin the no-op path: with
// Shift up the re-fired preview cursor is the raw cursor.
TEST_CASE("InputHandler's Draw-edge preview leaves the cursor unsnapped when Shift is up", "[input][line][snap]") {
    if (keyboardStateUnavailable()) {
        SKIP("This path queries live keyboard state, which SFML cannot do on headless Linux");
    }
    InputHandler handler;
    sf::Vector2f lastCursor{ -1.f, -1.f };
    InputHandler::Callbacks cb;
    cb.onMarkExitsLinePreview = [&](const std::vector<sf::Vector2f>&, sf::Vector2f cursor, bool) {
        lastCursor = cursor;
    };
    handler.setCallbacks(cb);
    handler.setMarkExitsMode(true);

    // No committed vertex yet: snap can't apply (no segment), so the cursor (the key re-fire's last
    // world pos, default 0,0) is passed through unchanged regardless of Shift.
    pressKeyDirect(handler, sf::Keyboard::Key::Space);
    CHECK(lastCursor == sf::Vector2f{ 0.f, 0.f });
}

TEST_CASE("InputHandler's flip key does nothing outside Draw-edge mode, and resets on mode change", "[input][line][flip]") {
    if (keyboardStateUnavailable()) {
        SKIP("This path queries live keyboard state, which SFML cannot do on headless Linux");
    }
    InputHandler handler;
    int previewCalls = 0;
    InputHandler::Callbacks cb;
    cb.onMarkExitsLinePreview = [&](const std::vector<sf::Vector2f>&, sf::Vector2f, bool) { ++previewCalls; };
    handler.setCallbacks(cb);

    // Not in MarkExits mode: Space is ignored (no toggle, no preview).
    pressKeyDirect(handler, sf::Keyboard::Key::Space);
    CHECK_FALSE(handler.isMarkExitsFlipped());
    CHECK(previewCalls == 0);

    // Enter the mode, flip, then leave: the toggle resets to the default.
    handler.setMarkExitsMode(true);
    pressKeyDirect(handler, sf::Keyboard::Key::Space);
    CHECK(handler.isMarkExitsFlipped());
    handler.setMarkExitsMode(false); // back to Select
    CHECK_FALSE(handler.isMarkExitsFlipped());
}
