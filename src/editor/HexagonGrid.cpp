#include "HexagonGrid.h"
#include "Hex.h"
#include <functional>
#include <limits>

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
    // Find the hex with the matching position value
    for (const auto& hex : _grid) {
        if (hex.position() == position) {
            return std::cref(hex);
        }
    }
    return std::nullopt;
}

} // namespace geck
