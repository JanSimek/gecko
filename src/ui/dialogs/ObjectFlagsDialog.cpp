#include "ObjectFlagsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QVBoxLayout>

#include "format/pro/Pro.h"

namespace geck {

namespace {
    constexpr uint32_t bit(Pro::ObjectFlags f) { return static_cast<uint32_t>(f); }
} // namespace

ObjectFlagsDialog::ObjectFlagsDialog(uint32_t flags, uint32_t objectType, QWidget* parent)
    : QDialog(parent)
    , _originalFlags(flags) {

    setWindowTitle("Object Flags");

    auto* mainLayout = new QVBoxLayout(this);

    const bool isItem = objectType == static_cast<uint32_t>(Pro::OBJECT_TYPE::ITEM);
    const bool isWallOrScenery = objectType == static_cast<uint32_t>(Pro::OBJECT_TYPE::WALL)
        || objectType == static_cast<uint32_t>(Pro::OBJECT_TYPE::SCENERY);

    auto* behaviourGroup = new QGroupBox("Behaviour", this);
    auto* behaviourLayout = new QVBoxLayout(behaviourGroup);
    addFlag(behaviourLayout, "Flat", bit(Pro::ObjectFlags::OBJECT_FLAT));
    addFlag(behaviourLayout, "No Block", bit(Pro::ObjectFlags::OBJECT_NO_BLOCK));
    addFlag(behaviourLayout, "Multi Hex", bit(Pro::ObjectFlags::OBJECT_MULTIHEX));
    addFlag(behaviourLayout, "Shoot Thru", bit(Pro::ObjectFlags::OBJECT_SHOOT_THRU));
    addFlag(behaviourLayout, "Light Thru", bit(Pro::ObjectFlags::OBJECT_LIGHT_THRU));
    if (isWallOrScenery) {
        addFlag(behaviourLayout, "Wall Trans End", bit(Pro::ObjectFlags::OBJECT_WALL_TRANS_END));
    }
    if (isItem) {
        addFlag(behaviourLayout, "No Highlight", bit(Pro::ObjectFlags::OBJECT_NO_HIGHLIGHT));
    }
    mainLayout->addWidget(behaviourGroup);

    auto* transparencyGroup = new QGroupBox("Transparency", this);
    auto* transparencyLayout = new QVBoxLayout(transparencyGroup);
    addFlag(transparencyLayout, "None", bit(Pro::ObjectFlags::OBJECT_TRANS_NONE));
    addFlag(transparencyLayout, "Red", bit(Pro::ObjectFlags::OBJECT_TRANS_RED));
    addFlag(transparencyLayout, "Glass", bit(Pro::ObjectFlags::OBJECT_TRANS_GLASS));
    addFlag(transparencyLayout, "Steam", bit(Pro::ObjectFlags::OBJECT_TRANS_STEAM));
    addFlag(transparencyLayout, "Energy", bit(Pro::ObjectFlags::OBJECT_TRANS_ENERGY));
    mainLayout->addWidget(transparencyGroup);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void ObjectFlagsDialog::addFlag(QLayout* layout, const QString& label, uint32_t bit) {
    auto* box = new QCheckBox(label, this);
    box->setChecked((_originalFlags & bit) != 0);
    layout->addWidget(box);
    _entries.push_back({ box, bit });
    _shownMask |= bit;
}

uint32_t ObjectFlagsDialog::getFlags() const {
    // Preserve every bit we did not expose, then OR back the checked ones.
    uint32_t result = _originalFlags & ~_shownMask;
    for (const auto& entry : _entries) {
        if (entry.box->isChecked()) {
            result |= entry.bit;
        }
    }
    return result;
}

} // namespace geck
