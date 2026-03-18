#include "ProCritterWidget.h"

#include "../../../resource/GameResources.h"
#include "../../theme/ThemeManager.h"
#include "../../UIConstants.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <climits>
#include <filesystem>

namespace geck {

namespace {

    constexpr int NumSpecialStats = 7;
    constexpr int NumSkills = 18;
    constexpr int NumDamageTypes = 7;
    constexpr int NumDamageTypesWithRadiation = 9;
    constexpr int MinSpecialStat = 1;
    constexpr int MaxSpecialStat = 10;
    constexpr int MaxSkillPercent = 300;
    constexpr int MinAge = 1;
    constexpr int MaxAge = 99;

}

ProCritterWidget::ProCritterWidget(resource::GameResources& resources, QWidget* parent)
    : ProTabWidget(resources, parent)
    , _headFidLabel(nullptr)
    , _headFidSelectorButton(nullptr)
    , _headFid(0)
    , _aiPacketEdit(nullptr)
    , _teamNumberEdit(nullptr)
    , _barterCheck(nullptr)
    , _noStealCheck(nullptr)
    , _noDropCheck(nullptr)
    , _noLimbsCheck(nullptr)
    , _noAgeCheck(nullptr)
    , _noHealCheck(nullptr)
    , _invulnerableCheck(nullptr)
    , _noFlattenCheck(nullptr)
    , _specialDeathCheck(nullptr)
    , _longLimbsCheck(nullptr)
    , _noKnockbackCheck(nullptr)
    , _maxHitPointsEdit(nullptr)
    , _actionPointsEdit(nullptr)
    , _armorClassEdit(nullptr)
    , _meleeDamageEdit(nullptr)
    , _carryWeightMaxEdit(nullptr)
    , _sequenceEdit(nullptr)
    , _healingRateEdit(nullptr)
    , _criticalChanceEdit(nullptr)
    , _betterCriticalsEdit(nullptr)
    , _ageEdit(nullptr)
    , _genderCombo(nullptr)
    , _bodyTypeCombo(nullptr)
    , _experienceEdit(nullptr)
    , _killTypeEdit(nullptr)
    , _damageTypeEdit(nullptr) {
    for (auto& widget : _specialStatEdits) {
        widget = nullptr;
    }
    for (auto& widget : _skillEdits) {
        widget = nullptr;
    }
    for (auto& widget : _damageThresholdEdits) {
        widget = nullptr;
    }
    for (auto& widget : _damageResistEdits) {
        widget = nullptr;
    }

    setupUI();
}

void ProCritterWidget::setupUI() {
    auto* critterTabs = new QTabWidget();
    critterTabs->setTabPosition(QTabWidget::North);
    setupStatsTab(critterTabs);
    setupDefenceTab(critterTabs);
    setupGeneralTab(critterTabs);
    _mainLayout->addWidget(critterTabs);
}

void ProCritterWidget::setupStatsTab(QTabWidget* parentTabs) {
    auto* statsTab = new QWidget();
    auto* mainLayout = new QVBoxLayout(statsTab);
    mainLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    mainLayout->setSpacing(ui::constants::SPACING_FORM);

    auto* topLayout = new QHBoxLayout();
    topLayout->setSpacing(ui::constants::SPACING_COLUMNS);

    auto* specialGroup = this->createStandardGroupBox("SPECIAL Stats");
    auto* specialLayout = new QGridLayout();
    specialLayout->setContentsMargins(0, 0, 0, 0);
    specialLayout->setSpacing(ui::constants::SPACING_TIGHT);
    addLayoutToGroupBox(specialGroup, specialLayout);
    specialGroup->setStyleSheet(ui::theme::styles::boldGroupBox());

    const char* specialNames[] = { "STR", "PER", "END", "CHR", "INT", "AGL", "LCK" };
    for (int i = 0; i < NumSpecialStats; ++i) {
        _specialStatEdits[i] = this->createSpinBox(MinSpecialStat, MaxSpecialStat, QString("Base %1 stat").arg(specialNames[i]));
        auto* label = new QLabel(specialNames[i]);
        label->setFixedWidth(ui::constants::sizes::LABEL_FRAME);
        specialLayout->addWidget(label, i / 4, (i % 4) * 2);
        specialLayout->addWidget(_specialStatEdits[i], i / 4, (i % 4) * 2 + 1);
    }

    auto* skillsGroup = this->createStandardGroupBox("Skills");
    auto* skillsLayout = new QGridLayout();
    skillsLayout->setContentsMargins(0, 0, 0, 0);
    skillsLayout->setSpacing(ui::constants::SPACING_TIGHT);
    addLayoutToGroupBox(skillsGroup, skillsLayout);
    skillsGroup->setStyleSheet(ui::theme::styles::boldGroupBox());

    auto addSkillsHeader = [&](int column, const QString& skillTitle, const QString& valueTitle) {
        auto* skillHeader = new QLabel(skillTitle);
        skillHeader->setStyleSheet(ui::theme::styles::boldLabel());
        skillsLayout->addWidget(skillHeader, 0, column);

        auto* valueHeader = new QLabel(valueTitle);
        valueHeader->setStyleSheet(ui::theme::styles::boldLabel());
        valueHeader->setAlignment(Qt::AlignCenter);
        skillsLayout->addWidget(valueHeader, 0, column + 1);
    };

    addSkillsHeader(0, "Skill", "Value (%)");
    addSkillsHeader(2, "Skill", "Value (%)");
    addSkillsHeader(4, "Skill", "Value (%)");

    const char* skillNames[] = {
        "Small Guns", "Big Guns", "Energy Weapons", "Unarmed", "Melee Weapons", "Throwing",
        "First Aid", "Doctor", "Sneak", "Lockpick", "Steal", "Traps",
        "Science", "Repair", "Speech", "Barter", "Gambling", "Outdoorsman"
    };

    for (int i = 0; i < NumSkills; ++i) {
        const int column = i / 6;
        const int row = (i % 6) + 1;
        auto* skillLabel = new QLabel(QString::fromLatin1(skillNames[i]) + ":");
        skillLabel->setFixedWidth(ui::constants::sizes::WIDTH_LABEL_SKILL);
        skillsLayout->addWidget(skillLabel, row, column * 2);

        _skillEdits[i] = this->createSpinBox(0, MaxSkillPercent, QString("%1 skill percentage").arg(skillNames[i]));
        _skillEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        skillsLayout->addWidget(_skillEdits[i], row, column * 2 + 1);
    }

    topLayout->addWidget(specialGroup);
    topLayout->addWidget(skillsGroup, 1);
    mainLayout->addLayout(topLayout);

    auto* combatGroup = this->createStandardGroupBox("Combat Stats");
    combatGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    auto* combatLayout = this->createStandardFormLayout();
    addLayoutToGroupBox(combatGroup, combatLayout);

    _maxHitPointsEdit = this->createSpinBox(1, INT_MAX, "Maximum hit points");
    combatLayout->addRow("Max HP:", _maxHitPointsEdit);

    _actionPointsEdit = this->createSpinBox(1, INT_MAX, "Action points per turn");
    combatLayout->addRow("Action Points:", _actionPointsEdit);

    _armorClassEdit = this->createSpinBox(0, INT_MAX, "Base armor class");
    combatLayout->addRow("Armor Class:", _armorClassEdit);

    _meleeDamageEdit = this->createSpinBox(0, INT_MAX, "Melee damage bonus");
    combatLayout->addRow("Melee Damage:", _meleeDamageEdit);

    _carryWeightMaxEdit = this->createSpinBox(0, INT_MAX, "Maximum carry weight");
    combatLayout->addRow("Carry Weight:", _carryWeightMaxEdit);

    _sequenceEdit = this->createSpinBox(0, INT_MAX, "Initiative sequence bonus");
    combatLayout->addRow("Sequence:", _sequenceEdit);

    _healingRateEdit = this->createSpinBox(0, INT_MAX, "Healing rate bonus");
    combatLayout->addRow("Healing Rate:", _healingRateEdit);

    _criticalChanceEdit = this->createSpinBox(0, INT_MAX, "Critical hit chance bonus");
    combatLayout->addRow("Critical Chance:", _criticalChanceEdit);

    _betterCriticalsEdit = this->createSpinBox(0, INT_MAX, "Better criticals bonus");
    combatLayout->addRow("Better Criticals:", _betterCriticalsEdit);

    mainLayout->addWidget(combatGroup);
    mainLayout->addStretch();
    parentTabs->addTab(statsTab, "Stats");
}

void ProCritterWidget::setupDefenceTab(QTabWidget* parentTabs) {
    auto* defenceTab = new QWidget();
    auto* mainLayout = new QVBoxLayout(defenceTab);
    mainLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    mainLayout->setSpacing(ui::constants::SPACING_FORM);

    auto* damageProtectionGroup = this->createStandardGroupBox("Damage Protection");
    damageProtectionGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    auto* damageProtectionLayout = new QGridLayout();
    damageProtectionLayout->setContentsMargins(0, 0, 0, 0);
    damageProtectionLayout->setSpacing(ui::constants::SPACING_GRID);
    addLayoutToGroupBox(damageProtectionGroup, damageProtectionLayout);

    auto* thresholdHeader = new QLabel("Threshold");
    thresholdHeader->setStyleSheet(ui::theme::styles::boldLabel());
    thresholdHeader->setAlignment(Qt::AlignCenter);
    damageProtectionLayout->addWidget(thresholdHeader, 0, 1);

    auto* resistanceHeader = new QLabel("Resistance");
    resistanceHeader->setStyleSheet(ui::theme::styles::boldLabel());
    resistanceHeader->setAlignment(Qt::AlignCenter);
    damageProtectionLayout->addWidget(resistanceHeader, 0, 2);

    const QStringList damageTypes = game::enums::damageTypes9(_resources);
    for (int i = 0; i < NumDamageTypes; ++i) {
        auto* typeLabel = new QLabel(damageTypes.at(i) + ":");
        typeLabel->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(typeLabel, i + 1, 0);

        _damageThresholdEdits[i] = this->createSpinBox(0, INT_MAX, QString("%1 damage threshold").arg(damageTypes.at(i)));
        _damageThresholdEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(_damageThresholdEdits[i], i + 1, 1);

        _damageResistEdits[i] = this->createSpinBox(0, INT_MAX, QString("%1 damage resistance").arg(damageTypes.at(i)));
        _damageResistEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(_damageResistEdits[i], i + 1, 2);
    }

    for (int i = NumDamageTypes; i < NumDamageTypesWithRadiation; ++i) {
        auto* typeLabel = new QLabel(damageTypes.at(i) + ":");
        typeLabel->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(typeLabel, i + 1, 0);

        auto* placeholder = new QLabel("-");
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet(ui::theme::styles::placeholderText());
        placeholder->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(placeholder, i + 1, 1);

        _damageResistEdits[i] = this->createSpinBox(0, INT_MAX, QString("%1 damage resistance").arg(damageTypes.at(i)));
        _damageResistEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(_damageResistEdits[i], i + 1, 2);
    }

    mainLayout->addWidget(damageProtectionGroup);
    mainLayout->addStretch();
    parentTabs->addTab(defenceTab, "Defence");
}

void ProCritterWidget::setupGeneralTab(QTabWidget* parentTabs) {
    auto* generalTab = new QWidget();
    auto* mainLayout = new QVBoxLayout(generalTab);
    mainLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    mainLayout->setSpacing(ui::constants::SPACING_FORM);

    auto* columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(ui::constants::SPACING_COLUMNS);

    auto* leftColumn = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(ui::constants::SPACING_NORMAL);

    auto* critterGroup = this->createStandardGroupBox("Critter Properties");
    critterGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    auto* critterLayout = this->createStandardFormLayout();
    addLayoutToGroupBox(critterGroup, critterLayout);

    auto* headFidWidget = new QWidget();
    auto* headFidLayout = new QHBoxLayout(headFidWidget);
    headFidLayout->setContentsMargins(0, 0, 0, 0);
    headFidLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _headFidLabel = new QLabel("No FRM");
    _headFidLabel->setToolTip("FRM filename for critter head appearance");
    _headFidLabel->setStyleSheet(ui::theme::styles::borderedLabel());

    _headFidSelectorButton = new QPushButton("...");
    _headFidSelectorButton->setMaximumWidth(ui::constants::sizes::ICON_BUTTON);
    _headFidSelectorButton->setMaximumHeight(ui::constants::sizes::ICON_BUTTON_HEIGHT);
    _headFidSelectorButton->setToolTip("Browse FRM files for critter head");
    connect(_headFidSelectorButton, &QPushButton::clicked, this, [this]() {
        emit fidLabelSelectorRequested(_headFidLabel, &_headFid, HeadFrmObjectType);
    });

    headFidLayout->addWidget(_headFidLabel, 1);
    headFidLayout->addWidget(_headFidSelectorButton);
    critterLayout->addRow("Head FID:", headFidWidget);

    _aiPacketEdit = this->createSpinBox(0, INT_MAX, "AI packet number for critter behavior");
    critterLayout->addRow("AI Packet:", _aiPacketEdit);

    _teamNumberEdit = this->createSpinBox(0, INT_MAX, "Team number for faction identification");
    critterLayout->addRow("Team Number:", _teamNumberEdit);

    leftLayout->addWidget(critterGroup);

    auto* rightColumn = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(ui::constants::SPACING_NORMAL);

    auto* charGroup = this->createStandardGroupBox("Character Information");
    charGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    auto* charLayout = this->createStandardFormLayout();
    addLayoutToGroupBox(charGroup, charLayout);

    _ageEdit = this->createSpinBox(MinAge, MaxAge, "Critter age");
    charLayout->addRow("Age:", _ageEdit);

    _genderCombo = this->createComboBox(game::enums::critterGenders(), "Critter gender");
    charLayout->addRow("Gender:", _genderCombo);

    _bodyTypeCombo = this->createComboBox(game::enums::critterBodyTypes(_resources), "Body type for animations");
    charLayout->addRow("Body Type:", _bodyTypeCombo);

    _experienceEdit = this->createSpinBox(0, INT_MAX, "Experience points for killing this critter");
    charLayout->addRow("Experience:", _experienceEdit);

    _killTypeEdit = this->createSpinBox(0, INT_MAX, "Kill type for karma tracking");
    charLayout->addRow("Kill Type:", _killTypeEdit);

    _damageTypeEdit = this->createSpinBox(0, INT_MAX, "Default damage type");
    charLayout->addRow("Damage Type:", _damageTypeEdit);

    rightLayout->addWidget(charGroup);

    columnsLayout->addWidget(leftColumn);
    columnsLayout->addWidget(rightColumn);
    mainLayout->addLayout(columnsLayout);

    auto* critterFlagsGroup = this->createStandardGroupBox("Critter Flags");
    critterFlagsGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    auto* critterFlagsLayout = new QGridLayout();
    critterFlagsLayout->setContentsMargins(0, 0, 0, 0);
    critterFlagsLayout->setSpacing(ui::constants::SPACING_TIGHT);
    addLayoutToGroupBox(critterFlagsGroup, critterFlagsLayout);

    auto addFlagCheck = [&](QCheckBox*& checkBox, const char* label, const char* tooltip, int row, int column) {
        checkBox = new QCheckBox(label);
        checkBox->setToolTip(tooltip);
        this->connectCheckBox(checkBox);
        critterFlagsLayout->addWidget(checkBox, row, column);
    };

    addFlagCheck(_barterCheck, "Can Barter", "Can barter with this critter", 0, 0);
    addFlagCheck(_noStealCheck, "No Steal", "Cannot steal from this critter", 1, 0);
    addFlagCheck(_noDropCheck, "No Drop", "Cannot drop items", 2, 0);
    addFlagCheck(_noLimbsCheck, "No Limbs", "No limb damage", 3, 0);
    addFlagCheck(_noAgeCheck, "No Age", "Does not age", 4, 0);
    addFlagCheck(_noHealCheck, "No Heal", "Cannot heal", 5, 0);
    addFlagCheck(_invulnerableCheck, "Invulnerable", "Cannot be damaged", 0, 1);
    addFlagCheck(_noFlattenCheck, "No Flatten", "Doesn't flatten on death (leaves no dead body)", 1, 1);
    addFlagCheck(_specialDeathCheck, "Special Death", "Special death animation", 2, 1);
    addFlagCheck(_longLimbsCheck, "Long Limbs", "Has long limbs", 3, 1);
    addFlagCheck(_noKnockbackCheck, "No Knockback", "Cannot be knocked back", 4, 1);

    mainLayout->addWidget(critterFlagsGroup);
    mainLayout->addStretch();
    parentTabs->addTab(generalTab, "General");
}

void ProCritterWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro)) {
        return;
    }

    const auto& critterData = pro->critterData;
    _headFid = static_cast<int32_t>(critterData.headFID);
    updateHeadFidLabel();

    _aiPacketEdit->setValue(static_cast<int>(critterData.aiPacket));
    _teamNumberEdit->setValue(static_cast<int>(critterData.teamNumber));
    _barterCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_BARTER));
    _noStealCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_STEAL));
    _noDropCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_DROP));
    _noLimbsCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_LIMBS));
    _noAgeCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_AGE));
    _noHealCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_HEAL));
    _invulnerableCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_INVULNERABLE));
    _noFlattenCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_FLATTEN));
    _specialDeathCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_SPECIAL_DEATH));
    _longLimbsCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_LONG_LIMBS));
    _noKnockbackCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_KNOCKBACK));

    _maxHitPointsEdit->setValue(static_cast<int>(critterData.maxHitPoints));
    _actionPointsEdit->setValue(static_cast<int>(critterData.actionPoints));
    _armorClassEdit->setValue(static_cast<int>(critterData.armorClass));
    _meleeDamageEdit->setValue(static_cast<int>(critterData.meleeDamage));
    _carryWeightMaxEdit->setValue(static_cast<int>(critterData.carryWeightMax));
    _sequenceEdit->setValue(static_cast<int>(critterData.sequence));
    _healingRateEdit->setValue(static_cast<int>(critterData.healingRate));
    _criticalChanceEdit->setValue(static_cast<int>(critterData.criticalChance));
    _betterCriticalsEdit->setValue(static_cast<int>(critterData.betterCriticals));
    _ageEdit->setValue(static_cast<int>(critterData.age));
    _genderCombo->setCurrentIndex(static_cast<int>(critterData.gender));
    _bodyTypeCombo->setCurrentIndex(static_cast<int>(critterData.bodyType));
    _experienceEdit->setValue(static_cast<int>(critterData.experienceForKill));
    _killTypeEdit->setValue(static_cast<int>(critterData.killType));
    _damageTypeEdit->setValue(static_cast<int>(critterData.damageType));

    this->loadIntArrayToWidgets(_specialStatEdits, critterData.specialStats, NumSpecialStats);
    this->loadIntArrayToWidgets(_damageThresholdEdits, critterData.damageThreshold, NumDamageTypes);
    this->loadIntArrayToWidgets(_damageResistEdits, critterData.damageResist, NumDamageTypesWithRadiation);
    this->loadIntArrayToWidgets(_skillEdits, critterData.skills, NumSkills);
}

void ProCritterWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro)) {
        return;
    }

    auto& critterData = pro->critterData;
    critterData.headFID = static_cast<uint32_t>(_headFid);

    critterData.aiPacket = static_cast<uint32_t>(_aiPacketEdit->value());
    critterData.teamNumber = static_cast<uint32_t>(_teamNumberEdit->value());

    critterData.flags = currentCritterFlags();

    critterData.maxHitPoints = static_cast<uint32_t>(_maxHitPointsEdit->value());
    critterData.actionPoints = static_cast<uint32_t>(_actionPointsEdit->value());
    critterData.armorClass = static_cast<uint32_t>(_armorClassEdit->value());
    critterData.meleeDamage = static_cast<uint32_t>(_meleeDamageEdit->value());
    critterData.carryWeightMax = static_cast<uint32_t>(_carryWeightMaxEdit->value());
    critterData.sequence = static_cast<uint32_t>(_sequenceEdit->value());
    critterData.healingRate = static_cast<uint32_t>(_healingRateEdit->value());
    critterData.criticalChance = static_cast<uint32_t>(_criticalChanceEdit->value());
    critterData.betterCriticals = static_cast<uint32_t>(_betterCriticalsEdit->value());
    critterData.age = static_cast<uint32_t>(_ageEdit->value());
    critterData.gender = static_cast<uint32_t>(getComboIndex(_genderCombo));
    critterData.bodyType = static_cast<uint32_t>(getComboIndex(_bodyTypeCombo));
    critterData.experienceForKill = static_cast<uint32_t>(_experienceEdit->value());
    critterData.killType = static_cast<uint32_t>(_killTypeEdit->value());
    critterData.damageType = static_cast<uint32_t>(_damageTypeEdit->value());

    this->saveWidgetsToIntArray(_specialStatEdits, critterData.specialStats, NumSpecialStats);
    this->saveWidgetsToIntArray(_damageThresholdEdits, critterData.damageThreshold, NumDamageTypes);
    this->saveWidgetsToIntArray(_damageResistEdits, critterData.damageResist, NumDamageTypesWithRadiation);
    this->saveWidgetsToIntArray(_skillEdits, critterData.skills, NumSkills);
}

bool ProCritterWidget::canHandle(const std::shared_ptr<Pro>& pro) const {
    return pro && pro->type() == Pro::OBJECT_TYPE::CRITTER;
}

QString ProCritterWidget::getTabLabel() const {
    return "Critter";
}

void ProCritterWidget::updateHeadFidLabel() {
    if (!_headFidLabel) {
        return;
    }
    if (_headFid <= 0) {
        _headFidLabel->setText("No FRM");
        return;
    }

    const std::string frmPath = _resources.frmResolver().resolve(static_cast<unsigned int>(_headFid));
    if (frmPath.empty()) {
        _headFidLabel->setText(QString("Invalid FID (%1)").arg(_headFid));
        return;
    }

    _headFidLabel->setText(QString::fromStdString(std::filesystem::path(frmPath).filename().string()));
}

uint32_t ProCritterWidget::currentCritterFlags() const {
    uint32_t flags = 0;
    if (_barterCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_BARTER);
    }
    if (_noStealCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_STEAL);
    }
    if (_noDropCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_DROP);
    }
    if (_noLimbsCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_LIMBS);
    }
    if (_noAgeCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_AGE);
    }
    if (_noHealCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_HEAL);
    }
    if (_invulnerableCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_INVULNERABLE);
    }
    if (_noFlattenCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_FLATTEN);
    }
    if (_specialDeathCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_SPECIAL_DEATH);
    }
    if (_longLimbsCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_LONG_LIMBS);
    }
    if (_noKnockbackCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_KNOCKBACK);
    }
    return flags;
}

} // namespace geck
