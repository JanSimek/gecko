#include "HexagonGrid.h"
#include "Hex.h"
#include "HexGeometry.h"
#include <algorithm>
#include <array>
#include <functional>
#include <limits>

namespace geck {

HexagonGrid::HexagonGrid() {
    // Creating 200x200 hexagonal map
    const unsigned int xMod = Hex::HEX_WIDTH / 2;  // x offset
    const unsigned int yMod = Hex::HEX_HEIGHT / 2; // y offset

    uint32_t position = 0;
    for (unsigned int hy = 0; hy != GRID_HEIGHT; ++hy) {
        for (unsigned int hx = 0; hx != GRID_WIDTH; ++hx) {
            // Calculate hex's actual screen position
            const bool oddCol = hx & 1;
            const int oddMod = hy + 1;
            const int x = (48 * (GRID_WIDTH / 2)) + (Hex::HEX_WIDTH * oddMod) - ((Hex::HEX_HEIGHT * 2) * hx) - (xMod * oddCol);
            const int y = (oddMod * Hex::HEX_HEIGHT) + (yMod * hx) + Hex::HEX_HEIGHT - (yMod * oddCol);

            _grid.emplace_back(x, y, position);
            ++position;
        }
    }
}

const std::vector<Hex>& HexagonGrid::grid() const {
    return _grid;
}

size_t HexagonGrid::size() const {
    return _grid.size();
}

bool HexagonGrid::empty() const {
    return _grid.empty();
}

bool HexagonGrid::containsPosition(int position) const {
    return position >= 0 && position < static_cast<int>(_grid.size());
}

uint32_t HexagonGrid::positionAt(uint32_t x, uint32_t y) const {
    // Find the closest hex by distance rather than using inaccurate rectangular bounds
    uint32_t closestPosition = static_cast<uint32_t>(Hex::HEX_OUT_OF_MAP);
    float minDistance = std::numeric_limits<float>::max();

    for (size_t i = 0; i < _grid.size(); i++) {
        const auto& hex = _grid.at(i);

        // Calculate squared distance to avoid expensive sqrt
        float dx = static_cast<float>(x) - static_cast<float>(hex.x());
        float dy = static_cast<float>(y) - static_cast<float>(hex.y());
        float distanceSquared = dx * dx + dy * dy;

        if (distanceSquared < minDistance) {
            minDistance = distanceSquared;
            closestPosition = hex.position();
        }
    }

    // Reject hits too far from any hex; threshold derived from hex dimensions.
    const float maxDistance = static_cast<float>(Hex::HEX_WIDTH * Hex::HEX_WIDTH + Hex::HEX_HEIGHT * Hex::HEX_HEIGHT);
    if (minDistance <= maxDistance) {
        return closestPosition;
    }

    return static_cast<uint32_t>(Hex::HEX_OUT_OF_MAP);
}

std::optional<std::reference_wrapper<const Hex>> HexagonGrid::getHexByPosition(uint32_t position) const {
    if (position < _grid.size()) {
        return std::cref(_grid.at(position));
    }

    return std::nullopt;
}

std::optional<HexagonGrid::GridCoordinates> HexagonGrid::coordinatesForPosition(int position) const {
    if (!containsPosition(position)) {
        return std::nullopt;
    }

    return GridCoordinates{
        position % GRID_WIDTH,
        position / GRID_WIDTH
    };
}

std::optional<int> HexagonGrid::positionForCoordinates(int x, int y) const {
    if (x < 0 || x >= GRID_WIDTH || y < 0 || y >= GRID_HEIGHT) {
        return std::nullopt;
    }

    return y * GRID_WIDTH + x;
}

std::optional<int> HexagonGrid::tileIndexForPosition(int position) const {
    auto coordinates = coordinatesForPosition(position);
    if (!coordinates.has_value()) {
        return std::nullopt;
    }

    const int tileX = coordinates->x / 2;
    const int tileY = coordinates->y / 2;
    return tileY * TILE_GRID_WIDTH + tileX;
}

std::vector<int> HexagonGrid::screenRectangleBorderPositions(float left, float top, float right, float bottom) const {
    const auto inside = [&](int position) {
        const Hex& hex = _grid[static_cast<size_t>(position)];
        const auto x = static_cast<float>(hex.x());
        const auto y = static_cast<float>(hex.y());
        return x >= left && x <= right && y >= top && y <= bottom;
    };

    // Neighbours are taken in cube space, which absorbs the grid's column-parity offset.
    static constexpr std::array<hexgrid::Cube, 6> DIRECTIONS{ { { 1, -1, 0 }, { 1, 0, -1 }, { 0, 1, -1 }, { -1, 1, 0 }, { -1, 0, 1 }, { 0, -1, 1 } } };

    std::vector<int> border;
    for (int position = 0; position < POSITION_COUNT; ++position) {
        if (!inside(position)) {
            continue;
        }
        const hexgrid::Cube center = hexgrid::cubeOfPosition(position);
        const bool onEdge = std::ranges::any_of(DIRECTIONS, [&](const hexgrid::Cube& direction) {
            const hexgrid::ColRow cell = hexgrid::cubeToOffset(center + direction);
            const auto neighbour = positionForCoordinates(cell.col, cell.row);
            return !neighbour.has_value() || !inside(*neighbour);
        });
        if (onEdge) {
            border.push_back(position);
        }
    }
    return border;
}

} // namespace geck
