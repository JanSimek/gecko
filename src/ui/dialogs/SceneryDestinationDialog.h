#pragma once

#include "ui/common/BaseDialog.h"

#include <cstdint>

#include "format/pro/Pro.h"

class QSpinBox;

namespace geck {

/// @brief Editor for a scenery transition destination (stairs / ladders /
/// elevators).
///
/// Stairs and ladders store their target as a packed built tile plus a
/// destination map id; elevators store a type and a current level. Matches
/// proto.cc scenery objectDataRead/Write and mp_instance.cc protoInstSceneryEdit.
class SceneryDestinationDialog : public BaseDialog {
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
