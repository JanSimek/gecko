#include <catch2/catch_test_macros.hpp>

#include <QString>

#include "ui/core/EditorHints.h"
#include "ui/core/EditorMode.h"

using geck::EditorMode;
using geck::hintForContext;

// The hint provider is pure and headless: it maps (mode, hasSelection) to the keys that
// genuinely act in that context. These pin the per-mode keys against InputHandler so the
// status-bar hint can't silently drift from the real bindings. No GL / widgets needed.

TEST_CASE("MarkExits hint advertises Space to flip the edge side", "[hints]") {
    const QString hint = hintForContext(EditorMode::MarkExits, false);
    REQUIRE(hint.contains("Space"));
    REQUIRE(hint.contains("flip"));
    // Enter / double-click finish the line; Esc cancels it.
    REQUIRE(hint.contains("Esc"));
    REQUIRE(hint.contains("finish"));
}

TEST_CASE("MarkExits hint advertises Shift to snap the live segment to a clean angle", "[hints]") {
    const QString hint = hintForContext(EditorMode::MarkExits, false);
    REQUIRE(hint.contains("Shift"));
    REQUIRE(hint.contains("snap"));
}

TEST_CASE("PlaceExitGrid hint advertises Esc to leave the tool", "[hints]") {
    const QString hint = hintForContext(EditorMode::PlaceExitGrid, false);
    REQUIRE(hint.contains("Esc"));
    REQUIRE(hint.contains("place exit grid"));
}

TEST_CASE("Select mode with a selection advertises R and Delete", "[hints]") {
    const QString hint = hintForContext(EditorMode::Select, /*hasSelection=*/true);
    REQUIRE(hint.contains("R"));
    REQUIRE(hint.contains("rotate"));
    REQUIRE(hint.contains("Delete"));
}

TEST_CASE("Select mode with nothing selected is empty", "[hints]") {
    // R / Delete do nothing without a selection, so the hint stays minimal (empty).
    REQUIRE(hintForContext(EditorMode::Select, /*hasSelection=*/false).isEmpty());
}

TEST_CASE("StampPattern hint advertises R to cycle variants, not in other modes", "[hints]") {
    const QString stamp = hintForContext(EditorMode::StampPattern, false);
    REQUIRE(stamp.contains("R"));
    REQUIRE(stamp.contains("variant"));
    REQUIRE(stamp.contains("Esc"));

    // R must NOT appear in PlaceTile (it does nothing there) — guard against a drifting list.
    REQUIRE_FALSE(hintForContext(EditorMode::PlaceTile, false).contains("variant"));
}

TEST_CASE("PlaceTile hint advertises Esc to exit placement", "[hints]") {
    REQUIRE(hintForContext(EditorMode::PlaceTile, false).contains("Esc"));
}

TEST_CASE("SetPlayerPosition hint advertises click to set the start and Esc to cancel", "[hints]") {
    const QString hint = hintForContext(EditorMode::SetPlayerPosition, false);
    REQUIRE(hint.contains("Click"));
    REQUIRE(hint.contains("Esc"));
}

TEST_CASE("Every hint uses the middot separator between multiple keys", "[hints]") {
    // MarkExits has four parts (Space, Shift, Enter/double-click, Esc), so it must contain the
    // separator exactly three times.
    const QString hint = hintForContext(EditorMode::MarkExits, false);
    REQUIRE(hint.count(QString::fromUtf8("·")) == 3);
}

TEST_CASE("PluginTool hint falls back to the host's cancel guarantee", "[hints]") {
    // With no tool hint, the generic line advertises exactly what the host enforces: an
    // unconsumed Esc / right-click always leaves the tool (EditorWidget's onEscape branch).
    const QString hint = hintForContext(EditorMode::PluginTool, false);
    REQUIRE(hint.contains("Esc"));
    REQUIRE(hint.contains("right-click"));
    REQUIRE(hint.contains("cancel"));
}

TEST_CASE("PluginTool hint renders the active tool's own items when provided", "[hints]") {
    // A registered tool supplies its keys one per line (ITool::statusHint); the provider
    // joins them with the standard middot separator. The object-placement tool's hint must
    // match what the dedicated PlaceObject mode used to show, so porting it onto the
    // registry changed no visible text.
    const QString hint = hintForContext(EditorMode::PluginTool, false,
        QStringLiteral("Click: place\nR: rotate\nEsc / right-click: cancel"));
    REQUIRE(hint.contains("Click: place"));
    REQUIRE(hint.contains("R: rotate"));
    REQUIRE(hint.contains("Esc / right-click: cancel"));
    REQUIRE(hint.count(QString::fromUtf8("·")) == 2);
}
