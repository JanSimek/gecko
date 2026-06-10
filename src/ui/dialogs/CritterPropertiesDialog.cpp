#include "CritterPropertiesDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

namespace geck {

namespace {
    QSpinBox* makeSpin(QWidget* parent, int value, int max) {
        auto* spin = new QSpinBox(parent);
        spin->setRange(0, max);
        spin->setValue(value);
        return spin;
    }
} // namespace

CritterPropertiesDialog::CritterPropertiesDialog(uint32_t aiPacket, uint32_t team, uint32_t hp,
    uint32_t radiation, uint32_t poison, QWidget* parent)
    : QDialog(parent) {

    setWindowTitle("Critter Properties");

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    _aiPacketSpin = makeSpin(this, static_cast<int>(aiPacket), 32000);
    _aiPacketSpin->setToolTip("Raw AI packet number (from the game's AI definitions).");
    formLayout->addRow("AI packet:", _aiPacketSpin);

    _teamSpin = makeSpin(this, static_cast<int>(team), 32000);
    formLayout->addRow("Team:", _teamSpin);

    _hpSpin = makeSpin(this, static_cast<int>(hp), 32000);
    formLayout->addRow("Current HP:", _hpSpin);

    _radiationSpin = makeSpin(this, static_cast<int>(radiation), 32000);
    formLayout->addRow("Radiation:", _radiationSpin);

    _poisonSpin = makeSpin(this, static_cast<int>(poison), 32000);
    formLayout->addRow("Poison:", _poisonSpin);

    mainLayout->addLayout(formLayout);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

uint32_t CritterPropertiesDialog::getAiPacket() const { return static_cast<uint32_t>(_aiPacketSpin->value()); }
uint32_t CritterPropertiesDialog::getTeam() const { return static_cast<uint32_t>(_teamSpin->value()); }
uint32_t CritterPropertiesDialog::getHp() const { return static_cast<uint32_t>(_hpSpin->value()); }
uint32_t CritterPropertiesDialog::getRadiation() const { return static_cast<uint32_t>(_radiationSpin->value()); }
uint32_t CritterPropertiesDialog::getPoison() const { return static_cast<uint32_t>(_poisonSpin->value()); }

} // namespace geck
