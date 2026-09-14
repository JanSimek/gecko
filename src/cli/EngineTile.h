#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <numbers>
#include <optional>
#include <vector>

// The Fallout 2 engine's own hex-tile math, ported from fallout2-ce tile.cc and animation.cc so that
// distance, facing and line of sight come out exactly as the engine computes them.
//
// This is deliberately NOT the editor's cube-coordinate geometry (editor/HexGeometry.h, util/HexLine.h).
// The engine works in screen space: tile_dir takes an atan2 over screen offsets, tile_dist walks one
// tile_dir step at a time, and make_straight_path runs a Bresenham line over pixels and maps each pixel
// back through a 32x16 hit mask. Those can disagree with a cube lerp by a hex, and a hex decides
// whether a critter sees its target.
//
// Screen coordinates depend on the camera (tileSetCenter), and one branch of tileToScreenXY compares a
// tile's column against the camera column, so results are a function of where the view is centred, as
// they are in the engine. Callers pick the camera explicitly.
namespace geck::enginetile {

inline constexpr int GRID_WIDTH = 200;  // fallout2-ce map_defs.h HEX_GRID_WIDTH
inline constexpr int GRID_HEIGHT = 200; // HEX_GRID_HEIGHT
inline constexpr int GRID_SIZE = GRID_WIDTH * GRID_HEIGHT;
inline constexpr int ROTATION_COUNT = 6;
inline constexpr int MAX_DISTANCE = 9999; // the engine's "unreachable" distance

// _dir_tile, as tileInit fills it: the tile-number step for each rotation, by column parity.
inline constexpr int DIR_TILE[2][ROTATION_COUNT] = {
    { -1, GRID_WIDTH - 1, GRID_WIDTH, GRID_WIDTH + 1, 1, -GRID_WIDTH },
    { -GRID_WIDTH - 1, -1, GRID_WIDTH, 1, 1 - GRID_WIDTH, -GRID_WIDTH },
};

inline bool isValid(int tile) {
    return tile >= 0 && tile < GRID_SIZE;
}

// tileIsEdge
inline bool isEdge(int tile) {
    if (!isValid(tile)) {
        return false;
    }
    return tile < GRID_WIDTH || tile >= GRID_SIZE - GRID_WIDTH || tile % GRID_WIDTH == 0
        || tile % GRID_WIDTH == GRID_WIDTH - 1;
}

/// The view origin tileToScreenXY / tileFromScreenXY work against (_tile_x, _tile_y, _tile_offx,
/// _tile_offy).
struct Camera {
    int tileX = 0;
    int tileY = 0;
    int offX = 0;
    int offY = 0;
};

/// tileSetCenter's arithmetic for a view centred on `tile` (window size only shifts every screen
/// coordinate by a constant; 640x380 is the original isometric window). Assumes a valid tile.
inline Camera cameraCenteredOn(int tile, int windowWidth = 640, int windowHeight = 380) {
    Camera camera;
    const int tileX = GRID_WIDTH - 1 - tile % GRID_WIDTH;
    camera.tileY = tile / GRID_WIDTH;
    camera.offX = (windowWidth - 32) / 2;
    camera.tileX = tileX;
    camera.offY = (windowHeight - 16) / 2;
    if (tileX & 1) {
        camera.tileX -= 1;
        camera.offX -= 32;
    }
    return camera;
}

// tileToScreenXY
inline std::optional<std::array<int, 2>> tileToScreenXY(int tile, const Camera& camera) {
    if (!isValid(tile)) {
        return std::nullopt;
    }
    const int v3 = GRID_WIDTH - 1 - tile % GRID_WIDTH;
    const int v4 = tile / GRID_WIDTH;
    int screenX = camera.offX;
    int screenY = camera.offY;

    const int v5 = (v3 - camera.tileX) / -2;
    screenX += 48 * ((v3 - camera.tileX) / 2);
    screenY += 12 * v5;

    if (v3 & 1) {
        if (v3 <= camera.tileX) {
            screenX -= 16;
            screenY += 12;
        } else {
            screenX += 32;
        }
    }

    const int v6 = v4 - camera.tileY;
    screenX += 16 * v6;
    screenY += 12 * v6;
    return std::array<int, 2>{ screenX, screenY };
}

// _tile_mask, built with tileInit's loops: which neighbouring tile a pixel of the 32x16 cell belongs to.
inline const std::array<unsigned char, 512>& tileMask() {
    static const std::array<unsigned char, 512> mask = [] {
        std::array<unsigned char, 512> m{};
        int index = 0;
        int row = 0;
        do {
            int column = 64;
            do {
                m[index++] = column > row ? 1 : 0;
                column -= 4;
            } while (column);
            do {
                m[index++] = column > row ? 2 : 0;
                column += 4;
            } while (column != 64);
            row += 16;
        } while (row != 64);

        row = 0;
        do {
            int column = 0;
            do {
                m[index++] = 0;
                column++;
            } while (column < 32);
            row++;
        } while (row < 8);

        row = 0;
        do {
            int column = 0;
            do {
                m[index++] = column > row ? 0 : 3;
                column += 4;
            } while (column != 64);
            column = 64;
            do {
                m[index++] = column > row ? 0 : 4;
                column -= 4;
            } while (column);
            row += 16;
        } while (row != 64);
        return m;
    }();
    return mask;
}

// tileFromScreenXY (ignoreBounds = false): -1 when the pixel maps off the grid.
inline int tileFromScreenXY(int screenX, int screenY, const Camera& camera) {
    const int yTileOff = screenY - camera.offY;
    const int y = yTileOff >= 0 ? yTileOff / 12 : (yTileOff + 1) / 12 - 1;

    const int xTileOff = screenX - camera.offX - 16 * y;
    const int yOffset = yTileOff - y * 12;
    const int x = xTileOff >= 0 ? xTileOff / 64 : (xTileOff + 1) / 64 - 1;

    int xOffset = xTileOff - x * 64;
    const int tY = x + y;
    int tX = 2 * x;
    if (xOffset >= 32) {
        xOffset -= 32;
        tX++;
    }

    int xTile = camera.tileX + tX;
    int yTile = camera.tileY + tY;

    switch (tileMask()[static_cast<std::size_t>(32 * yOffset + xOffset)]) {
        case 1:
            yTile--;
            break;
        case 2:
            xTile++;
            if (xTile & 1) {
                yTile--;
            }
            break;
        case 3:
            xTile--;
            if (!(xTile & 1)) {
                yTile++;
            }
            break;
        case 4:
            yTile++;
            break;
        default:
            break;
    }

    const int xPos = GRID_WIDTH - 1 - xTile;
    if (xPos >= 0 && xPos < GRID_WIDTH && yTile >= 0 && yTile < GRID_HEIGHT) {
        return GRID_WIDTH * yTile + xPos;
    }
    return -1;
}

// tileGetRotationTo (tile_dir). Both tiles must be valid.
inline int rotationTo(int tile1, int tile2, const Camera& camera) {
    const auto p1 = tileToScreenXY(tile1, camera);
    const auto p2 = tileToScreenXY(tile2, camera);
    const int x1 = p1 ? (*p1)[0] : 0;
    const int y1 = p1 ? (*p1)[1] : 0;
    const int x2 = p2 ? (*p2)[0] : 0;
    const int y2 = p2 ? (*p2)[1] : 0;
    const int dy = y2 - y1;
    const int dx = x2 - x1;
    if (dx != 0) {
        const int raw = static_cast<int>(std::trunc(std::atan2(static_cast<double>(-dy), static_cast<double>(dx)) * 180.0
            / std::numbers::pi));
        int angle = 360 - (raw + 180) - 90;
        if (angle < 0) {
            angle += 360;
        }
        angle /= 60;
        if (angle >= ROTATION_COUNT) {
            angle = 5; // ROTATION_NW
        }
        return angle;
    }
    return dy < 0 ? 0 /* ROTATION_NE */ : 2 /* ROTATION_SE */;
}

// tileDistanceBetween: tile_dir steps from tile1 until tile2 is reached. The unbounded engine loop can
// run away (fallout2-ce PR #771); like that fix, stop at MAX_DISTANCE.
inline int distance(int tile1, int tile2, const Camera& camera) {
    if (!isValid(tile1) || !isValid(tile2)) {
        return MAX_DISTANCE;
    }
    int steps = 0;
    int current = tile1;
    for (; current != tile2 && steps < MAX_DISTANCE; steps++) {
        current += DIR_TILE[(current % GRID_WIDTH) & 1][rotationTo(current, tile2, camera)];
        if (!isValid(current)) {
            return MAX_DISTANCE;
        }
    }
    return steps;
}

// tileGetTileInDirection: stops early at the grid edge, as the engine does.
inline int tileInDirection(int tile, int rotation, int steps) {
    int result = tile;
    for (int i = 0; i < steps; ++i) {
        if (isEdge(result)) {
            break;
        }
        result += DIR_TILE[(result % GRID_WIDTH) & 1][rotation];
    }
    return result;
}

/// _can_see: whether a critter facing `rotation` has `targetTile` in its frontal arc (its own direction
/// or one step either side).
inline bool inFrontalArc(int sourceTile, int rotation, int targetTile, const Camera& camera) {
    int diff = rotation - rotationTo(sourceTile, targetTile, camera);
    if (diff < 0) {
        diff = -diff;
    }
    return diff == 0 || diff == 1 || diff == 5;
}

struct StraightPathResult {
    std::vector<int> tilesEntered; ///< each tile the pixel walk moved into, in order (starting tile first)
    int obstacleTile = -1;         ///< tile where the blocker was found, -1 when the walk reached `to`
    int obstacleId = -1;           ///< caller-defined id of the blocker returned by the callback
};

/// _make_straight_path_func with an obstacle pointer: walk a Bresenham line over screen pixels from
/// the centre of `from` to the centre of `to`, asking `blockerAt(tile)` for a blocker at the start
/// tile and at every tile the walk enters. `blockerAt` returns a caller-defined object id or -1.
/// `shootThrough` mirrors a6 == 32 (the sfall ObjCanSeeObj_ShootThru_Fix mode): blockers for which
/// `isShootThrough(id)` holds are then skipped. obj_can_see_obj passes 16, i.e. shootThrough = false.
inline StraightPathResult straightPath(int from, int to, const Camera& camera, const std::function<int(int)>& blockerAt,
    bool shootThrough = false, const std::function<bool(int)>& isShootThrough = {}) {
    StraightPathResult result;
    auto blocks = [&](int id) { return id >= 0 && (!shootThrough || !isShootThrough || !isShootThrough(id)); };

    result.tilesEntered.push_back(from);
    if (const int id = blockerAt(from); blocks(id)) {
        result.obstacleTile = from;
        result.obstacleId = id;
        return result;
    }

    const auto fromXY = tileToScreenXY(from, camera);
    const auto toXY = tileToScreenXY(to, camera);
    if (!fromXY || !toXY) {
        return result;
    }
    const int fromX = (*fromXY)[0] + 16;
    const int fromY = (*fromXY)[1] + 8;
    const int toX = (*toXY)[0] + 16;
    const int toY = (*toXY)[1] + 8;

    const int deltaX = toX - fromX;
    const int deltaY = toY - fromY;
    const int stepX = deltaX > 0 ? 1 : (deltaX < 0 ? -1 : 0);
    const int stepY = deltaY > 0 ? 1 : (deltaY < 0 ? -1 : 0);
    const int ddx = 2 * std::abs(deltaX);
    const int ddy = 2 * std::abs(deltaY);

    int tileX = fromX;
    int tileY = fromY;
    int prevTile = from;

    auto visit = [&](int tile) {
        if (tile == prevTile) {
            return false;
        }
        result.tilesEntered.push_back(tile);
        prevTile = tile;
        if (const int id = blockerAt(tile); blocks(id)) {
            result.obstacleTile = tile;
            result.obstacleId = id;
            return true;
        }
        return false;
    };

    if (ddx <= ddy) {
        int middle = ddx - ddy / 2;
        while (true) {
            const int tile = tileFromScreenXY(tileX, tileY, camera);
            if (tileY == toY) {
                break;
            }
            if (middle >= 0) {
                tileX += stepX;
                middle -= ddy;
            }
            tileY += stepY;
            middle += ddx;
            if (visit(tile)) {
                break;
            }
        }
    } else {
        int middle = ddy - ddx / 2;
        while (true) {
            const int tile = tileFromScreenXY(tileX, tileY, camera);
            if (tileX == toX) {
                break;
            }
            if (middle >= 0) {
                tileY += stepY;
                middle -= ddx;
            }
            tileX += stepX;
            middle += ddy;
            if (visit(tile)) {
                break;
            }
        }
    }
    return result;
}

} // namespace geck::enginetile
