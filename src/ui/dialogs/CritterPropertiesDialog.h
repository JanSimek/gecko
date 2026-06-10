#pragma once

#include <QDialog>

#include <cstdint>

class QSpinBox;

namespace geck {

/// @brief Editor for a critter instance's combat/state fields.
///
/// Edits the per-instance critter data the engine stores on the object and the
/// .map already round-trips: AI packet, team/group, current HP, radiation and
/// poison (see mp_instance.cc protoInstCritterEdit, object.cc critter data).
/// The AI packet is shown as its raw engine number rather than a name table, to
/// avoid inventing labels that may not match the loaded game data.
class CritterPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    CritterPropertiesDialog(uint32_t aiPacket, uint32_t team, uint32_t hp,
        uint32_t radiation, uint32_t poison, QWidget* parent = nullptr);

    uint32_t getAiPacket() const;
    uint32_t getTeam() const;
    uint32_t getHp() const;
    uint32_t getRadiation() const;
    uint32_t getPoison() const;

private:
    QSpinBox* _aiPacketSpin;
    QSpinBox* _teamSpin;
    QSpinBox* _hpSpin;
    QSpinBox* _radiationSpin;
    QSpinBox* _poisonSpin;
};

} // namespace geck
