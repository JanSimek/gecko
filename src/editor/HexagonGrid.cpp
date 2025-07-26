#include "HexagonGrid.h"
#include "Hex.h"
#include <functional>

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
    for (size_t i = 0; i < _grid.size(); i++) {
        const auto& hex = _grid.at(i);
        if (x >= static_cast<uint32_t>(hex.x() - Hex::HEX_WIDTH) && x < static_cast<uint32_t>(hex.x() + Hex::HEX_WIDTH) && y >= static_cast<uint32_t>(hex.y() - 8) && y < static_cast<uint32_t>(hex.y() + 4)) {
            return hex.position(); // Return the hex's actual position, not the array index
        }
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
