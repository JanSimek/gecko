#pragma once

#include "ui/common/BaseDialog.h"

#include <cstdint>

class QSpinBox;
class QComboBox;

namespace geck {

class AiTxt;

/// @brief Editor for a critter instance's combat/state fields.
///
/// Edits the per-instance critter data the engine stores on the object and the
/// .map already round-trips: AI packet, team/group, current HP, radiation and
/// poison (see mp_instance.cc protoInstCritterEdit, object.cc critter data).
/// The AI packet is picked by name from the loaded ai.txt (the actual game data,
/// not an invented label table); an unknown packet — or none mounted — falls back
/// to its raw number so the value is never lost.
class CritterPropertiesDialog : public BaseDialog {
    Q_OBJECT

public:
    CritterPropertiesDialog(uint32_t aiPacket, uint32_t team, uint32_t hp,
        uint32_t radiation, uint32_t poison, const AiTxt& aiTxt, QWidget* parent = nullptr);

    uint32_t getAiPacket() const;
    uint32_t getTeam() const;
    uint32_t getHp() const;
    uint32_t getRadiation() const;
    uint32_t getPoison() const;

private:
    QComboBox* _aiPacketCombo;
    uint32_t _originalAiPacket = 0; // fallback if the editable combo is left with unparseable text
    QSpinBox* _teamSpin;
    QSpinBox* _hpSpin;
    QSpinBox* _radiationSpin;
    QSpinBox* _poisonSpin;
};

} // namespace geck
