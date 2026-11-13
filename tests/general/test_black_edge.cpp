#include <catch2/catch_test_macros.hpp>

#include <SFML/Graphics/Rect.hpp>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "editor/HexagonGrid.h"
#include "editor/helper/BlackEdge.h"
#include "editing/tools/BlackEdgeTool.h"

using namespace geck;
using namespace geck::black_edge;

namespace {

// The vanilla art, read from BLACK003.frm / BLACK002.frm (frame size, direction-0 shift).
const Shapes VANILLA{
    .row = { .width = 320, .height = 148, .shiftX = -96, .shiftY = -72 },
    .column = { .width = 160, .height = 168, .shiftX = -92, .shiftY = -48 },
};

int hexAt(int x, int y) {
    return y * HexagonGrid::GRID_WIDTH + x;
}

sf::Vector2i worldOf(const HexagonGrid& grid, int hex) {
    const Hex& h = grid.getHexByPosition(static_cast<uint32_t>(hex))->get();
    return { h.x(), h.y() };
}

sf::Vector2f worldF(const HexagonGrid& grid, int hex) {
    const auto w = worldOf(grid, hex);
    return { static_cast<float>(w.x), static_cast<float>(w.y) };
}

// Where a placed piece draws, the way Object::setHexPosition puts it.
sf::IntRect rectOf(const HexagonGrid& grid, const Placement& piece) {
    const PieceShape& shape = piece.pid == ROW_PID ? VANILLA.row : VANILLA.column;
    const auto hex = worldOf(grid, piece.hex);
    return { { hex.x - shape.width / 2 + shape.shiftX, hex.y - shape.height + shape.shiftY }, { shape.width, shape.height } };
}

bool covered(const HexagonGrid& grid, const std::vector<Placement>& pieces, sf::Vector2i point) {
    return std::ranges::any_of(pieces, [&](const Placement& piece) { return rectOf(grid, piece).contains(point); });
}

} // namespace

TEST_CASE("A horizontal line on HUBSTASH's top edge places vanilla's own pieces", "[black_edge]") {
    // HUBSTASH.MAP elevation 2 runs BLACK003 across hexes (118,80) (108,85) (98,90) (88,95) (78,100):
    // their bottom edge is y=1620 and they span x 3008..4608 in gecko's world coordinates.
    HexagonGrid grid;
    const int left = hexAt(123, 72);
    const int right = hexAt(73, 97);
    REQUIRE(worldOf(grid, left) == sf::Vector2i(3008, 1620));
    REQUIRE(worldOf(grid, right) == sf::Vector2i(4608, 1620));

    const auto pieces = piecesForLine(grid, { { left, right, true } }, VANILLA);
    const std::vector<Placement> vanilla{
        { hexAt(118, 80), ROW_PID },
        { hexAt(108, 85), ROW_PID },
        { hexAt(98, 90), ROW_PID },
        { hexAt(88, 95), ROW_PID },
        { hexAt(78, 100), ROW_PID },
    };
    CHECK(pieces == vanilla);
    // Drawn right to left it is the same row.
    auto reversed = piecesForLine(grid, { { right, left, true } }, VANILLA);
    std::ranges::sort(reversed, {}, &Placement::hex);
    auto expected = vanilla;
    std::ranges::sort(expected, {}, &Placement::hex);
    CHECK(reversed == expected);
}

TEST_CASE("A run's black sits on the chosen side with its edge on the line and no gaps", "[black_edge]") {
    HexagonGrid grid;
    const int left = hexAt(123, 72);
    const int right = hexAt(73, 97);
    const int lineY = 1620;

    for (const bool before : { true, false }) {
        const auto pieces = piecesForLine(grid, { { left, right, before } }, VANILLA);
        REQUIRE(pieces.size() >= 2);
        for (const auto& piece : pieces) {
            const auto r = rectOf(grid, piece);
            // Above: bottom edge on the line. Below: top edge on the line (to the 12 px hex row).
            const int edge = before ? r.position.y + r.size.y : r.position.y;
            CHECK(std::abs(edge - lineY) <= 6);
        }
        auto rects = pieces;
        std::ranges::sort(rects, {}, [&](const Placement& p) { return rectOf(grid, p).position.x; });
        CHECK(rectOf(grid, rects.front()).position.x <= 3008);
        CHECK(rectOf(grid, rects.back()).position.x + 320 >= 4608);
        for (std::size_t i = 1; i < rects.size(); ++i) {
            CHECK(rectOf(grid, rects[i]).position.x <= rectOf(grid, rects[i - 1]).position.x + 320);
        }
    }
}

TEST_CASE("A vertical line stacks BLACK002 168 px apart beside it", "[black_edge]") {
    HexagonGrid grid;
    const int top = hexAt(123, 72);
    const int bottom = segmentEnd(grid, top, worldF(grid, top) + sf::Vector2f(0.f, 900.f));
    REQUIRE(worldOf(grid, bottom).x == worldOf(grid, top).x);
    const int lineX = worldOf(grid, top).x;

    const auto pieces = piecesForLine(grid, { { top, bottom, true } }, VANILLA);
    REQUIRE(pieces.size() >= 5);
    std::vector<sf::IntRect> rects;
    for (const auto& piece : pieces) {
        CHECK(piece.pid == COLUMN_PID);
        rects.push_back(rectOf(grid, piece));
        CHECK(std::abs(rects.back().position.x + rects.back().size.x - lineX) <= 4); // right edge on the line
    }
    std::ranges::sort(rects, {}, [](const sf::IntRect& r) { return r.position.y; });
    for (std::size_t i = 1; i + 1 < rects.size(); ++i) {
        CHECK(rects[i].position.y - rects[i - 1].position.y == 168);
        CHECK(rects[i].position.x == rects[0].position.x);
    }
    CHECK(rects.front().position.y <= worldOf(grid, top).y);
    CHECK(rects.back().position.y + 168 >= worldOf(grid, bottom).y);
}

TEST_CASE("segmentEnd runs straight across or straight down the screen", "[black_edge]") {
    HexagonGrid grid;
    const int from = hexAt(123, 72);
    const auto origin = worldF(grid, from);

    const int across = segmentEnd(grid, from, origin + sf::Vector2f(700.f, 40.f));
    CHECK(worldOf(grid, across).y == worldOf(grid, from).y);
    CHECK(std::abs(worldOf(grid, across).x - (worldOf(grid, from).x + 700)) <= 16);

    const int down = segmentEnd(grid, from, origin + sf::Vector2f(-30.f, 500.f));
    CHECK(worldOf(grid, down).x == worldOf(grid, from).x);
    CHECK(std::abs(worldOf(grid, down).y - (worldOf(grid, from).y + 500)) <= 12);

    CHECK(segmentEnd(grid, from, origin) == from);
}

TEST_CASE("A closed frame fills its outside corners", "[black_edge]") {
    HexagonGrid grid;
    const int topLeft = hexAt(123, 72);
    const int topRight = segmentEnd(grid, topLeft, worldF(grid, topLeft) + sf::Vector2f(1600.f, 0.f));
    const int bottomRight = segmentEnd(grid, topRight, worldF(grid, topRight) + sf::Vector2f(0.f, 840.f));
    const int bottomLeft = segmentEnd(grid, bottomRight, worldF(grid, bottomRight) + sf::Vector2f(-1600.f, 0.f));
    const int back = segmentEnd(grid, bottomLeft, worldF(grid, topLeft));
    const sf::Vector2f center = (worldF(grid, topLeft) + worldF(grid, bottomRight)) / 2.f;

    std::vector<Run> runs;
    for (const auto& [a, b] : std::vector<std::pair<int, int>>{
             { topLeft, topRight }, { topRight, bottomRight }, { bottomRight, bottomLeft }, { bottomLeft, back } }) {
        runs.push_back({ a, b, autoBlackBefore(grid, a, b, center) });
    }
    CHECK(runs[0].blackBefore);  // top: black above
    CHECK_FALSE(runs[1].blackBefore); // right: black to the right
    CHECK_FALSE(runs[2].blackBefore); // bottom: black below
    CHECK(runs[3].blackBefore);  // left: black to the left

    const auto pieces = piecesForLine(grid, runs, VANILLA);
    const auto tl = worldOf(grid, topLeft);
    const auto br = worldOf(grid, bottomRight);
    // Just outside each corner, diagonally.
    CHECK(covered(grid, pieces, { tl.x - 40, tl.y - 40 }));
    CHECK(covered(grid, pieces, { br.x + 40, tl.y - 40 }));
    CHECK(covered(grid, pieces, { br.x + 40, br.y + 40 }));
    CHECK(covered(grid, pieces, { tl.x - 40, br.y + 40 }));
    // And never inside the frame.
    CHECK_FALSE(covered(grid, pieces, { (tl.x + br.x) / 2, (tl.y + br.y) / 2 }));
    CHECK_FALSE(covered(grid, pieces, { tl.x + 60, tl.y + 60 }));
    CHECK_FALSE(covered(grid, pieces, { br.x - 60, br.y - 60 }));
}

TEST_CASE("BlackEdgeTool places one line per finish and leaves the host alone otherwise", "[black_edge][tool]") {
    HexagonGrid grid;
    std::vector<std::vector<Placement>> placed;
    BlackEdgeTool tool(BlackEdgeTool::Host{
        .grid = &grid,
        .shapes = []() -> std::optional<Shapes> { return VANILLA; },
        .floorCenter = [&grid]() { return worldF(grid, hexAt(98, 110)); }, // below the line
        .frmPidFor = [](uint32_t pid) { return pid; },
        .placePieces = [&placed](const std::vector<Placement>& pieces) {
            placed.push_back(pieces);
            return static_cast<int>(pieces.size());
        },
    });
    const auto click = [&](sf::Vector2f world, sf::Mouse::Button button = sf::Mouse::Button::Left) {
        ToolMouseEvent event;
        event.worldPos = world;
        event.button = button;
        return tool.onMousePressed(event);
    };
    const auto left = worldF(grid, hexAt(123, 72));
    const auto right = worldF(grid, hexAt(73, 97));

    CHECK_FALSE(click(left, sf::Mouse::Button::Right)); // nothing drawn: the host leaves the tool

    CHECK(click(left));
    CHECK(tool.hasStart());
    ToolMouseEvent hover;
    hover.worldPos = right;
    CHECK(tool.buildPreview(hover).objects.size() == 5);

    CHECK(click(right));
    REQUIRE(tool.runs().size() == 1);
    CHECK(tool.runs()[0].blackBefore); // away from the floor below

    // Backspace drops the run, and is always consumed.
    CHECK(tool.onKeyPressed({ .code = sf::Keyboard::Key::Backspace }));
    CHECK(tool.runs().empty());
    // Space flips the side of the next run.
    CHECK(tool.onKeyPressed({ .code = sf::Keyboard::Key::Space }));
    CHECK(click(right));
    CHECK_FALSE(tool.runs()[0].blackBefore);

    CHECK(tool.onKeyPressed({ .code = sf::Keyboard::Key::Enter }));
    REQUIRE(placed.size() == 1);
    CHECK(placed[0].size() == 5);
    CHECK_FALSE(tool.hasStart());

    // Esc drops an unfinished line without placing it.
    CHECK(click(left));
    CHECK(tool.onKeyPressed({ .code = sf::Keyboard::Key::Escape }));
    CHECK_FALSE(tool.hasStart());
    CHECK_FALSE(tool.onKeyPressed({ .code = sf::Keyboard::Key::Escape }));
    CHECK(placed.size() == 1);
}
