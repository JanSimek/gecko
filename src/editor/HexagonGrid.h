#pragma once

#include "Hex.h"

#include <memory>
#include <vector>
#include <optional>
#include <functional>

namespace geck {

class HexagonGrid {
private:
    std::vector<Hex> _grid;

public:
    static constexpr int GRID_WIDTH = 200;
    static constexpr int GRID_HEIGHT = 200;
    static constexpr int POSITION_COUNT = GRID_WIDTH * GRID_HEIGHT;
    static constexpr int TILE_GRID_WIDTH = GRID_WIDTH / 2;
    static constexpr int TILE_GRID_HEIGHT = GRID_HEIGHT / 2;
    static constexpr int TILE_COUNT = TILE_GRID_WIDTH * TILE_GRID_HEIGHT;

    struct GridCoordinates {
        int x;
        int y;
    };

    HexagonGrid();

    const std::vector<Hex>& grid() const;
    size_t size() const;
    bool empty() const;
    bool containsPosition(int position) const;

    uint32_t positionAt(uint32_t x, uint32_t y) const;

    std::optional<std::reference_wrapper<const Hex>> getHexByPosition(uint32_t position) const;
    std::optional<GridCoordinates> coordinatesForPosition(int position) const;
    std::optional<int> positionForCoordinates(int x, int y) const;
    std::optional<int> tileIndexForPosition(int position) const;
    std::vector<int> rectangleBorderPositions(int topLeftPosition,
        int topRightPosition,
        int bottomLeftPosition,
        int bottomRightPosition) const;
};

} // namespace geck
