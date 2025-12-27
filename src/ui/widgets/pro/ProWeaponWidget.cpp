#include "ProWeaponWidget.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include "../../theme/ThemeManager.h"
#include "../../GameEnums.h"

namespace geck {

ProWeaponWidget::ProWeaponWidget(QWidget* parent)
    : ProTabWidget(parent)
    , _weaponAnimationCombo(nullptr)
    , _weaponDamageMinEdit(nullptr)
    , _weaponDamageMaxEdit(nullptr)
    , _weaponDamageTypeCombo(nullptr)
    , _weaponAPPrimaryEdit(nullptr)
    , _weaponAPSecondaryEdit(nullptr)
    , _weaponRangePrimaryEdit(nullptr)
    , _weaponRangeSecondaryEdit(nullptr)
    , _weaponMinStrengthEdit(nullptr)
    , _weaponProjectilePIDEdit(nullptr)
    , _weaponCriticalFailEdit(nullptr)
    , _weaponPerkCombo(nullptr)
    , _weaponBurstRoundsEdit(nullptr)
    , _weaponAmmoTypeCombo(nullptr)
    , _weaponAmmoPIDEdit(nullptr)
    , _weaponAmmoCapacityEdit(nullptr)
    , _weaponSoundIdEdit(nullptr)
    , _weaponEnergyWeaponCheck(nullptr)
    , _weaponAIPriorityLabel(nullptr) {

    setupUI();
}

void ProWeaponWidget::setupUI() {
    // Create two-column layout
    QHBoxLayout* columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(12);

    // Left column
    QWidget* leftColumn = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    // Right column
    QWidget* rightColumn = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    // === LEFT COLUMN: Damage & Attack ===

    // Weapon Param (matching F2_ProtoManager naming)
    QGroupBox* basicGroup = new QGroupBox("Weapon Param");
    basicGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* basicLayout = createStandardFormLayout();

    _weaponAnimationCombo = createComboBox(game::enums::weaponAnimations(), "Weapon animation type");
    connect(_weaponAnimationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ProWeaponWidget::updateAIPriority);
    basicLayout->addRow("Animation:", _weaponAnimationCombo);

    _weaponDamageMinEdit = createSpinBox(0, 999, "Minimum damage");
    connect(_weaponDamageMinEdit, QOverload<int>::of(&QSpinBox::valueChanged),
        this, &ProWeaponWidget::updateAIPriority);
    basicLayout->addRow("Min Damage:", _weaponDamageMinEdit);

    _weaponDamageMaxEdit = createSpinBox(0, 999, "Maximum damage");
    connect(_weaponDamageMaxEdit, QOverload<int>::of(&QSpinBox::valueChanged),
        this, &ProWeaponWidget::updateAIPriority);
    basicLayout->addRow("Max Damage:", _weaponDamageMaxEdit);

    _weaponDamageTypeCombo = createComboBox(game::enums::damageTypes7(), "Damage type");
    basicLayout->addRow("Damage Type:", _weaponDamageTypeCombo);

    basicGroup->setLayout(basicLayout);
    leftLayout->addWidget(basicGroup);

    // AP Cost Attack (separate from Range, matching F2)
    QGroupBox* apGroup = new QGroupBox("AP Cost Attack");
    apGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* apLayout = createStandardFormLayout();

    _weaponAPPrimaryEdit = createSpinBox(0, 99, "Action points for primary attack");
    apLayout->addRow("AP Primary:", _weaponAPPrimaryEdit);

    _weaponAPSecondaryEdit = createSpinBox(0, 99, "Action points for secondary attack");
    apLayout->addRow("AP Secondary:", _weaponAPSecondaryEdit);

    apGroup->setLayout(apLayout);
    leftLayout->addWidget(apGroup);

    // Range Attack (separate groupbox, matching F2)
    QGroupBox* rangeGroup = new QGroupBox("Range Attack");
    rangeGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* rangeLayout = createStandardFormLayout();

    _weaponRangePrimaryEdit = createSpinBox(0, 999, "Primary attack range");
    rangeLayout->addRow("Range Primary:", _weaponRangePrimaryEdit);

    _weaponRangeSecondaryEdit = createSpinBox(0, 999, "Secondary attack range");
    rangeLayout->addRow("Range Secondary:", _weaponRangeSecondaryEdit);

    rangeGroup->setLayout(rangeLayout);
    leftLayout->addWidget(rangeGroup);
    leftLayout->addStretch();

    // === RIGHT COLUMN: Advanced Properties ===

    // Requirements & Special
    QGroupBox* reqGroup = new QGroupBox("Requirements & Special");
    reqGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* reqLayout = createStandardFormLayout();

    _weaponMinStrengthEdit = createSpinBox(0, 10, "Minimum strength required");
    reqLayout->addRow("Min Strength:", _weaponMinStrengthEdit);

    _weaponProjectilePIDEdit = createSpinBox(0, 999999, "Projectile PRO ID");
    reqLayout->addRow("Projectile PID:", _weaponProjectilePIDEdit);

    _weaponCriticalFailEdit = createSpinBox(0, 100, "Critical failure chance");
    reqLayout->addRow("Critical Fail:", _weaponCriticalFailEdit);

    _weaponPerkCombo = createComboBox({ "None", "Penetrate", "Knockback", "Knockdown", "Other" },
        "Special perk associated with weapon");
    reqLayout->addRow("Perk:", _weaponPerkCombo);

    reqGroup->setLayout(reqLayout);
    rightLayout->addWidget(reqGroup);

    // Ammo & Burst
    QGroupBox* ammoGroup = new QGroupBox("Ammo & Burst");
    ammoGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* ammoLayout = createStandardFormLayout();

    _weaponBurstRoundsEdit = createSpinBox(0, 99, "Number of rounds per burst");
    ammoLayout->addRow("Burst Rounds:", _weaponBurstRoundsEdit);

    _weaponAmmoTypeCombo = createComboBox({ "None", ".223 FMJ", "5mm JHP", "5mm AP", "10mm JHP", "10mm AP",
                                              ".44 Magnum JHP", ".44 Magnum FMJ", "14mm AP", "12 ga. Shotgun",
                                              "7.62mm", "9mm", "BB's", "Energy Cell", "Micro Fusion Cell",
                                              "Small Energy Cell", "Flamethrower Fuel", "Rocket", "Plasma" },
        "Ammunition type");
    ammoLayout->addRow("Ammo Type:", _weaponAmmoTypeCombo);

    _weaponAmmoPIDEdit = createSpinBox(0, 999999, "Ammunition PRO ID");
    ammoLayout->addRow("Ammo PID:", _weaponAmmoPIDEdit);

    _weaponAmmoCapacityEdit = createSpinBox(0, 999, "Maximum ammunition capacity");
    ammoLayout->addRow("Ammo Capacity:", _weaponAmmoCapacityEdit);

    ammoGroup->setLayout(ammoLayout);
    rightLayout->addWidget(ammoGroup);

    // Misc Properties
    QGroupBox* miscGroup = createStandardGroupBox("Misc Properties");
    QFormLayout* miscLayout = createStandardFormLayout();
    static_cast<QVBoxLayout*>(miscGroup->layout())->addLayout(miscLayout);

    _weaponSoundIdEdit = createSpinBox(0, 255, "Sound ID for weapon");
    miscLayout->addRow("Sound ID:", _weaponSoundIdEdit);

    _weaponEnergyWeaponCheck = new QCheckBox("Energy weapon flag (sfall feature)");
    _weaponEnergyWeaponCheck->setToolTip("Forces weapon to use Energy Weapons skill");
    connectCheckBox(_weaponEnergyWeaponCheck);
    miscLayout->addRow("", _weaponEnergyWeaponCheck);

    // AI Priority display
    _weaponAIPriorityLabel = new QLabel("0");
    _weaponAIPriorityLabel->setStyleSheet(ui::theme::styles::emphasisLabel());
    _weaponAIPriorityLabel->setToolTip("AI Priority = average damage + range (used by AI to select best weapon)");
    miscLayout->addRow("AI Priority:", _weaponAIPriorityLabel);

    rightLayout->addWidget(miscGroup);
    rightLayout->addStretch();

    // Add columns to main layout
    columnsLayout->addWidget(leftColumn, 1);
    columnsLayout->addWidget(rightColumn, 1);
    _mainLayout->addLayout(columnsLayout);
}

void ProWeaponWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    // Load weapon data - copy fields individually
    _weaponData.animationCode = pro->weaponData.animationCode;
    _weaponData.damageMin = pro->weaponData.damageMin;
    _weaponData.damageMax = pro->weaponData.damageMax;
    _weaponData.damageType = pro->weaponData.damageType;
    _weaponData.rangePrimary = pro->weaponData.rangePrimary;
    _weaponData.rangeSecondary = pro->weaponData.rangeSecondary;
    _weaponData.projectilePID = pro->weaponData.projectilePID;
    _weaponData.minimumStrength = pro->weaponData.minimumStrength;
    _weaponData.actionCostPrimary = pro->weaponData.actionCostPrimary;
    _weaponData.actionCostSecondary = pro->weaponData.actionCostSecondary;
    _weaponData.criticalFail = pro->weaponData.criticalFail;
    _weaponData.perk = pro->weaponData.perk;
    _weaponData.burstRounds = pro->weaponData.burstRounds;
    _weaponData.ammoType = pro->weaponData.ammoType;
    _weaponData.ammoPID = pro->weaponData.ammoPID;
    _weaponData.ammoCapacity = pro->weaponData.ammoCapacity;
    _weaponData.soundId = pro->weaponData.soundId;

    // Update UI controls
    setComboIndex(_weaponAnimationCombo, static_cast<int>(_weaponData.animationCode));
    if (_weaponDamageMinEdit)
        _weaponDamageMinEdit->setValue(static_cast<int>(_weaponData.damageMin));
    if (_weaponDamageMaxEdit)
        _weaponDamageMaxEdit->setValue(static_cast<int>(_weaponData.damageMax));
    setComboIndex(_weaponDamageTypeCombo, static_cast<int>(_weaponData.damageType));
    if (_weaponRangePrimaryEdit)
        _weaponRangePrimaryEdit->setValue(static_cast<int>(_weaponData.rangePrimary));
    if (_weaponRangeSecondaryEdit)
        _weaponRangeSecondaryEdit->setValue(static_cast<int>(_weaponData.rangeSecondary));
    if (_weaponProjectilePIDEdit)
        _weaponProjectilePIDEdit->setValue(static_cast<int>(_weaponData.projectilePID));
    if (_weaponMinStrengthEdit)
        _weaponMinStrengthEdit->setValue(static_cast<int>(_weaponData.minimumStrength));
    if (_weaponAPPrimaryEdit)
        _weaponAPPrimaryEdit->setValue(static_cast<int>(_weaponData.actionCostPrimary));
    if (_weaponAPSecondaryEdit)
        _weaponAPSecondaryEdit->setValue(static_cast<int>(_weaponData.actionCostSecondary));
    if (_weaponCriticalFailEdit)
        _weaponCriticalFailEdit->setValue(static_cast<int>(_weaponData.criticalFail));
    setComboIndex(_weaponPerkCombo, static_cast<int>(_weaponData.perk));
    if (_weaponBurstRoundsEdit)
        _weaponBurstRoundsEdit->setValue(static_cast<int>(_weaponData.burstRounds));
    setComboIndex(_weaponAmmoTypeCombo, static_cast<int>(_weaponData.ammoType));
    if (_weaponAmmoPIDEdit)
        _weaponAmmoPIDEdit->setValue(static_cast<int>(_weaponData.ammoPID));
    if (_weaponAmmoCapacityEdit)
        _weaponAmmoCapacityEdit->setValue(static_cast<int>(_weaponData.ammoCapacity));
    if (_weaponSoundIdEdit)
        _weaponSoundIdEdit->setValue(static_cast<int>(_weaponData.soundId));

    // Handle energy weapon flag from Pro.h weaponFlags field if available
    if (_weaponEnergyWeaponCheck && pro->weaponData.weaponFlags) {
        _weaponEnergyWeaponCheck->setChecked((pro->weaponData.weaponFlags & 0x1) != 0);
    }

    updateAIPriority();
}

void ProWeaponWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    // Update data from UI
    _weaponData.animationCode = static_cast<uint32_t>(getComboIndex(_weaponAnimationCombo));
    if (_weaponDamageMinEdit)
        _weaponData.damageMin = static_cast<uint32_t>(_weaponDamageMinEdit->value());
    if (_weaponDamageMaxEdit)
        _weaponData.damageMax = static_cast<uint32_t>(_weaponDamageMaxEdit->value());
    _weaponData.damageType = static_cast<uint32_t>(getComboIndex(_weaponDamageTypeCombo));
    if (_weaponRangePrimaryEdit)
        _weaponData.rangePrimary = static_cast<uint32_t>(_weaponRangePrimaryEdit->value());
    if (_weaponRangeSecondaryEdit)
        _weaponData.rangeSecondary = static_cast<uint32_t>(_weaponRangeSecondaryEdit->value());
    if (_weaponProjectilePIDEdit)
        _weaponData.projectilePID = static_cast<int32_t>(_weaponProjectilePIDEdit->value());
    if (_weaponMinStrengthEdit)
        _weaponData.minimumStrength = static_cast<uint32_t>(_weaponMinStrengthEdit->value());
    if (_weaponAPPrimaryEdit)
        _weaponData.actionCostPrimary = static_cast<uint32_t>(_weaponAPPrimaryEdit->value());
    if (_weaponAPSecondaryEdit)
        _weaponData.actionCostSecondary = static_cast<uint32_t>(_weaponAPSecondaryEdit->value());
    if (_weaponCriticalFailEdit)
        _weaponData.criticalFail = static_cast<uint32_t>(_weaponCriticalFailEdit->value());
    _weaponData.perk = static_cast<uint32_t>(getComboIndex(_weaponPerkCombo));
    if (_weaponBurstRoundsEdit)
        _weaponData.burstRounds = static_cast<uint32_t>(_weaponBurstRoundsEdit->value());
    _weaponData.ammoType = static_cast<uint32_t>(getComboIndex(_weaponAmmoTypeCombo));
    if (_weaponAmmoPIDEdit)
        _weaponData.ammoPID = static_cast<int32_t>(_weaponAmmoPIDEdit->value());
    if (_weaponAmmoCapacityEdit)
        _weaponData.ammoCapacity = static_cast<uint32_t>(_weaponAmmoCapacityEdit->value());
    if (_weaponSoundIdEdit)
        _weaponData.soundId = static_cast<uint8_t>(_weaponSoundIdEdit->value());

    // Save to PRO - copy fields individually
    pro->weaponData.animationCode = _weaponData.animationCode;
    pro->weaponData.damageMin = _weaponData.damageMin;
    pro->weaponData.damageMax = _weaponData.damageMax;
    pro->weaponData.damageType = _weaponData.damageType;
    pro->weaponData.rangePrimary = _weaponData.rangePrimary;
    pro->weaponData.rangeSecondary = _weaponData.rangeSecondary;
    pro->weaponData.projectilePID = _weaponData.projectilePID;
    pro->weaponData.minimumStrength = _weaponData.minimumStrength;
    pro->weaponData.actionCostPrimary = _weaponData.actionCostPrimary;
    pro->weaponData.actionCostSecondary = _weaponData.actionCostSecondary;
    pro->weaponData.criticalFail = _weaponData.criticalFail;
    pro->weaponData.perk = _weaponData.perk;
    pro->weaponData.burstRounds = _weaponData.burstRounds;
    pro->weaponData.ammoType = _weaponData.ammoType;
    pro->weaponData.ammoPID = _weaponData.ammoPID;
    pro->weaponData.ammoCapacity = _weaponData.ammoCapacity;
    pro->weaponData.soundId = _weaponData.soundId;

    // Handle energy weapon flag
    if (_weaponEnergyWeaponCheck) {
        if (_weaponEnergyWeaponCheck->isChecked()) {
            pro->weaponData.weaponFlags |= 0x1;
        } else {
            pro->weaponData.weaponFlags &= ~0x1;
        }
    }
}

bool ProWeaponWidget::canHandle(const std::shared_ptr<Pro>& pro) const {
    return pro && pro->type() == Pro::OBJECT_TYPE::ITEM && pro->itemType() == Pro::ITEM_TYPE::WEAPON;
}

QString ProWeaponWidget::getTabLabel() const {
    return "Weapon";
}

void ProWeaponWidget::updateAIPriority() {
    if (_weaponAIPriorityLabel) {
        _weaponAIPriorityLabel->setText(QString::number(calculateAIPriority()));
    }
}

int ProWeaponWidget::calculateAIPriority() const {
    int avgDamage = 0;
    int range = 0;

    // Calculate average damage
    if (_weaponDamageMinEdit && _weaponDamageMaxEdit) {
        avgDamage = (_weaponDamageMinEdit->value() + _weaponDamageMaxEdit->value()) / 2;
    }

    // Use primary range
    if (_weaponRangePrimaryEdit) {
        range = _weaponRangePrimaryEdit->value();
    }

    return avgDamage + range;
}

} // namespace geck