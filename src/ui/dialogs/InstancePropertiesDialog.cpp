#include "InstancePropertiesDialog.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QVBoxLayout>

#include "format/pro/Pro.h"

namespace geck {

namespace {
    constexpr uint32_t FLAG_LOCKED = static_cast<uint32_t>(Pro::InteractionFlags::LOCKED);
    constexpr uint32_t FLAG_JAMMED = static_cast<uint32_t>(Pro::InteractionFlags::JAMMED);
} // namespace

InstancePropertiesDialog::InstancePropertiesDialog(bool isDoor, uint32_t doorOpenFlags,
    uint32_t containerDataFlags, QWidget* parent)
    : BaseDialog(isDoor ? "Door Interaction" : "Container Interaction", parent)
    , _isDoor(isDoor)
    , _doorOpenFlags(doorOpenFlags)
    , _containerDataFlags(containerDataFlags) {

    const uint32_t source = isDoor ? doorOpenFlags : containerDataFlags;

    auto* mainLayout = new QVBoxLayout(this);

    auto* group = new QGroupBox("Access", this);
    auto* groupLayout = new QVBoxLayout(group);

    _lockedBox = new QCheckBox("Locked", this);
    _lockedBox->setChecked((source & FLAG_LOCKED) != 0);
    groupLayout->addWidget(_lockedBox);

    _jammedBox = new QCheckBox("Jammed", this);
    _jammedBox->setChecked((source & FLAG_JAMMED) != 0);
    groupLayout->addWidget(_jammedBox);

    mainLayout->addWidget(group);

    mainLayout->addWidget(createButtonBox());
}

uint32_t InstancePropertiesDialog::getDoorOpenFlags() const {
    if (!_isDoor) {
        return _doorOpenFlags; // unchanged for containers
    }
    uint32_t result = _doorOpenFlags & ~(FLAG_LOCKED | FLAG_JAMMED);
    if (_lockedBox->isChecked()) {
        result |= FLAG_LOCKED;
    }
    if (_jammedBox->isChecked()) {
        result |= FLAG_JAMMED;
    }
    return result;
}

uint32_t InstancePropertiesDialog::getContainerDataFlags() const {
    if (_isDoor) {
        return _containerDataFlags; // unchanged for doors
    }
    uint32_t result = _containerDataFlags & ~(FLAG_LOCKED | FLAG_JAMMED);
    if (_lockedBox->isChecked()) {
        result |= FLAG_LOCKED;
    }
    if (_jammedBox->isChecked()) {
        result |= FLAG_JAMMED;
    }
    return result;
}

} // namespace geck
