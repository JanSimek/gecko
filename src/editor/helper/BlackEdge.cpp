#include "BlackEdge.h"

#include "editor/HexagonGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <utility>

namespace geck::black_edge {

namespace {

    constexpr int SEARCH_RADIUS = 8;   // hexes; same-row neighbours are 32 px apart, same-column 24 px
    constexpr int MAX_PIECES_PER_RUN = 400;

    std::optional<sf::Vector2i> worldOf(const HexagonGrid& grid, int hex) {
        if (hex < 0) {
            return std::nullopt;
        }
        const auto found = grid.getHexByPosition(static_cast<uint32_t>(hex));
        if (!found) {
            return std::nullopt;
        }
        return sf::Vector2i(found->get().x(), found->get().y());
    }

    // The hex nearest a world point, also when the point is off the drawn grid.
    int nearestHex(const HexagonGrid& grid, sf::Vector2i point) {
        if (point.x >= 0 && point.y >= 0) {
            const uint32_t hit = grid.positionAt(static_cast<uint32_t>(point.x), static_cast<uint32_t>(point.y));
            if (hit != Hex::HEX_OUT_OF_MAP) {
                return static_cast<int>(hit);
            }
        }
        int best = -1;
        long long bestDistance = std::numeric_limits<long long>::max();
        for (const Hex& hex : grid.grid()) {
            const long long dx = hex.x() - point.x;
            const long long dy = hex.y() - point.y;
            if (dx * dx + dy * dy < bestDistance) {
                bestDistance = dx * dx + dy * dy;
                best = static_cast<int>(hex.position());
            }
        }
        return best;
    }

    // Among the hexes whose world coordinate on the fixed axis equals `fixed`, the one whose
    // coordinate along the line is the largest <= `along` (floor) or the smallest >= it (ceiling).
    // Horizontal lines fix y and run along x; vertical lines the other way round.
    std::optional<int> hexOnLine(const HexagonGrid& grid, bool horizontal, int fixed, int along, bool ceiling) {
        const sf::Vector2i probe = horizontal ? sf::Vector2i(along, fixed) : sf::Vector2i(fixed, along);
        const int start = nearestHex(grid, probe);
        if (start < 0) {
            return std::nullopt;
        }
        const int sx = start % HexagonGrid::GRID_WIDTH;
        const int sy = start / HexagonGrid::GRID_WIDTH;
        std::optional<int> best;
        int bestAlong = 0;
        for (int y = sy - SEARCH_RADIUS; y <= sy + SEARCH_RADIUS; ++y) {
            for (int x = sx - SEARCH_RADIUS; x <= sx + SEARCH_RADIUS; ++x) {
                const auto hex = grid.positionForCoordinates(x, y);
                if (!hex) {
                    continue;
                }
                const auto world = worldOf(grid, *hex);
                const int fixedHere = horizontal ? world->y : world->x;
                const int alongHere = horizontal ? world->x : world->y;
                if (fixedHere != fixed || (ceiling ? alongHere < along : alongHere > along)) {
                    continue;
                }
                if (!best || (ceiling ? alongHere < bestAlong : alongHere > bestAlong)) {
                    best = *hex;
                    bestAlong = alongHere;
                }
            }
        }
        return best;
    }

    // The hex row (horizontal) or hex column (vertical) nearest `wanted`, near `along`: rows are
    // 12 px apart, but a row's hexes are 32 px apart, so the plain nearest hex can sit one row off.
    std::optional<int> closestFixed(const HexagonGrid& grid, bool horizontal, int wanted, int along) {
        const int start = nearestHex(grid, horizontal ? sf::Vector2i(along, wanted) : sf::Vector2i(wanted, along));
        if (start < 0) {
            return std::nullopt;
        }
        const int sx = start % HexagonGrid::GRID_WIDTH;
        const int sy = start / HexagonGrid::GRID_WIDTH;
        std::optional<int> best;
        for (int y = sy - SEARCH_RADIUS; y <= sy + SEARCH_RADIUS; ++y) {
            for (int x = sx - SEARCH_RADIUS; x <= sx + SEARCH_RADIUS; ++x) {
                const auto hex = grid.positionForCoordinates(x, y);
                if (!hex) {
                    continue;
                }
                const auto world = worldOf(grid, *hex);
                const int fixedHere = horizontal ? world->y : world->x;
                if (!best || std::abs(fixedHere - wanted) < std::abs(*best - wanted)) {
                    best = fixedHere;
                }
            }
        }
        return best;
    }

    struct Line {
        bool horizontal = true;
        int fixed = 0; // y of a horizontal line, x of a vertical one
        int lo = 0;    // extent along the line
        int hi = 0;
        bool blackBefore = true;
    };

    // Covers [lo, hi] of one line with pieces whose inner edge is the line. The first piece starts
    // at or before lo; each next one starts at or before the previous one's end, so nothing is
    // left open; the last is pulled back to end at or just past hi.
    void coverLine(const HexagonGrid& grid, const Line& line, const PieceShape& shape, uint32_t pid,
        std::vector<Placement>& out) {
        // Sprite top-left = hex + (-w/2 + shiftX, -h + shiftY), so the hex coordinate that puts a
        // piece's edge where we want it is that edge minus this offset.
        const int offsetX = -shape.width / 2 + shape.shiftX;
        const int offsetY = -shape.height + shape.shiftY;
        const int length = line.horizontal ? shape.width : shape.height;
        const int thickness = line.horizontal ? shape.height : shape.width;
        const int offsetAlong = line.horizontal ? offsetX : offsetY;
        const int offsetAcross = line.horizontal ? offsetY : offsetX;
        if (length <= 0) {
            return;
        }

        // The across coordinate the pieces' hexes must share: the black's near edge on the line.
        const int edge = line.blackBefore ? line.fixed - thickness : line.fixed;
        const int wanted = edge - offsetAcross;
        const auto fixedOrNot = closestFixed(grid, line.horizontal, wanted, line.lo - offsetAlong);
        if (!fixedOrNot) {
            return;
        }
        const int fixed = *fixedOrNot;

        auto start = hexOnLine(grid, line.horizontal, fixed, line.lo - offsetAlong, false);
        if (!start) {
            start = hexOnLine(grid, line.horizontal, fixed, line.lo - offsetAlong, true);
        }
        if (!start) {
            return;
        }
        int startAlong = line.horizontal ? worldOf(grid, *start)->x : worldOf(grid, *start)->y;
        out.push_back({ *start, pid });
        int end = startAlong + offsetAlong + length;

        for (int count = 1; end < line.hi && count < MAX_PIECES_PER_RUN; ++count) {
            // Prefer the last piece flush with the line's end, if it still meets the one before.
            const auto last = hexOnLine(grid, line.horizontal, fixed, line.hi - length - offsetAlong, true);
            if (last) {
                const int lastStart = (line.horizontal ? worldOf(grid, *last)->x : worldOf(grid, *last)->y) + offsetAlong;
                if (lastStart <= end) {
                    out.push_back({ *last, pid });
                    break;
                }
            }
            const auto next = hexOnLine(grid, line.horizontal, fixed, end - offsetAlong, false);
            if (!next) {
                break;
            }
            const int nextAlong = line.horizontal ? worldOf(grid, *next)->x : worldOf(grid, *next)->y;
            if (nextAlong <= startAlong) {
                break; // no progress: the grid ends here
            }
            out.push_back({ *next, pid });
            startAlong = nextAlong;
            end = startAlong + offsetAlong + length;
        }
    }

    // +1 / -1: which way the black of a run lies across its line, in that line's across axis.
    int blackDirection(const Run& run) {
        return run.blackBefore ? -1 : 1;
    }

} // namespace

int snapVertex(const HexagonGrid& grid, sf::Vector2f worldPos) {
    if (worldPos.x < 0.f || worldPos.y < 0.f) {
        return -1;
    }
    const uint32_t hit = grid.positionAt(static_cast<uint32_t>(worldPos.x), static_cast<uint32_t>(worldPos.y));
    return hit == Hex::HEX_OUT_OF_MAP ? -1 : static_cast<int>(hit);
}

int segmentEnd(const HexagonGrid& grid, int fromVertex, sf::Vector2f cursor) {
    const auto from = worldOf(grid, fromVertex);
    if (!from) {
        return fromVertex;
    }
    const float dx = cursor.x - static_cast<float>(from->x);
    const float dy = cursor.y - static_cast<float>(from->y);
    const bool horizontal = std::abs(dx) >= std::abs(dy);
    const int along = static_cast<int>(std::lround(horizontal ? cursor.x : cursor.y));
    const int fixed = horizontal ? from->y : from->x;
    // The nearer of the hexes on either side of the cursor on this line.
    const auto below = hexOnLine(grid, horizontal, fixed, along, false);
    const auto above = hexOnLine(grid, horizontal, fixed, along, true);
    const auto alongOf = [&](int hex) { return horizontal ? worldOf(grid, hex)->x : worldOf(grid, hex)->y; };
    if (below && above) {
        return std::abs(alongOf(*below) - along) <= std::abs(alongOf(*above) - along) ? *below : *above;
    }
    if (below) {
        return *below;
    }
    return above ? *above : fromVertex;
}

bool autoBlackBefore(const HexagonGrid& grid, int from, int to, sf::Vector2f center) {
    const auto a = worldOf(grid, from);
    const auto b = worldOf(grid, to);
    if (!a || !b) {
        return true;
    }
    if (a->y == b->y) {
        return static_cast<float>(a->y) < center.y; // above the middle: black goes up
    }
    return static_cast<float>(a->x) < center.x; // left of the middle: black goes left
}

std::vector<Placement> piecesForLine(const HexagonGrid& grid, const std::vector<Run>& runs, const Shapes& shapes) {
    std::vector<Placement> pieces;
    const bool closed = runs.size() >= 4 && runs.front().from == runs.back().to;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const Run& run = runs[i];
        const auto a = worldOf(grid, run.from);
        const auto b = worldOf(grid, run.to);
        if (!a || !b || run.from == run.to) {
            continue;
        }
        Line line;
        line.horizontal = a->y == b->y;
        if (!line.horizontal && a->x != b->x) {
            continue; // not a straight run
        }
        line.fixed = line.horizontal ? a->y : a->x;
        const int from = line.horizontal ? a->x : a->y;
        const int to = line.horizontal ? b->x : b->y;
        const int direction = to > from ? 1 : -1;
        int startExtra = 0;
        int endExtra = 0;

        // At a turn, reach past the corner by the other run's thickness when the other run's
        // black lies beyond the corner, so the outside corner square is black too.
        const PieceShape& other = line.horizontal ? shapes.column : shapes.row;
        const int otherThickness = line.horizontal ? other.width : other.height;
        const auto turnsInto = [&](const Run* neighbour) {
            if (!neighbour) {
                return false;
            }
            const auto na = worldOf(grid, neighbour->from);
            const auto nb = worldOf(grid, neighbour->to);
            return na && nb && (na->y == nb->y) != line.horizontal;
        };
        const Run* before = i > 0 ? &runs[i - 1] : (closed ? &runs.back() : nullptr);
        const Run* after = i + 1 < runs.size() ? &runs[i + 1] : (closed ? &runs.front() : nullptr);
        if (turnsInto(after) && blackDirection(*after) == direction) {
            endExtra = otherThickness;
        }
        if (turnsInto(before) && blackDirection(*before) == -direction) {
            startExtra = otherThickness;
        }
        const int startAt = from - direction * startExtra;
        const int endAt = to + direction * endExtra;
        line.lo = std::min(startAt, endAt);
        line.hi = std::max(startAt, endAt);
        line.blackBefore = run.blackBefore;
        coverLine(grid, line, line.horizontal ? shapes.row : shapes.column, line.horizontal ? ROW_PID : COLUMN_PID, pieces);
    }

    // Two runs can both cover a corner; keep one piece per hex and pid.
    std::vector<Placement> unique;
    std::set<std::pair<int, uint32_t>> seen;
    for (const auto& piece : pieces) {
        if (seen.insert({ piece.hex, piece.pid }).second) {
            unique.push_back(piece);
        }
    }
    return unique;
}

} // namespace geck::black_edge
