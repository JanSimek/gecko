#include "CritterPropertiesDialog.h"

#include "format/ai/AiPacket.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

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
    uint32_t radiation, uint32_t poison, const AiTxt& aiTxt, QWidget* parent)
    : BaseDialog("Critter Properties", parent) {

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    // AI packet picker: every packet ai.txt defines, by name, ordered by packet number, with the packet
    // number as the item data. If the critter's current packet isn't in ai.txt (or none is mounted), add
    // a raw entry for it so the value round-trips unchanged. The combo is editable so a packet number can
    // still be typed directly (e.g. with no ai.txt mounted, or when authoring against another build).
    _originalAiPacket = aiPacket;
    _aiPacketCombo = new QComboBox(this);
    _aiPacketCombo->setEditable(true);
    _aiPacketCombo->setInsertPolicy(QComboBox::NoInsert);
    _aiPacketCombo->setToolTip("AI behaviour packet: pick by name (from the game's ai.txt) or type a packet number.");

    std::vector<const AiPacket*> packets;
    packets.reserve(aiTxt.packets().size());
    for (const auto& [num, packet] : aiTxt.packets()) {
        packets.push_back(&packet);
    }
    std::sort(packets.begin(), packets.end(),
        [](const AiPacket* a, const AiPacket* b) { return a->packetNum < b->packetNum; });
    for (const AiPacket* packet : packets) {
        _aiPacketCombo->addItem(
            QString("%1 (%2)").arg(QString::fromStdString(packet->name)).arg(packet->packetNum),
            packet->packetNum);
    }

    int currentIndex = _aiPacketCombo->findData(static_cast<int>(aiPacket));
    if (currentIndex < 0) {
        _aiPacketCombo->addItem(QString("Packet %1 (not in ai.txt)").arg(aiPacket), static_cast<int>(aiPacket));
        currentIndex = _aiPacketCombo->count() - 1;
    }
    _aiPacketCombo->setCurrentIndex(currentIndex);
    formLayout->addRow("AI packet:", _aiPacketCombo);

    auto* aiPacketHint = new QLabel("Pick a packet by name, or type a number.", this);
    aiPacketHint->setStyleSheet(ui::theme::styles::smallLabel());
    formLayout->addRow("", aiPacketHint);

    _teamSpin = makeSpin(this, static_cast<int>(team), 32000);
    formLayout->addRow("Team:", _teamSpin);

    _hpSpin = makeSpin(this, static_cast<int>(hp), 32000);
    formLayout->addRow("Current HP:", _hpSpin);

    _radiationSpin = makeSpin(this, static_cast<int>(radiation), 32000);
    formLayout->addRow("Radiation:", _radiationSpin);

    _poisonSpin = makeSpin(this, static_cast<int>(poison), 32000);
    formLayout->addRow("Poison:", _poisonSpin);

    mainLayout->addLayout(formLayout);

    mainLayout->addWidget(createButtonBox());
}

uint32_t CritterPropertiesDialog::getAiPacket() const {
    // A number typed into the editable combo wins; otherwise use the picked item's packet number (its
    // text is "name (num)", which isn't a bare number so it falls through here); empty/garbage keeps the
    // original value so it's never lost.
    bool ok = false;
    const long long typed = _aiPacketCombo->currentText().toLongLong(&ok);
    if (ok && typed >= 0) {
        return static_cast<uint32_t>(typed);
    }
    const QVariant itemData = _aiPacketCombo->currentData();
    return itemData.isValid() ? static_cast<uint32_t>(itemData.toInt()) : _originalAiPacket;
}
uint32_t CritterPropertiesDialog::getTeam() const { return static_cast<uint32_t>(_teamSpin->value()); }
uint32_t CritterPropertiesDialog::getHp() const { return static_cast<uint32_t>(_hpSpin->value()); }
uint32_t CritterPropertiesDialog::getRadiation() const { return static_cast<uint32_t>(_radiationSpin->value()); }
uint32_t CritterPropertiesDialog::getPoison() const { return static_cast<uint32_t>(_poisonSpin->value()); }

} // namespace geck
