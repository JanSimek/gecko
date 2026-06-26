#include <catch2/catch_test_macros.hpp>

#include <QtGlobal>

#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Window/Event.hpp>

#include <cstddef>
#include <vector>

#include "ui/input/InputHandler.h"

using geck::InputHandler;

namespace {

// InputHandler::pixelToWorld() needs a real RenderTarget, which needs an OpenGL
// context. On a headless CI runner there is no display and SFML *aborts* (SIGABRT)
// while creating the context rather than failing gracefully — so we must decide to
// skip BEFORE touching sf::RenderTexture. These cases therefore run locally (where
// a GL context exists) and skip in CI; they exist to characterise the per-mode
// dispatch and pin it across refactors.
bool glContextUnavailable() {
    // qEnvironmentVariableIsSet is cross-platform and avoids MSVC's C4996 getenv
    // warning (which /WX turns into a hard error).
    return qEnvironmentVariableIsSet("CI") || qEnvironmentVariableIsSet("GITHUB_ACTIONS");
}

struct Harness {
    sf::RenderTexture target;
    sf::View view;
    InputHandler handler;

    Harness() {
        if (!target.resize({ 800, 600 })) {
            SKIP("No OpenGL context available for sf::RenderTexture");
        }
        view = target.getView();
    }

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
};

} // namespace

// These cases characterize how InputHandler routes a left click for each active
// tool mode. They are the safety net for collapsing the per-mode bool flags into
// the single EditorMode enum: the same callbacks must fire afterwards.

TEST_CASE("InputHandler routes a click to the player-position callback and exits the mode", "[input]") {
    if (glContextUnavailable()) {
        SKIP("InputHandler dispatch test needs a display/GL context; skipped in CI");
    }
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
    if (glContextUnavailable()) {
        SKIP("InputHandler dispatch test needs a display/GL context; skipped in CI");
    }
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
    if (glContextUnavailable()) {
        SKIP("InputHandler dispatch test needs a display/GL context; skipped in CI");
    }
    Harness h;
    int calls = 0;
    InputHandler::Callbacks cb;
    cb.onStampPattern = [&](sf::Vector2f) { ++calls; };
    h.handler.setCallbacks(cb);
    h.handler.setStampPatternMode(true);

    h.leftClick(100, 100);

    CHECK(calls == 1);
}

TEST_CASE("InputHandler enters tile-placing action on click in tile mode", "[input]") {
    if (glContextUnavailable()) {
        SKIP("InputHandler dispatch test needs a display/GL context; skipped in CI");
    }
    Harness h;
    h.handler.setCallbacks(InputHandler::Callbacks{});
    h.handler.setTilePlacementMode(true, /*tileIndex*/ 5);

    h.leftClick(100, 100);

    CHECK(h.handler.getCurrentAction() == InputHandler::EditorAction::TILE_PLACING);
}

TEST_CASE("InputHandler begins drag-selection on a plain click in select mode", "[input]") {
    if (glContextUnavailable()) {
        SKIP("InputHandler dispatch test needs a display/GL context; skipped in CI");
    }
    Harness h;
    h.handler.setCallbacks(InputHandler::Callbacks{});
    // Default mode is Select with SelectionMode::ALL.

    h.leftClick(100, 100);

    CHECK(h.handler.getCurrentAction() == InputHandler::EditorAction::DRAG_SELECTING);
}

// The "Draw edge" (MarkExits) polyline state machine: clicks append vertices, mouse moves fire a
// live preview, and right-click with >=2 vertices finalizes the line.
TEST_CASE("InputHandler builds an exit-grid edge line and finalizes it on right-click", "[input][line]") {
    if (glContextUnavailable()) {
        SKIP("InputHandler dispatch test needs a display/GL context; skipped in CI");
    }
    Harness h;
    std::vector<sf::Vector2f> finalized;
    int previewCalls = 0;
    std::size_t lastPreviewVertexCount = 0;
    InputHandler::Callbacks cb;
    cb.onMarkExitsLine = [&](const std::vector<sf::Vector2f>& verts) { finalized = verts; };
    cb.onMarkExitsLinePreview = [&](const std::vector<sf::Vector2f>& verts, sf::Vector2f) {
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
    if (glContextUnavailable()) {
        SKIP("InputHandler dispatch test needs a display/GL context; skipped in CI");
    }
    Harness h;
    bool finalized = false;
    bool cancelled = false;
    InputHandler::Callbacks cb;
    cb.onMarkExitsLine = [&](const std::vector<sf::Vector2f>&) { finalized = true; };
    cb.onMarkExitsModeCancelled = [&]() { cancelled = true; };
    h.handler.setCallbacks(cb);
    h.handler.setMarkExitsMode(true);

    h.leftClick(100, 100); // only one vertex

    h.rightClick(220, 220);

    CHECK_FALSE(finalized);
    CHECK(cancelled);
    CHECK_FALSE(h.handler.isInMarkExitsMode()); // tool dropped
}
