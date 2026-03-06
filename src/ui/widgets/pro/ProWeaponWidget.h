#pragma once

#include "ProTabWidget.h"

namespace geck {

/**
 * @brief Widget for editing Weapon type PRO files
 *
 * Handles weapon-specific fields including:
 * - Animation, damage (min/max), damage type
 * - AP costs and ranges for primary/secondary attacks
 * - Minimum strength, critical fail chance
 * - Perk, burst rounds, ammo information
 * - AI Priority calculation
 */
class ProWeaponWidget : public ProTabWidget {
    Q_OBJECT

public:
    explicit ProWeaponWidget(QWidget* parent = nullptr);
    ~ProWeaponWidget() override = default;

    // ProTabWidget interface
    void loadFromPro(const std::shared_ptr<Pro>& pro) override;
    void saveToPro(std::shared_ptr<Pro>& pro) override;
    bool canHandle(const std::shared_ptr<Pro>& pro) const override;
    QString getTabLabel() const override;

private:
    void setupUI();
    void updateAIPriority();
    int calculateAIPriority() const;

    // UI controls - Column 1: Damage & Attack
    QComboBox* _weaponAnimationCombo;
    QSpinBox* _weaponDamageMinEdit;
    QSpinBox* _weaponDamageMaxEdit;
    QComboBox* _weaponDamageTypeCombo;
    QSpinBox* _weaponAPPrimaryEdit;
    QSpinBox* _weaponAPSecondaryEdit;
    QSpinBox* _weaponRangePrimaryEdit;
    QSpinBox* _weaponRangeSecondaryEdit;

    // UI controls - Column 2: Advanced Properties
    QSpinBox* _weaponMinStrengthEdit;
    QSpinBox* _weaponProjectilePIDEdit;
    QSpinBox* _weaponCriticalFailEdit;
    QComboBox* _weaponPerkCombo;
    QSpinBox* _weaponBurstRoundsEdit;
    QComboBox* _weaponAmmoTypeCombo;
    QSpinBox* _weaponAmmoPIDEdit;
    QSpinBox* _weaponAmmoCapacityEdit;
    QSpinBox* _weaponSoundIdEdit;
    QCheckBox* _weaponEnergyWeaponCheck;
    QLabel* _weaponAIPriorityLabel;

    // Data
    ProWeaponData _weaponData;
};

} // namespace geck