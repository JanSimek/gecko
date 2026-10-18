#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QString>

#include <vector>

#include "ui/core/EditorHints.h"
#include "ui/input/ActionSpec.h"
#include "editing/EditorMode.h"

using geck::EditorMode;
using geck::hintForContext;

// The hint provider is pure and headless: it maps (mode, hasSelection) to the keys that
// genuinely act in that context. These pin the per-mode keys against InputHandler so the
// status-bar hint can't silently drift from the real bindings. No GL / widgets needed.

TEST_CASE("Each mode's hint advertises the keys that act in it", "[hints]") {
    struct Row {
        EditorMode mode;
        bool hasSelection;
        std::vector<const char*> present;
        std::vector<const char*> absent;
    };
    const Row row = GENERATE(values<Row>({
        // Space flips the edge side, Shift snaps the live segment to a clean angle, Enter /
        // double-click finish the line and Esc cancels it.
        { EditorMode::MarkExits, false, { "Space", "flip", "Shift", "snap", "Esc", "finish" }, {} },
        { EditorMode::PlaceExitGrid, false, { "Esc", "place exit grid" }, {} },
        { EditorMode::Select, true, { "R", "rotate", "Delete" }, {} },
        { EditorMode::StampPattern, false, { "R", "variant", "Esc" }, {} },
        // R does nothing in PlaceTile: guard against a drifting list.
        { EditorMode::PlaceTile, false, { "Esc" }, { "variant" } },
        { EditorMode::SetPlayerPosition, false, { "Click", "Esc" }, {} },
        // With no tool hint, the generic line advertises exactly what the host enforces: an
        // unconsumed Esc / right-click always leaves the tool (EditorWidget's onEscape branch).
        { EditorMode::PluginTool, false, { "Esc", "right-click", "cancel" }, {} },
    }));

    const QString hint = hintForContext(row.mode, row.hasSelection);
    INFO("mode " << static_cast<int>(row.mode) << ": " << hint.toStdString());
    for (const char* key : row.present) {
        CHECK(hint.contains(QString::fromUtf8(key)));
    }
    for (const char* key : row.absent) {
        CHECK_FALSE(hint.contains(QString::fromUtf8(key)));
    }
}

TEST_CASE("Select mode names rebound keys and leaves unbound actions out", "[hints]") {
    const auto lookup = [](const QString& actionId) {
        if (actionId == QLatin1StringView(geck::actions::TOOL_ROTATE)) {
            return QString(); // unbound
        }
        return QStringLiteral("Ctrl+I");
    };
    const QString hint = hintForContext(EditorMode::Select, /*hasSelection=*/true, {}, lookup);
    CHECK(hint.contains("Ctrl+I: inspect"));
    CHECK_FALSE(hint.contains("rotate"));
    CHECK(hint.contains("Delete: remove"));
}

TEST_CASE("Select mode with nothing selected is empty", "[hints]") {
    // R / Delete do nothing without a selection, so the hint stays minimal (empty).
    REQUIRE(hintForContext(EditorMode::Select, /*hasSelection=*/false).isEmpty());
}

TEST_CASE("Every hint uses the middot separator between multiple keys", "[hints]") {
    // Each part is "<key>: <what>", so a hint of N parts carries N-1 separators. MarkExits has four
    // parts (Space, Shift, Enter/double-click, Esc), so it must contain the separator exactly three times.
    const QString separator = QStringLiteral("  ") + QChar(0x00B7) + QStringLiteral("  ");
    REQUIRE(hintForContext(EditorMode::MarkExits, false).count(separator) == 3);

    using enum EditorMode;
    for (const EditorMode mode : { Select, PlaceTile, PlaceExitGrid, MarkExits, SetPlayerPosition, StampPattern, PluginTool }) {
        for (const bool hasSelection : { false, true }) {
            const QString hint = hintForContext(mode, hasSelection);
            INFO("mode " << static_cast<int>(mode) << ", selection " << hasSelection << ": " << hint.toStdString());
            if (!hint.isEmpty()) {
                CHECK(hint.count(QStringLiteral(": ")) == hint.count(separator) + 1);
            }
        }
    }
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
