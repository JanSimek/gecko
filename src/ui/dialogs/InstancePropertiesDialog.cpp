#include "InstancePropertiesDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QVBoxLayout>

namespace geck {

InstancePropertiesDialog::InstancePropertiesDialog(bool isDoor, uint32_t doorOpenFlags,
    uint32_t containerDataFlags, QWidget* parent)
    : QDialog(parent)
    , _isDoor(isDoor)
    , _doorOpenFlags(doorOpenFlags)
    , _containerDataFlags(containerDataFlags) {

    setWindowTitle(isDoor ? "Door Interaction" : "Container Interaction");

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

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
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
