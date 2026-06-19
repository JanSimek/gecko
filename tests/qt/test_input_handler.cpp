#include <catch2/catch_test_macros.hpp>

#include <QtGlobal>

#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Window/Event.hpp>

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
