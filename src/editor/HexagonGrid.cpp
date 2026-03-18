#include "HexagonGrid.h"
#include "Hex.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <set>

namespace geck {

HexagonGrid::HexagonGrid() {
    // Creating 200x200 hexagonal map
    const unsigned int xMod = Hex::HEX_WIDTH / 2;  // x offset
    const unsigned int yMod = Hex::HEX_HEIGHT / 2; // y offset

    uint32_t position = 0;
    for (unsigned int hy = 0; hy != GRID_HEIGHT; ++hy) // rows
    {
        for (unsigned int hx = 0; hx != GRID_WIDTH; ++hx) // columns
        {
            // Calculate hex's actual position
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

        // Check if this is the closest hex so far
        if (distanceSquared < minDistance) {
            minDistance = distanceSquared;
            closestPosition = hex.position();
        }
    }

    // Only return a valid position if we're within a reasonable distance
    // Using threshold based on hex dimensions
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

std::vector<int> HexagonGrid::rectangleBorderPositions(int topLeftPosition,
    int topRightPosition,
    int bottomLeftPosition,
    int bottomRightPosition) const {
    auto topLeft = coordinatesForPosition(topLeftPosition);
    auto topRight = coordinatesForPosition(topRightPosition);
    auto bottomLeft = coordinatesForPosition(bottomLeftPosition);
    auto bottomRight = coordinatesForPosition(bottomRightPosition);
    if (!topLeft.has_value() || !topRight.has_value() || !bottomLeft.has_value() || !bottomRight.has_value()) {
        return {};
    }

    int leftX = std::min(topLeft->x, bottomLeft->x);
    int rightX = std::max(topRight->x, bottomRight->x);
    int topY = std::min(topLeft->y, topRight->y);
    int bottomY = std::max(bottomLeft->y, bottomRight->y);

    std::set<int> uniquePositions;

    for (int x = leftX; x <= rightX; ++x) {
        auto topPosition = positionForCoordinates(x, topY);
        if (topPosition.has_value()) {
            uniquePositions.insert(*topPosition);
        }

        auto bottomPosition = positionForCoordinates(x, bottomY);
        if (bottomPosition.has_value()) {
            uniquePositions.insert(*bottomPosition);
        }
    }

    for (int y = topY + 1; y < bottomY; ++y) {
        auto leftPosition = positionForCoordinates(leftX, y);
        if (leftPosition.has_value()) {
            uniquePositions.insert(*leftPosition);
        }

        auto rightPosition = positionForCoordinates(rightX, y);
        if (rightPosition.has_value()) {
            uniquePositions.insert(*rightPosition);
        }
    }

    return { uniquePositions.begin(), uniquePositions.end() };
}

} // namespace geck
