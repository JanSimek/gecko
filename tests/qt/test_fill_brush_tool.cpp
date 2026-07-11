#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "ui/tools/FillBrushTool.h"

using namespace geck;

namespace {

// A scripted host: resolves world x to tile index floor(x/10) (y ignored), records every
// paint and the batch lifecycle so the stroke mechanics are assertable without an editor.
struct BrushHarness {
    struct Paint {
        int tileId;
        int resolvedTile;
        bool isRoof;
    };

    std::vector<Paint> paints;
    std::vector<std::string> batchLog;
    bool offMap = false;

    FillBrushTool makeTool() {
        return FillBrushTool(FillBrushTool::Host{
            .resolveTile = [this](sf::Vector2f worldPos, bool) -> std::optional<int> {
                if (offMap) {
                    return std::nullopt;
                }
                return static_cast<int>(worldPos.x) / 10;
            },
            .paintTile = [this](int tileId, sf::Vector2f worldPos, bool isRoof) { paints.push_back({ tileId, static_cast<int>(worldPos.x) / 10, isRoof }); },
            .beginStroke = [this](const std::string& description) { batchLog.push_back("begin:" + description); },
            .endStroke = [this]() { batchLog.push_back("end"); },
        });
    }

    static ToolMouseEvent at(float x, std::optional<sf::Mouse::Button> button = std::nullopt) {
        ToolMouseEvent event;
        event.worldPos = { x, 0.f };
        event.button = button;
        return event;
    }
};

} // namespace

TEST_CASE("FillBrushTool paints a deduped stroke inside one batch", "[tools][brush]") {
    BrushHarness h;
    FillBrushTool tool = h.makeTool();
    tool.setTile(271, false);

    // Press paints the first tile; the drag crosses tile 0 -> 1 -> 1 -> 2 -> back to 1.
    CHECK(tool.onMousePressed(BrushHarness::at(5.f, sf::Mouse::Button::Left)));
    CHECK(tool.strokeActive());
    CHECK(tool.onMouseMoved(BrushHarness::at(15.f)));
    CHECK(tool.onMouseMoved(BrushHarness::at(17.f))); // same tile: no repaint
    CHECK(tool.onMouseMoved(BrushHarness::at(25.f)));
    CHECK(tool.onMouseMoved(BrushHarness::at(12.f))); // back over painted ground: no repaint
    CHECK(tool.onMouseReleased(BrushHarness::at(12.f, sf::Mouse::Button::Left)));
    CHECK_FALSE(tool.strokeActive());

    REQUIRE(h.paints.size() == 3);
    CHECK(h.paints[0].resolvedTile == 0);
    CHECK(h.paints[1].resolvedTile == 1);
    CHECK(h.paints[2].resolvedTile == 2);
    CHECK(h.paints[0].tileId == 271);
    CHECK_FALSE(h.paints[0].isRoof);

    // Exactly one batch, opened before any paint and closed after the last.
    REQUIRE(h.batchLog.size() == 2);
    CHECK(h.batchLog[0] == "begin:Fill Brush");
    CHECK(h.batchLog[1] == "end");
}

TEST_CASE("FillBrushTool strokes are independent: a new stroke repaints old ground", "[tools][brush]") {
    BrushHarness h;
    FillBrushTool tool = h.makeTool();
    tool.setTile(300, true);

    CHECK(tool.onMousePressed(BrushHarness::at(5.f, sf::Mouse::Button::Left)));
    CHECK(tool.onMouseReleased(BrushHarness::at(5.f, sf::Mouse::Button::Left)));
    CHECK(tool.onMousePressed(BrushHarness::at(5.f, sf::Mouse::Button::Left)));
    CHECK(tool.onMouseReleased(BrushHarness::at(5.f, sf::Mouse::Button::Left)));

    REQUIRE(h.paints.size() == 2); // same tile painted once per stroke
    CHECK(h.paints[0].isRoof);
    REQUIRE(h.batchLog.size() == 4);
    CHECK(h.batchLog == std::vector<std::string>{ "begin:Fill Brush", "end", "begin:Fill Brush", "end" });
}

TEST_CASE("FillBrushTool deactivation flushes an open stroke exactly once", "[tools][brush]") {
    BrushHarness h;
    FillBrushTool tool = h.makeTool();
    tool.setTile(271, false);

    // Esc mid-stroke: the host's cancel guarantee deactivates the tool without a release
    // ever arriving; the open batch must flush (one undo entry), and only once.
    CHECK(tool.onMousePressed(BrushHarness::at(5.f, sf::Mouse::Button::Left)));
    tool.onDeactivate();
    CHECK_FALSE(tool.strokeActive());
    tool.onDeactivate(); // idempotent: a second deactivation must not double-end
    REQUIRE(h.batchLog.size() == 2);
    CHECK(h.batchLog[1] == "end");

    // A release arriving after the flush is not consumed (no stroke to end).
    CHECK_FALSE(tool.onMouseReleased(BrushHarness::at(5.f, sf::Mouse::Button::Left)));
    REQUIRE(h.batchLog.size() == 2);
}

TEST_CASE("FillBrushTool leaves right-click and Escape to the host's cancel path", "[tools][brush]") {
    BrushHarness h;
    FillBrushTool tool = h.makeTool();
    tool.setTile(271, false);

    // Unconsumed right press/Escape = the host exits the tool; the brush adds no bespoke cancel.
    CHECK_FALSE(tool.onMousePressed(BrushHarness::at(5.f, sf::Mouse::Button::Right)));
    CHECK_FALSE(tool.onKeyPressed(ToolKeyEvent{ .code = sf::Keyboard::Key::Escape, .modifiers = {} }));
    CHECK(h.paints.empty());
    CHECK(h.batchLog.empty());
}

TEST_CASE("FillBrushTool without a loaded tile paints nothing and opens no batch", "[tools][brush]") {
    BrushHarness h;
    FillBrushTool tool = h.makeTool();

    CHECK_FALSE(tool.onMousePressed(BrushHarness::at(5.f, sf::Mouse::Button::Left)));
    CHECK(h.paints.empty());
    CHECK(h.batchLog.empty());
    CHECK(tool.buildPreview(BrushHarness::at(5.f)).empty());
}

TEST_CASE("FillBrushTool skips off-map positions but keeps the stroke alive", "[tools][brush]") {
    BrushHarness h;
    FillBrushTool tool = h.makeTool();
    tool.setTile(271, false);

    CHECK(tool.onMousePressed(BrushHarness::at(5.f, sf::Mouse::Button::Left)));
    h.offMap = true;
    CHECK(tool.onMouseMoved(BrushHarness::at(15.f))); // off-map: skipped, stroke continues
    h.offMap = false;
    CHECK(tool.onMouseMoved(BrushHarness::at(25.f)));
    CHECK(tool.onMouseReleased(BrushHarness::at(25.f, sf::Mouse::Button::Left)));

    REQUIRE(h.paints.size() == 2);
    CHECK(h.paints[0].resolvedTile == 0);
    CHECK(h.paints[1].resolvedTile == 2);
}

TEST_CASE("FillBrushTool ghosts the hovered tile on the loaded layer", "[tools][brush]") {
    BrushHarness h;
    FillBrushTool tool = h.makeTool();

    tool.setTile(271, false);
    ToolPreviewSpec floor = tool.buildPreview(BrushHarness::at(25.f));
    REQUIRE(floor.floorTiles.size() == 1);
    CHECK(floor.roofTiles.empty());
    CHECK(floor.floorTiles[0].tileIndex == 2);
    CHECK(floor.floorTiles[0].tileId == 271);

    tool.setTile(300, true);
    ToolPreviewSpec roof = tool.buildPreview(BrushHarness::at(25.f));
    CHECK(roof.floorTiles.empty());
    REQUIRE(roof.roofTiles.size() == 1);
    CHECK(roof.roofTiles[0].tileId == 300);

    h.offMap = true;
    CHECK(tool.buildPreview(BrushHarness::at(25.f)).empty());
}

TEST_CASE("FillBrushTool advertises its keys via statusHint", "[tools][brush]") {
    BrushHarness h;
    FillBrushTool tool = h.makeTool();
    CHECK_FALSE(tool.statusHint().empty());
    CHECK(std::string(tool.statusHint()).find("Esc") != std::string::npos);
}
