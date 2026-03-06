#include "Coordinates.h"
#include "Exceptions.h"

namespace geck {
namespace CoordinateUtils {

    HexPosition toValidHexPosition(int position) {
        if (!isValidHexPosition(position)) {
            throw InvalidArgumentException(
                "Hex position " + std::to_string(position) + " is out of range [0, " + std::to_string(HexPosition::MAX_VALUE) + "]",
                "position");
        }
        return HexPosition(position);
    }

    TileIndex toValidTileIndex(int index) {
        if (!isValidTileIndex(index)) {
            throw InvalidArgumentException(
                "Tile index " + std::to_string(index) + " is out of range [0, " + std::to_string(TileIndex::MAX_VALUE) + "]",
                "index");
        }
        return TileIndex(index);
    }

    Elevation toValidElevation(int elevation) {
        if (!isValidElevation(elevation)) {
            throw InvalidArgumentException(
                "Elevation " + std::to_string(elevation) + " is out of range [0, 2]",
                "elevation");
        }
        return static_cast<Elevation>(elevation);
    }

} // namespace CoordinateUtils
} // namespace geck