#include "Tile.h"

namespace geck {

Tile::Tile(uint16_t floor, uint16_t roof)
    : _floor(floor)
    , _roof(roof) { }

uint16_t Tile::getFloor() const {
    return _floor & 0x0FFF; // 4095 limit, see Dims mapper
}

uint16_t Tile::getRoof() const {
    return _roof & 0x0FFF; // 4095 limit, see Dims mapper; roof tile IDs on epax.map
}

void Tile::setFloor(uint16_t newFloor) {
    _floor = newFloor;
}

void Tile::setRoof(uint16_t newRoof) {
    _roof = newRoof;
}

} // namespace geck
