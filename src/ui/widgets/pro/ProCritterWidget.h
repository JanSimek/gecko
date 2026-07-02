#pragma once

#include "ProTabWidget.h"

#include "format/ai/AiPacket.h"

class QTabWidget;
class QPushButton;

namespace geck {

class ProCritterWidget : public ProTabWidget {
    Q_OBJECT

public:
    explicit ProCritterWidget(resource::GameResources& resources, QWidget* parent = nullptr);
    ~ProCritterWidget() override = default;

    void loadFromPro(const std::shared_ptr<Pro>& pro) override;
    void saveToPro(std::shared_ptr<Pro>& pro) override;
    bool canHandle(const std::shared_ptr<Pro>& pro) const override;
    QString getTabLabel() const override;

private:
    void setupUI();
    void setupStatsTab(QTabWidget* parentTabs);
    void setupDefenceTab(QTabWidget* parentTabs);
    void setupGeneralTab(QTabWidget* parentTabs);
    void updateHeadFidLabel();
    // Resolve the AI Packet spin box value to its ai.txt name and show it beside the number.
    void updateAiPacketNameLabel();
    uint32_t currentCritterFlags() const;

    // data/ai.txt packet -> name table, loaded once so the AI Packet field reads as a name
    // (engine-fidelity: the spin box still stores the raw packet number). Empty if unmounted.
    AiTxt _aiTxt;

    static constexpr Frm::FRM_TYPE HeadFrmObjectType = Frm::FRM_TYPE::CRITTER;

    QSpinBox* _specialStatEdits[7];
    QSpinBox* _skillEdits[18];
    QSpinBox* _damageThresholdEdits[7];
    QSpinBox* _damageResistEdits[9];

    QLabel* _headFidLabel;
    QPushButton* _headFidSelectorButton;
    int32_t _headFid;

    QSpinBox* _aiPacketEdit;
    QLabel* _aiPacketNameLabel;
    QSpinBox* _teamNumberEdit;
    QCheckBox* _barterCheck;
    QCheckBox* _noStealCheck;
    QCheckBox* _noDropCheck;
    QCheckBox* _noLimbsCheck;
    QCheckBox* _noAgeCheck;
    QCheckBox* _noHealCheck;
    QCheckBox* _invulnerableCheck;
    QCheckBox* _noFlattenCheck;
    QCheckBox* _specialDeathCheck;
    QCheckBox* _longLimbsCheck;
    QCheckBox* _noKnockbackCheck;

    QSpinBox* _maxHitPointsEdit;
    QSpinBox* _actionPointsEdit;
    QSpinBox* _armorClassEdit;
    QSpinBox* _meleeDamageEdit;
    QSpinBox* _carryWeightMaxEdit;
    QSpinBox* _sequenceEdit;
    QSpinBox* _healingRateEdit;
    QSpinBox* _criticalChanceEdit;
    QSpinBox* _betterCriticalsEdit;
    QSpinBox* _ageEdit;
    QComboBox* _genderCombo;
    QComboBox* _bodyTypeCombo;
    QSpinBox* _experienceEdit;
    QSpinBox* _killTypeEdit;
    QSpinBox* _damageTypeEdit;
};

} // namespace geck
