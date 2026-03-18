#include "ProAmmoWidget.h"

#include "../../GameEnums.h"
#include "../../theme/ThemeManager.h"
#include "../../UIConstants.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <climits>

namespace geck {

ProAmmoWidget::ProAmmoWidget(resource::GameResources& resources, QWidget* parent)
    : ProTabWidget(resources, parent)
    , _caliberCombo(nullptr)
    , _quantityEdit(nullptr)
    , _damageModifierEdit(nullptr)
    , _damageResistModifierEdit(nullptr)
    , _damageMultiplierEdit(nullptr)
    , _damageTypeModifierEdit(nullptr) {
    setupUI();
}

void ProAmmoWidget::setupUI() {
    QGroupBox* ammoGroup = createStandardGroupBox("Ammo Properties");
    ammoGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* ammoLayout = createStandardFormLayout();
    addLayoutToGroupBox(ammoGroup, ammoLayout);

    _caliberCombo = createComboBox(game::enums::ammoCaliberTypes(_resources), "Ammo caliber or energy cell type");
    ammoLayout->addRow("Caliber:", _caliberCombo);

    _quantityEdit = createSpinBox(0, INT_MAX, "Rounds or charges contained in the ammo item");
    ammoLayout->addRow("Quantity:", _quantityEdit);

    _mainLayout->addWidget(ammoGroup);

    QGroupBox* modifierGroup = createStandardGroupBox("Combat Modifiers");
    modifierGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* modifierLayout = createStandardFormLayout();
    addLayoutToGroupBox(modifierGroup, modifierLayout);

    _damageModifierEdit = createSpinBox(INT_MIN, INT_MAX, "Signed damage modifier applied by this ammo");
    modifierLayout->addRow("Damage Mod:", _damageModifierEdit);

    _damageResistModifierEdit = createSpinBox(INT_MIN, INT_MAX, "Signed damage resistance modifier applied by this ammo");
    modifierLayout->addRow("DR Mod:", _damageResistModifierEdit);

    _damageMultiplierEdit = createSpinBox(INT_MIN, INT_MAX, "Signed damage multiplier value stored in the PRO");
    modifierLayout->addRow("Damage Mult:", _damageMultiplierEdit);

    _damageTypeModifierEdit = createSpinBox(INT_MIN, INT_MAX, "Signed damage type modifier value stored in the PRO");
    modifierLayout->addRow("Type Mod:", _damageTypeModifierEdit);

    _mainLayout->addWidget(modifierGroup);
    _mainLayout->addStretch();
}

void ProAmmoWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    _ammoData.caliber = pro->ammoData.caliber;
    _ammoData.quantity = pro->ammoData.quantity;
    _ammoData.damageModifier = pro->ammoData.damageModifier;
    _ammoData.damageResistModifier = pro->ammoData.damageResistModifier;
    _ammoData.damageMultiplier = pro->ammoData.damageMultiplier;
    _ammoData.damageTypeModifier = pro->ammoData.damageTypeModifier;

    setComboIndexSafe(_caliberCombo, _ammoData.caliber);
    if (_quantityEdit)
        _quantityEdit->setValue(static_cast<int>(_ammoData.quantity));
    if (_damageModifierEdit)
        _damageModifierEdit->setValue(_ammoData.damageModifier);
    if (_damageResistModifierEdit)
        _damageResistModifierEdit->setValue(_ammoData.damageResistModifier);
    if (_damageMultiplierEdit)
        _damageMultiplierEdit->setValue(_ammoData.damageMultiplier);
    if (_damageTypeModifierEdit)
        _damageTypeModifierEdit->setValue(_ammoData.damageTypeModifier);
}

void ProAmmoWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    _ammoData.caliber = static_cast<uint32_t>(getComboIndex(_caliberCombo));
    if (_quantityEdit)
        _ammoData.quantity = static_cast<uint32_t>(_quantityEdit->value());
    if (_damageModifierEdit)
        _ammoData.damageModifier = _damageModifierEdit->value();
    if (_damageResistModifierEdit)
        _ammoData.damageResistModifier = _damageResistModifierEdit->value();
    if (_damageMultiplierEdit)
        _ammoData.damageMultiplier = _damageMultiplierEdit->value();
    if (_damageTypeModifierEdit)
        _ammoData.damageTypeModifier = _damageTypeModifierEdit->value();

    pro->ammoData.caliber = _ammoData.caliber;
    pro->ammoData.quantity = _ammoData.quantity;
    pro->ammoData.damageModifier = _ammoData.damageModifier;
    pro->ammoData.damageResistModifier = _ammoData.damageResistModifier;
    pro->ammoData.damageMultiplier = _ammoData.damageMultiplier;
    pro->ammoData.damageTypeModifier = _ammoData.damageTypeModifier;
}

bool ProAmmoWidget::canHandle(const std::shared_ptr<Pro>& pro) const {
    return pro && pro->type() == Pro::OBJECT_TYPE::ITEM && pro->itemType() == Pro::ITEM_TYPE::AMMO;
}

QString ProAmmoWidget::getTabLabel() const {
    return "Ammo";
}

} // namespace geck
