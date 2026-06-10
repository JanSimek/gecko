#pragma once

#include <QDialog>

#include <cstdint>

#include "format/pro/Pro.h"

class QSpinBox;

namespace geck {

/// @brief Editor for a scenery transition destination (stairs / ladders /
/// elevators).
///
/// Stairs and ladders store their target as a packed "built tile" (hex tile in
/// the low bits, elevation in bits 29-31 - engine builtTileCreate) plus a
/// destination map id. Elevators instead store a type and a current level.
/// Matches proto.cc scenery objectDataRead/Write and mp_instance.cc
/// protoInstSceneryEdit. No .map format change - all fields already round-trip.
class SceneryDestinationDialog : public QDialog {
    Q_OBJECT

public:
    SceneryDestinationDialog(Pro::SCENERY_TYPE sceneryType,
        uint32_t elevhex, uint32_t map, uint32_t elevtype, uint32_t elevlevel,
        QWidget* parent = nullptr);

    // Returns the (possibly edited) field values. Fields that don't apply to this
    // scenery type are returned unchanged.
    uint32_t getElevhex() const;
    uint32_t getMap() const;
    uint32_t getElevtype() const;
    uint32_t getElevlevel() const;

    // Engine built-tile packing (obj_types.h): tile in the low bits, elevation in
    // bits 29-31.
    static constexpr uint32_t BUILT_TILE_TILE_MASK = 0x3FFFFFF;
    static constexpr int BUILT_TILE_ELEVATION_SHIFT = 29;
    static constexpr uint32_t BUILT_TILE_ELEVATION_MASK = 0xE0000000;
    static constexpr int MAX_HEX_TILE = 39999;

private:
    bool _isElevator;
    uint32_t _elevhex;
    uint32_t _map;
    uint32_t _elevtype;
    uint32_t _elevlevel;

    // Stairs/ladder controls
    QSpinBox* _tileSpin = nullptr;
    QSpinBox* _elevationSpin = nullptr;
    QSpinBox* _mapSpin = nullptr;

    // Elevator controls
    QSpinBox* _elevTypeSpin = nullptr;
    QSpinBox* _elevLevelSpin = nullptr;
};

} // namespace geck
