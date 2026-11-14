#pragma once

#include <SFML/System/Vector2.hpp>

#include <cstdint>
#include <vector>

namespace geck {

class HexagonGrid;

/// The black scenery rectangles Fallout 2 frames its maps with, so the floor's jagged outer
/// squares end on a straight screen-aligned line (132 vanilla and RPU maps: Vault 13, Vault 15,
/// Arroyo, the Hubologist stash, the caves and most random encounters).
///
/// Two pieces, both NoBlock / ShootThru / LightThru, so they only hide what is under them:
///   - BLACK003 (proto 2298), 320x148: the top and bottom of the frame, stepped exactly 320 px
///     across (hex step -10,+5);
///   - BLACK002 (proto 2297), 160x168: the sides, stepped exactly 168 px down.
/// A line here is the INNER edge of the black: the pieces sit on one side of it and their edge
/// runs along it. Horizontal lines use BLACK003, vertical lines BLACK002.
namespace black_edge {

    constexpr uint32_t COLUMN_PID = 0x020008F8; ///< BLACK002, vertical lines
    constexpr uint32_t ROW_PID = 0x020008F9;    ///< BLACK003, horizontal lines

    constexpr bool isBlackEdgePid(uint32_t pid) {
        return pid == COLUMN_PID || pid == ROW_PID;
    }

    /// A piece's frame size and shift (direction 0, frame 0): the sprite's top-left sits at
    /// hex + (-width/2 + shiftX, -height + shiftY), as Object::setHexPosition draws it.
    struct PieceShape {
        int width = 0;
        int height = 0;
        int shiftX = 0;
        int shiftY = 0;
    };

    struct Shapes {
        PieceShape row;    ///< BLACK003
        PieceShape column; ///< BLACK002
    };

    struct Placement {
        int hex = -1;
        uint32_t pid = 0;

        bool operator==(const Placement&) const = default;
    };

    /// One straight stretch of the line. `blackBefore`: the black goes on the smaller-coordinate
    /// side, i.e. above a horizontal run or left of a vertical one.
    struct Run {
        int from = -1;
        int to = -1;
        bool blackBefore = true;
    };

    /// The hex nearest a world position, or -1 off the grid.
    int snapVertex(const HexagonGrid& grid, sf::Vector2f worldPos);

    /// Where a run from `fromVertex` toward `cursor` ends: straight across or straight down the
    /// screen, whichever the cursor is further along. Returns `fromVertex` if it hasn't moved.
    int segmentEnd(const HexagonGrid& grid, int fromVertex, sf::Vector2f cursor);

    /// The side the black goes on by default: away from `center` (the middle of the map's floor).
    bool autoBlackBefore(const HexagonGrid& grid, int from, int to, sf::Vector2f center);

    /// The pieces for consecutive runs (each starts where the one before ended). Where two runs
    /// turn, the outside corner is filled too; a line that ends where it started is closed.
    std::vector<Placement> piecesForLine(const HexagonGrid& grid, const std::vector<Run>& runs, const Shapes& shapes);

} // namespace black_edge

} // namespace geck
