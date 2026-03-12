#include "ProEditorDialog.h"
#include "MessageSelectorDialog.h"
#include "../theme/ThemeManager.h"
#include "../GameEnums.h"
#include "../UIConstants.h"

#include <QApplication>
#include <QMessageBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QTimer>
#include <QScrollArea>
#include <QPainter>
#include <filesystem>
#include <algorithm>
#include <spdlog/spdlog.h>

#include "../../writer/pro/ProWriter.h"
#include "../../format/frm/Frm.h"
#include "../../format/frm/Frame.h"
#include "../../format/pal/Pal.h"
#include "../../format/msg/Msg.h"
#include "../../resource/GameResources.h"
#include "../../util/ResourcePaths.h"
#include "FrmSelectorDialog.h"

#include <util/ProHelper.h>
#include <util/FrmThumbnailGenerator.h>

namespace geck {

ProEditorDialog::ProEditorDialog(resource::GameResources& resources, std::shared_ptr<Pro> pro, QWidget* parent)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _contentLayout(nullptr)
    , _tabWidget(nullptr)
    , _buttonBox(nullptr)
    , _previewGroup(nullptr)
    , _previewLabel(nullptr)
    , _objectPreviewWidget(nullptr)
    , _dualPreviewWidget(nullptr)
    , _dualPreviewLayout(nullptr)
    , _inventoryPreviewWidget(nullptr)
    , _groundPreviewWidget(nullptr)
    , _animationControls(nullptr)
    , _animationLayout(nullptr)
    , _playPauseButton(nullptr)
    , _frameSlider(nullptr)
    , _frameLabel(nullptr)
    , _directionCombo(nullptr)
    , _animationController(nullptr)
    , _commonTab(nullptr)
    , _commonFieldsWidget(nullptr)
    , _wallWidget(nullptr)
    , _tileWidget(nullptr)
    , _armorWidget(nullptr)
    , _weaponWidget(nullptr)
    , _drugWidget(nullptr)
    , _containerKeyWidget(nullptr)
    , _ammoWidget(nullptr)
    , _miscItemWidget(nullptr)
    , _nameLabel(nullptr)
    , _descriptionEdit(nullptr)
    , _editMessageButton(nullptr)
    , _pidEdit(nullptr)
    , _filenameEdit(nullptr)
    , _critterHeadFIDLabel(nullptr)
    , _critterHeadFIDSelectorButton(nullptr)
    , _pro(pro)
    , _resources(resources) {

    setWindowTitle("PRO Editor");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
    resize(ui::constants::dialog_sizes::PRO_EDITOR_WIDTH, ui::constants::dialog_sizes::PRO_EDITOR_HEIGHT);

    for (int i = 0; i < NUM_SKILLS; ++i) {
        _critterSkillEdits[i] = nullptr;
    }
    for (int i = 0; i < NUM_SPECIAL_STATS; ++i) {
        _critterSpecialStatEdits[i] = nullptr;
        _critterBonusSpecialStatEdits[i] = nullptr;
        _critterDamageThresholdEdits[i] = nullptr;
    }
    for (int i = 0; i < NUM_DAMAGE_TYPES_WITH_RADIATION; ++i) {
        _critterDamageResistEdits[i] = nullptr;
    }
    for (int i = 0; i < NUM_DAMAGE_TYPES_WITH_SPECIAL; ++i) {
        _critterBonusDamageThresholdEdits[i] = nullptr;
        _critterBonusDamageResistanceEdits[i] = nullptr;
    }

    _critterHeadFIDLabel = nullptr;
    _critterHeadFIDSelectorButton = nullptr;
    _critterAIPacketEdit = nullptr;
    _critterTeamNumberEdit = nullptr;
    _critterFlagsEdit = nullptr;
    _critterMaxHitPointsEdit = nullptr;
    _critterActionPointsEdit = nullptr;
    _critterArmorClassEdit = nullptr;
    _critterMeleeDamageEdit = nullptr;
    _critterCarryWeightMaxEdit = nullptr;
    _critterSequenceEdit = nullptr;
    _critterHealingRateEdit = nullptr;
    _critterCriticalChanceEdit = nullptr;
    _critterBetterCriticalsEdit = nullptr;
    _critterAgeEdit = nullptr;
    _critterGenderCombo = nullptr;
    _critterBodyTypeCombo = nullptr;
    _critterExperienceEdit = nullptr;
    _critterKillTypeEdit = nullptr;
    _critterDamageTypeEdit = nullptr;
    _critterBonusHealthPointsEdit = nullptr;
    _critterBonusActionPointsEdit = nullptr;
    _critterBonusArmorClassEdit = nullptr;
    _critterBonusMeleeDamageEdit = nullptr;
    _critterBonusCarryWeightEdit = nullptr;
    _critterBonusSequenceEdit = nullptr;
    _critterBonusHealingRateEdit = nullptr;
    _critterBonusCriticalChanceEdit = nullptr;
    _critterBonusBetterCriticalsEdit = nullptr;
    _critterBonusAgeEdit = nullptr;
    _critterBonusGenderEdit = nullptr;

    // Initialize critter flag checkboxes to nullptr
    _critterBarterCheck = nullptr;
    _critterNoStealCheck = nullptr;
    _critterNoDropCheck = nullptr;
    _critterNoLimbsCheck = nullptr;
    _critterNoAgeCheck = nullptr;
    _critterNoHealCheck = nullptr;
    _critterInvulnerableCheck = nullptr;
    _critterNoFlattenCheck = nullptr;
    _critterSpecialDeathCheck = nullptr;
    _critterLongLimbsCheck = nullptr;
    _critterNoKnockbackCheck = nullptr;

    loadStatAndPerkNames();

    setupUI();

    loadProData();

    loadNameAndDescription();

    updateFilenameLabel();

    updateWindowTitle();

    QTimer::singleShot(0, this, &ProEditorDialog::updatePreview);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    adjustSize();
}

void ProEditorDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);

    // Create horizontal layout: Left Info Panel | Right Type-Specific Fields
    _contentLayout = new QHBoxLayout();

    // === LEFT PANEL: Image + Name + Description + Common Fields ===
    QWidget* leftInfoPanel = new QWidget();
    leftInfoPanel->setFixedWidth(ui::constants::sizes::WIDTH_INFO_PANEL); // 10% smaller than previous 320px
    QVBoxLayout* leftInfoLayout = new QVBoxLayout(leftInfoPanel);
    leftInfoLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    leftInfoLayout->setSpacing(0); // Remove all space between elements

    // Name with edit button above preview
    auto nameLayout = new QHBoxLayout();
    nameLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _nameLabel = new QLabel(this);
    _nameLabel->setAlignment(Qt::AlignCenter);
    _nameLabel->setWordWrap(true);
    _nameLabel->setStyleSheet(ui::theme::styles::titleLabel());
    nameLayout->addWidget(_nameLabel); // Take most of the space

    // Small edit button next to name
    _editMessageButton = new QPushButton("...", this);
    _editMessageButton->setMaximumWidth(ui::constants::sizes::ICON_BUTTON);
    _editMessageButton->setMaximumHeight(ui::constants::sizes::ICON_BUTTON);
    _editMessageButton->setToolTip("Edit object name and description");
    connect(_editMessageButton, &QPushButton::clicked, this, &ProEditorDialog::onEditMessageClicked);
    nameLayout->addWidget(_editMessageButton); // Fixed size

    leftInfoLayout->addLayout(nameLayout);

    // Setup compact preview at top
    setupCompactPreview(leftInfoLayout);

    // Description under preview (without prefix)
    _descriptionEdit = new QTextEdit(this);
    _descriptionEdit->setFixedHeight(ui::constants::sizes::HEIGHT_DESCRIPTION);
    _descriptionEdit->setReadOnly(true);
    _descriptionEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _descriptionEdit->setStyleSheet(ui::theme::styles::textAreaReadOnly());
    leftInfoLayout->addWidget(_descriptionEdit);

    // PID field (Object Type & ID)
    auto pidLayout = new QHBoxLayout();
    pidLayout->addWidget(new QLabel("PID (hex):", this));
    _pidEdit = new QSpinBox(this);
    _pidEdit->setRange(0, 0x5FFFFFF); // 24-bit object ID limit
    _pidEdit->setDisplayIntegerBase(16);
    _pidEdit->setToolTip("Object ID and Type (combined 32-bit value)");
    _pidEdit->setButtonSymbols(QAbstractSpinBox::NoButtons); // Remove up/down arrows
    _pidEdit->setMinimumWidth(ui::constants::sizes::WIDTH_PID_FIELD_MIN);  // Set consistent width
    connect(_pidEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    pidLayout->addWidget(_pidEdit);
    leftInfoLayout->addLayout(pidLayout);

    // Filename field (non-editable, like PID field)
    auto filenameLayout = new QHBoxLayout();
    filenameLayout->addWidget(new QLabel("PID (filename):", this));
    _filenameEdit = new QLineEdit(this);
    _filenameEdit->setReadOnly(true);
    _filenameEdit->setToolTip("PRO filename derived from PID");
    _filenameEdit->setStyleSheet(ui::theme::styles::readOnlyInput());
    _filenameEdit->setMinimumWidth(ui::constants::sizes::WIDTH_PID_FIELD_MIN); // Match PID field width
    filenameLayout->addWidget(_filenameEdit);
    leftInfoLayout->addLayout(filenameLayout);

    leftInfoLayout->addStretch(); // Push widgets to top while dialog size policy keeps it compact

    // === RIGHT PANEL: Tabbed Interface ===
    _tabWidget = new QTabWidget(this);
    _tabWidget->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);

    // Add main panels to content layout
    _contentLayout->addWidget(leftInfoPanel, 0); // Fixed width
    _contentLayout->addWidget(_tabWidget, 1);    // Flexible width

    // Setup tabbed content
    setupTabs();

    // Button box
    _buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(_buttonBox, &QDialogButtonBox::accepted, this, &ProEditorDialog::onAccept);
    connect(_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    _mainLayout->addLayout(_contentLayout);

    _mainLayout->addWidget(_buttonBox);
}

void ProEditorDialog::setupCompactPreview(QVBoxLayout* parentLayout) {
    QWidget* previewGroup = new QWidget();
    previewGroup->setContentsMargins(0, 0, 0, 0);
    previewGroup->setStyleSheet(ui::theme::styles::compactWidget());

    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(0);
    previewLayout->setAlignment(Qt::AlignCenter);

    bool hasSpecializedPreview = (_pro && _pro->type() == Pro::OBJECT_TYPE::ITEM);

    if (!hasSpecializedPreview) {
        _objectPreviewWidget = new ObjectPreviewWidget(_resources);

        connect(_objectPreviewWidget, &ObjectPreviewWidget::fidChangeRequested,
            this, &ProEditorDialog::onObjectFidChangeRequested);
        connect(_objectPreviewWidget, &ObjectPreviewWidget::fidChanged,
            this, &ProEditorDialog::onObjectFidChanged);

        previewLayout->addWidget(_objectPreviewWidget);
    }

    if (hasSpecializedPreview) {
        setupDualPreviewCompact(previewLayout);
    }

    parentLayout->addWidget(previewGroup);
}

void ProEditorDialog::setupDualPreviewCompact(QVBoxLayout* parentLayout) {
    QWidget* dualWidget = new QWidget();
    dualWidget->setStyleSheet(ui::theme::styles::compactWidget());
    QHBoxLayout* dualLayout = new QHBoxLayout(dualWidget);
    dualLayout->setContentsMargins(0, 0, 0, 0);
    dualLayout->setSpacing(ui::constants::SPACING_TIGHT);
    dualLayout->setAlignment(Qt::AlignCenter);

    _inventoryPreviewWidget = new ObjectPreviewWidget(_resources, this,
        ObjectPreviewWidget::PreviewOptions(),
        QSize(PREVIEW_ITEM_SIZE, PREVIEW_ITEM_SIZE));

    _groundPreviewWidget = new ObjectPreviewWidget(_resources, this,
        ObjectPreviewWidget::PreviewOptions(),
        QSize(PREVIEW_ITEM_SIZE, PREVIEW_ITEM_SIZE));

    connect(_inventoryPreviewWidget, &ObjectPreviewWidget::fidChangeRequested,
        this, &ProEditorDialog::onObjectFidChangeRequested);

    connect(_groundPreviewWidget, &ObjectPreviewWidget::fidChangeRequested,
        this, &ProEditorDialog::onObjectFidChangeRequested);

    dualLayout->addWidget(_inventoryPreviewWidget);
    dualLayout->addWidget(_groundPreviewWidget);

    parentLayout->addWidget(dualWidget);
}

void ProEditorDialog::setupTabs() {
    setupCommonTab();
    setupTypeSpecificTabs();
}

void ProEditorDialog::setupCommonTab() {
    _commonTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(_commonTab);
    layout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    layout->setSpacing(ui::constants::SPACING_FORM);

    _commonFieldsWidget = new ProCommonFieldsWidget(_resources, this);
    layout->addWidget(_commonFieldsWidget);

    connect(_commonFieldsWidget, &ProCommonFieldsWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);

    _tabWidget->addTab(_commonTab, "Common");
}

void ProEditorDialog::setupTypeSpecificTabs() {
    if (!_pro)
        return;

    Pro::OBJECT_TYPE type = _pro->type();

    switch (type) {
        case Pro::OBJECT_TYPE::ITEM:
            setupItemTabs();
            break;
        case Pro::OBJECT_TYPE::CRITTER:
            setupCritterTab();
            break;
        case Pro::OBJECT_TYPE::SCENERY:
            setupSceneryTab();
            break;
        case Pro::OBJECT_TYPE::WALL:
            setupWallTab();
            break;
        case Pro::OBJECT_TYPE::TILE:
            setupTileTab();
            break;
        case Pro::OBJECT_TYPE::MISC:
            setupMiscTab();
            break;
    }
}

void ProEditorDialog::setupItemTabs() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM)
        return;

    Pro::ITEM_TYPE itemType = _pro->itemType();

    switch (itemType) {
        case Pro::ITEM_TYPE::ARMOR:
            setupArmorTab();
            break;
        case Pro::ITEM_TYPE::CONTAINER:
        case Pro::ITEM_TYPE::KEY:
            setupContainerKeyTab();
            break;
        case Pro::ITEM_TYPE::DRUG:
            setupDrugTab();
            break;
        case Pro::ITEM_TYPE::WEAPON:
            setupWeaponTab();
            break;
        case Pro::ITEM_TYPE::AMMO:
        case Pro::ITEM_TYPE::MISC:
            setupAmmoMiscTab();
            break;
    }
}

void ProEditorDialog::setupCritterTab() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::CRITTER)
        return;

    // Create main critter tab with nested tabs
    QWidget* critterTab = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(critterTab);
    mainLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    mainLayout->setSpacing(ui::constants::SPACING_FORM);

    QTabWidget* critterSubTabs = new QTabWidget();
    critterSubTabs->setTabPosition(QTabWidget::North);

    setupCritterStatsTab(critterSubTabs);
    setupCritterDefenceTab(critterSubTabs);
    setupCritterGeneralTab(critterSubTabs);

    mainLayout->addWidget(critterSubTabs);
    _tabWidget->addTab(critterTab, "Critter");
}

void ProEditorDialog::setupCritterStatsTab(QTabWidget* parentTabs) {
    QWidget* statsTab = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(statsTab);
    mainLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    mainLayout->setSpacing(ui::constants::SPACING_FORM);

    // Create two-column layout for SPECIAL and Skills
    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->setSpacing(ui::constants::SPACING_COLUMNS);

    // === LEFT COLUMN: SPECIAL Stats ===
    QGroupBox* specialGroup = new QGroupBox("SPECIAL Stats");
    QGridLayout* specialLayout = new QGridLayout(specialGroup);
    specialLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    specialLayout->setSpacing(ui::constants::SPACING_TIGHT);

    const char* specialNames[] = { "STR", "PER", "END", "CHR", "INT", "AGL", "LCK" };
    for (int i = 0; i < 7; ++i) {
        _critterSpecialStatEdits[i] = new QSpinBox();
        _critterSpecialStatEdits[i]->setRange(MIN_SPECIAL_STAT, MAX_SPECIAL_STAT);
        _critterSpecialStatEdits[i]->setToolTip(QString("Base %1 stat").arg(specialNames[i]));
        connect(_critterSpecialStatEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);

        QLabel* label = new QLabel(specialNames[i]);
        label->setFixedWidth(ui::constants::sizes::LABEL_FRAME);
        specialLayout->addWidget(label, i / 4, (i % 4) * 2);
        specialLayout->addWidget(_critterSpecialStatEdits[i], i / 4, (i % 4) * 2 + 1);
    }

    // === RIGHT COLUMN: Skills ===
    QGroupBox* skillsGroup = new QGroupBox("Skills");
    QGridLayout* skillsLayout = new QGridLayout(skillsGroup);
    skillsLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    skillsLayout->setSpacing(ui::constants::SPACING_TIGHT);

    // Headers
    QLabel* skillHeader = new QLabel("Skill");
    skillHeader->setStyleSheet(ui::theme::styles::boldLabel());
    skillsLayout->addWidget(skillHeader, 0, 0);

    QLabel* valueHeader = new QLabel("Value (%)");
    valueHeader->setStyleSheet(ui::theme::styles::boldLabel());
    valueHeader->setAlignment(Qt::AlignCenter);
    skillsLayout->addWidget(valueHeader, 0, 1);

    QLabel* skillHeader2 = new QLabel("Skill");
    skillHeader2->setStyleSheet(ui::theme::styles::boldLabel());
    skillsLayout->addWidget(skillHeader2, 0, 2);

    QLabel* valueHeader2 = new QLabel("Value (%)");
    valueHeader2->setStyleSheet(ui::theme::styles::boldLabel());
    valueHeader2->setAlignment(Qt::AlignCenter);
    skillsLayout->addWidget(valueHeader2, 0, 3);

    QLabel* skillHeader3 = new QLabel("Skill");
    skillHeader3->setStyleSheet(ui::theme::styles::boldLabel());
    skillsLayout->addWidget(skillHeader3, 0, 4);

    QLabel* valueHeader3 = new QLabel("Value (%)");
    valueHeader3->setStyleSheet(ui::theme::styles::boldLabel());
    valueHeader3->setAlignment(Qt::AlignCenter);
    skillsLayout->addWidget(valueHeader3, 0, 5);

    // Skill names based on F2_ProtoManager reference
    const char* skillNames[] = {
        "Small Guns", "Big Guns", "Energy Weapons", "Unarmed", "Melee Weapons", "Throwing",
        "First Aid", "Doctor", "Sneak", "Lockpick", "Steal", "Traps",
        "Science", "Repair", "Speech", "Barter", "Gambling", "Outdoorsman"
    };

    // Create skill controls in 3 columns (6 skills per column)
    for (int i = 0; i < 18; ++i) {
        int column = i / 6;    // 0, 1, or 2
        int row = (i % 6) + 1; // 1-6

        // Skill name label
        QLabel* skillLabel = new QLabel(QString(skillNames[i]) + ":");
        skillLabel->setFixedWidth(ui::constants::sizes::WIDTH_LABEL_SKILL);
        skillsLayout->addWidget(skillLabel, row, column * 2);

        // Skill value spinbox
        _critterSkillEdits[i] = new QSpinBox();
        _critterSkillEdits[i]->setRange(0, MAX_SKILL_PERCENT); // 0-300%
        _critterSkillEdits[i]->setToolTip(QString("%1 skill percentage").arg(skillNames[i]));
        _critterSkillEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        connect(_critterSkillEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        skillsLayout->addWidget(_critterSkillEdits[i], row, column * 2 + 1);
    }

    topLayout->addWidget(specialGroup);
    topLayout->addWidget(skillsGroup, 1); // Give skills more space
    mainLayout->addLayout(topLayout);

    // === BOTTOM: Derived Combat Stats ===
    QGroupBox* combatGroup = new QGroupBox("Combat Stats");
    QFormLayout* combatLayout = new QFormLayout(combatGroup);
    combatLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    combatLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _critterMaxHitPointsEdit = new QSpinBox();
    _critterMaxHitPointsEdit->setRange(1, INT_MAX);
    _critterMaxHitPointsEdit->setToolTip("Maximum hit points");
    connect(_critterMaxHitPointsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Max HP:", _critterMaxHitPointsEdit);

    _critterActionPointsEdit = new QSpinBox();
    _critterActionPointsEdit->setRange(1, INT_MAX);
    _critterActionPointsEdit->setToolTip("Action points per turn");
    connect(_critterActionPointsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Action Points:", _critterActionPointsEdit);

    _critterArmorClassEdit = new QSpinBox();
    _critterArmorClassEdit->setRange(0, INT_MAX);
    _critterArmorClassEdit->setToolTip("Base armor class");
    connect(_critterArmorClassEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Armor Class:", _critterArmorClassEdit);

    _critterMeleeDamageEdit = new QSpinBox();
    _critterMeleeDamageEdit->setRange(0, INT_MAX);
    _critterMeleeDamageEdit->setToolTip("Melee damage bonus");
    connect(_critterMeleeDamageEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Melee Damage:", _critterMeleeDamageEdit);

    _critterCarryWeightMaxEdit = new QSpinBox();
    _critterCarryWeightMaxEdit->setRange(0, INT_MAX);
    _critterCarryWeightMaxEdit->setToolTip("Maximum carry weight");
    connect(_critterCarryWeightMaxEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Carry Weight:", _critterCarryWeightMaxEdit);

    _critterSequenceEdit = new QSpinBox();
    _critterSequenceEdit->setRange(0, INT_MAX);
    _critterSequenceEdit->setToolTip("Initiative sequence bonus");
    connect(_critterSequenceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Sequence:", _critterSequenceEdit);

    _critterHealingRateEdit = new QSpinBox();
    _critterHealingRateEdit->setRange(0, INT_MAX);
    _critterHealingRateEdit->setToolTip("Healing rate bonus");
    connect(_critterHealingRateEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Healing Rate:", _critterHealingRateEdit);

    _critterCriticalChanceEdit = new QSpinBox();
    _critterCriticalChanceEdit->setRange(0, INT_MAX);
    _critterCriticalChanceEdit->setToolTip("Critical hit chance bonus");
    connect(_critterCriticalChanceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Critical Chance:", _critterCriticalChanceEdit);

    _critterBetterCriticalsEdit = new QSpinBox();
    _critterBetterCriticalsEdit->setRange(0, INT_MAX);
    _critterBetterCriticalsEdit->setToolTip("Better criticals bonus");
    connect(_critterBetterCriticalsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Better Criticals:", _critterBetterCriticalsEdit);

    mainLayout->addWidget(combatGroup);
    mainLayout->addStretch(); // Push content to top

    parentTabs->addTab(statsTab, "Stats");
}

void ProEditorDialog::setupCritterDefenceTab(QTabWidget* parentTabs) {
    QWidget* defenceTab = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(defenceTab);
    mainLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    mainLayout->setSpacing(ui::constants::SPACING_FORM);

    // Damage Protection (unified resistance and threshold)
    QGroupBox* damageProtectionGroup = new QGroupBox("Damage Protection");
    QGridLayout* damageProtectionLayout = new QGridLayout(damageProtectionGroup);
    damageProtectionLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    damageProtectionLayout->setSpacing(ui::constants::SPACING_GRID);

    // Headers
    QLabel* thresholdHeader = new QLabel("Threshold");
    thresholdHeader->setStyleSheet(ui::theme::styles::boldLabel());
    thresholdHeader->setAlignment(Qt::AlignCenter);
    damageProtectionLayout->addWidget(thresholdHeader, 0, 1);

    QLabel* resistanceHeader = new QLabel("Resistance");
    resistanceHeader->setStyleSheet(ui::theme::styles::boldLabel());
    resistanceHeader->setAlignment(Qt::AlignCenter);
    damageProtectionLayout->addWidget(resistanceHeader, 0, 2);

    const QStringList damageTypes = game::enums::damageTypes9(_resources);

    // First 7 damage types (threshold + resistance)
    for (int i = 0; i < 7; ++i) {
        // Damage type label
        QLabel* typeLabel = new QLabel(damageTypes.at(i) + ":");
        typeLabel->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(typeLabel, i + 1, 0);

        // Damage threshold
        _critterDamageThresholdEdits[i] = new QSpinBox();
        _critterDamageThresholdEdits[i]->setRange(0, INT_MAX);
        _critterDamageThresholdEdits[i]->setToolTip(QString("%1 damage threshold").arg(damageTypes.at(i)));
        _critterDamageThresholdEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        connect(_critterDamageThresholdEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        damageProtectionLayout->addWidget(_critterDamageThresholdEdits[i], i + 1, 1);

        // Damage resistance
        _critterDamageResistEdits[i] = new QSpinBox();
        _critterDamageResistEdits[i]->setRange(0, INT_MAX);
        _critterDamageResistEdits[i]->setToolTip(QString("%1 damage resistance").arg(damageTypes.at(i)));
        _critterDamageResistEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        connect(_critterDamageResistEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        damageProtectionLayout->addWidget(_critterDamageResistEdits[i], i + 1, 2);
    }

    // Last 2 damage types (Radiation and Poison - resistance only)
    for (int i = 7; i < 9; ++i) {
        QLabel* typeLabel = new QLabel(damageTypes.at(i) + ":");
        typeLabel->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(typeLabel, i + 1, 0);

        // No threshold for Radiation and Poison, add placeholder
        QLabel* placeholder = new QLabel("—");
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet(ui::theme::styles::placeholderText());
        placeholder->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(placeholder, i + 1, 1);

        // Damage resistance
        _critterDamageResistEdits[i] = new QSpinBox();
        _critterDamageResistEdits[i]->setRange(0, INT_MAX);
        _critterDamageResistEdits[i]->setToolTip(QString("%1 damage resistance").arg(damageTypes.at(i)));
        _critterDamageResistEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        connect(_critterDamageResistEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        damageProtectionLayout->addWidget(_critterDamageResistEdits[i], i + 1, 2);
    }

    mainLayout->addWidget(damageProtectionGroup);
    mainLayout->addStretch(); // Push content to top

    parentTabs->addTab(defenceTab, "Defence");
}

void ProEditorDialog::setupCritterGeneralTab(QTabWidget* parentTabs) {
    QWidget* generalTab = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(generalTab);
    mainLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    mainLayout->setSpacing(ui::constants::SPACING_FORM);

    // Create two-column layout
    QHBoxLayout* columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(ui::constants::SPACING_COLUMNS);

    // === LEFT COLUMN ===
    QWidget* leftColumn = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(ui::constants::SPACING_NORMAL);

    // Critter Properties
    QGroupBox* critterGroup = new QGroupBox("Critter Properties");
    QFormLayout* critterLayout = new QFormLayout(critterGroup);
    critterLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    critterLayout->setSpacing(ui::constants::SPACING_TIGHT);

    // Head FID with selector button
    QWidget* headFidWidget = new QWidget();
    QHBoxLayout* headFidLayout = new QHBoxLayout(headFidWidget);
    headFidLayout->setContentsMargins(0, 0, 0, 0);
    headFidLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _critterHeadFIDLabel = new QLabel("No FRM");
    _critterHeadFIDLabel->setToolTip("FRM filename for critter head appearance");
    _critterHeadFIDLabel->setStyleSheet(ui::theme::styles::borderedLabel());

    _critterHeadFIDSelectorButton = new QPushButton("...");
    _critterHeadFIDSelectorButton->setMaximumWidth(ui::constants::sizes::ICON_BUTTON);
    _critterHeadFIDSelectorButton->setMaximumHeight(ui::constants::sizes::ICON_BUTTON_HEIGHT);
    _critterHeadFIDSelectorButton->setToolTip("Browse FRM files for critter head");
    connect(_critterHeadFIDSelectorButton, &QPushButton::clicked, this, &ProEditorDialog::onCritterHeadFidSelectorClicked);

    headFidLayout->addWidget(_critterHeadFIDLabel, 1);
    headFidLayout->addWidget(_critterHeadFIDSelectorButton);
    critterLayout->addRow("Head FID:", headFidWidget);

    _critterAIPacketEdit = createSpinBox(0, INT_MAX, "AI packet number for critter behavior");
    connectSpinBox(_critterAIPacketEdit);
    critterLayout->addRow("AI Packet:", _critterAIPacketEdit);

    _critterTeamNumberEdit = createSpinBox(0, INT_MAX, "Team number for faction identification");
    connectSpinBox(_critterTeamNumberEdit);
    critterLayout->addRow("Team Number:", _critterTeamNumberEdit);

    leftLayout->addWidget(critterGroup);

    // === RIGHT COLUMN ===
    QWidget* rightColumn = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(ui::constants::SPACING_NORMAL);

    // Character Information
    QGroupBox* charGroup = new QGroupBox("Character Information");
    QFormLayout* charLayout = new QFormLayout(charGroup);
    charLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    charLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _critterAgeEdit = new QSpinBox();
    _critterAgeEdit->setRange(MIN_AGE, MAX_AGE);
    _critterAgeEdit->setToolTip("Critter age");
    connect(_critterAgeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    charLayout->addRow("Age:", _critterAgeEdit);

    _critterGenderCombo = new QComboBox();
    _critterGenderCombo->addItems(game::enums::critterGenders());
    _critterGenderCombo->setToolTip("Critter gender");
    connect(_critterGenderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    charLayout->addRow("Gender:", _critterGenderCombo);

    _critterBodyTypeCombo = new QComboBox();
    _critterBodyTypeCombo->addItems(game::enums::critterBodyTypes(_resources));
    _critterBodyTypeCombo->setToolTip("Body type for animations");
    connect(_critterBodyTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    charLayout->addRow("Body Type:", _critterBodyTypeCombo);

    _critterExperienceEdit = new QSpinBox();
    _critterExperienceEdit->setRange(0, INT_MAX);
    _critterExperienceEdit->setToolTip("Experience points for killing this critter");
    connect(_critterExperienceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    charLayout->addRow("Experience:", _critterExperienceEdit);

    _critterKillTypeEdit = new QSpinBox();
    _critterKillTypeEdit->setRange(0, INT_MAX);
    _critterKillTypeEdit->setToolTip("Kill type for karma tracking");
    connect(_critterKillTypeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    charLayout->addRow("Kill Type:", _critterKillTypeEdit);

    _critterDamageTypeEdit = new QSpinBox();
    _critterDamageTypeEdit->setRange(0, INT_MAX);
    _critterDamageTypeEdit->setToolTip("Default damage type");
    connect(_critterDamageTypeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    charLayout->addRow("Damage Type:", _critterDamageTypeEdit);

    rightLayout->addWidget(charGroup);

    columnsLayout->addWidget(leftColumn);
    columnsLayout->addWidget(rightColumn);
    mainLayout->addLayout(columnsLayout);

    // === BOTTOM: Critter Flags ===
    QGroupBox* critterFlagsGroup = new QGroupBox("Critter Flags");
    QGridLayout* critterFlagsLayout = new QGridLayout(critterFlagsGroup);
    critterFlagsLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    critterFlagsLayout->setSpacing(ui::constants::SPACING_TIGHT);

    // Column 1 (left)
    _critterBarterCheck = new QCheckBox("Can Barter");
    _critterBarterCheck->setToolTip("Can barter with this critter");
    connect(_critterBarterCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterBarterCheck, 0, 0);

    _critterNoStealCheck = new QCheckBox("No Steal");
    _critterNoStealCheck->setToolTip("Cannot steal from this critter");
    connect(_critterNoStealCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoStealCheck, 1, 0);

    _critterNoDropCheck = new QCheckBox("No Drop");
    _critterNoDropCheck->setToolTip("Cannot drop items");
    connect(_critterNoDropCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoDropCheck, 2, 0);

    _critterNoLimbsCheck = new QCheckBox("No Limbs");
    _critterNoLimbsCheck->setToolTip("No limb damage");
    connect(_critterNoLimbsCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoLimbsCheck, 3, 0);

    _critterNoAgeCheck = new QCheckBox("No Age");
    _critterNoAgeCheck->setToolTip("Does not age");
    connect(_critterNoAgeCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoAgeCheck, 4, 0);

    _critterNoHealCheck = new QCheckBox("No Heal");
    _critterNoHealCheck->setToolTip("Cannot heal");
    connect(_critterNoHealCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoHealCheck, 5, 0);

    // Column 2 (right)
    _critterInvulnerableCheck = new QCheckBox("Invulnerable");
    _critterInvulnerableCheck->setToolTip("Cannot be damaged");
    connect(_critterInvulnerableCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterInvulnerableCheck, 0, 1);

    _critterNoFlattenCheck = new QCheckBox("No Flatten");
    _critterNoFlattenCheck->setToolTip("Doesn't flatten on death (leaves no dead body)");
    connect(_critterNoFlattenCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoFlattenCheck, 1, 1);

    _critterSpecialDeathCheck = new QCheckBox("Special Death");
    _critterSpecialDeathCheck->setToolTip("Special death animation");
    connect(_critterSpecialDeathCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterSpecialDeathCheck, 2, 1);

    _critterLongLimbsCheck = new QCheckBox("Long Limbs");
    _critterLongLimbsCheck->setToolTip("Has long limbs");
    connect(_critterLongLimbsCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterLongLimbsCheck, 3, 1);

    _critterNoKnockbackCheck = new QCheckBox("No Knockback");
    _critterNoKnockbackCheck->setToolTip("Cannot be knocked back");
    connect(_critterNoKnockbackCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoKnockbackCheck, 4, 1);

    mainLayout->addWidget(critterFlagsGroup);
    mainLayout->addStretch(); // Push content to top

    parentTabs->addTab(generalTab, "General");
}

void ProEditorDialog::setupSceneryTab() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::SCENERY)
        return;

    QWidget* sceneryTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(sceneryTab);
    layout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    layout->setSpacing(ui::constants::SPACING_FORM);

    // Set temporary layout pointers for setupSceneryFields
    _leftFieldsLayout = layout;
    _rightFieldsLayout = nullptr;

    // Use existing scenery fields setup
    setupSceneryFields();

    _tabWidget->addTab(sceneryTab, "Scenery");
}

void ProEditorDialog::setupWallTab() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::WALL)
        return;

    // Use the new ProWallWidget
    _wallWidget = new ProWallWidget(_resources);
    connect(_wallWidget, &ProWallWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);

    _tabWidget->addTab(_wallWidget, _wallWidget->getTabLabel());
}

void ProEditorDialog::setupTileTab() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::TILE)
        return;

    // Use the new ProTileWidget
    _tileWidget = new ProTileWidget(_resources);
    connect(_tileWidget, &ProTileWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);

    _tabWidget->addTab(_tileWidget, _tileWidget->getTabLabel());
}

void ProEditorDialog::setupMiscTab() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::MISC)
        return;

    QWidget* miscTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(miscTab);
    layout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    layout->setSpacing(ui::constants::SPACING_FORM);

    // Set temporary layout pointers for setupMiscFields
    _leftFieldsLayout = layout;
    _rightFieldsLayout = nullptr;

    // Use existing misc fields setup
    setupMiscFields();

    _tabWidget->addTab(miscTab, "Misc");
}

void ProEditorDialog::setupArmorTab() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM || _pro->itemType() != Pro::ITEM_TYPE::ARMOR)
        return;

    // Use the new ProArmorWidget
    _armorWidget = new ProArmorWidget(_resources);
    connect(_armorWidget, &ProArmorWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);

    _tabWidget->addTab(_armorWidget, _armorWidget->getTabLabel());
}

void ProEditorDialog::setupDrugTab() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM || _pro->itemType() != Pro::ITEM_TYPE::DRUG)
        return;

    // Use the new ProDrugWidget
    _drugWidget = new ProDrugWidget(_resources);
    connect(_drugWidget, &ProDrugWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);

    // Set stat and perk names from MSG files
    _drugWidget->setStatNames(_statNames);
    _drugWidget->setPerkOptions(_perkOptions);

    _tabWidget->addTab(_drugWidget, _drugWidget->getTabLabel());
}

void ProEditorDialog::setupWeaponTab() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM || _pro->itemType() != Pro::ITEM_TYPE::WEAPON)
        return;

    // Use the new ProWeaponWidget
    _weaponWidget = new ProWeaponWidget(_resources);
    connect(_weaponWidget, &ProWeaponWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);

    _tabWidget->addTab(_weaponWidget, _weaponWidget->getTabLabel());
}

void ProEditorDialog::setupAmmoMiscTab() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM)
        return;

    Pro::ITEM_TYPE itemType = _pro->itemType();
    if (itemType != Pro::ITEM_TYPE::AMMO && itemType != Pro::ITEM_TYPE::MISC)
        return;

    if (itemType == Pro::ITEM_TYPE::AMMO) {
        _ammoWidget = new ProAmmoWidget(_resources);
        connect(_ammoWidget, &ProAmmoWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);
        _tabWidget->addTab(_ammoWidget, _ammoWidget->getTabLabel());
        return;
    }

    _miscItemWidget = new ProMiscItemWidget(_resources);
    connect(_miscItemWidget, &ProMiscItemWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);
    _tabWidget->addTab(_miscItemWidget, _miscItemWidget->getTabLabel());
}

void ProEditorDialog::setupContainerKeyTab() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM)
        return;

    Pro::ITEM_TYPE itemType = _pro->itemType();
    if (itemType != Pro::ITEM_TYPE::CONTAINER && itemType != Pro::ITEM_TYPE::KEY)
        return;

    // Use the new ProContainerKeyWidget
    _containerKeyWidget = new ProContainerKeyWidget(_resources);
    connect(_containerKeyWidget, &ProContainerKeyWidget::fieldChanged, this, &ProEditorDialog::onFieldChanged);

    _tabWidget->addTab(_containerKeyWidget, _containerKeyWidget->getTabLabel());
}

void ProEditorDialog::setupAnimationControls() {
    // Create animation controller
    _animationController = new AnimationController(this);

    // Animation controls
    _animationControls = new QWidget();
    _animationLayout = new QHBoxLayout(_animationControls);
    _animationLayout->setContentsMargins(0, 0, 0, 0);

    // Direction selection
    _directionCombo = new QComboBox();
    _directionCombo->addItems(game::enums::orientationsShort());
    _directionCombo->setToolTip("Select animation direction");
    _animationLayout->addWidget(new QLabel("Direction:"));
    _animationLayout->addWidget(_directionCombo);

    _animationLayout->addSpacing(LAYOUT_SPACING);

    // Play/pause button
    _playPauseButton = new QPushButton("▶");
    _playPauseButton->setMaximumWidth(ui::constants::sizes::WIDTH_PLAY_BUTTON);
    _playPauseButton->setToolTip("Play/Pause animation");
    _animationLayout->addWidget(_playPauseButton);

    // Frame slider
    _frameSlider = new QSlider(Qt::Horizontal);
    _frameSlider->setMinimum(0);
    _frameSlider->setMaximum(0);
    _frameSlider->setToolTip("Select frame");
    _animationLayout->addWidget(_frameSlider);

    // Frame label
    _frameLabel = new QLabel("0/0");
    _frameLabel->setMinimumWidth(ui::constants::sizes::LABEL_FRAME_WIDE);
    _animationLayout->addWidget(_frameLabel);

    // Connect UI signals
    connect(_playPauseButton, &QPushButton::clicked, this, &ProEditorDialog::onPlayPauseClicked);
    connect(_frameSlider, &QSlider::valueChanged, this, &ProEditorDialog::onFrameSliderChanged);
    connect(_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onDirectionChanged);

    // Connect animation controller signals
    connect(_animationController, &AnimationController::frameChanged, this, &ProEditorDialog::onAnimationFrameChanged);
    connect(_animationController, &AnimationController::playStateChanged, this, [this](bool playing) {
        _playPauseButton->setText(playing ? "⏸" : "▶");
        _playPauseButton->setToolTip(playing ? "Pause animation" : "Play animation");
    });

    // Initially disable controls
    _animationControls->setEnabled(false);
}

void ProEditorDialog::loadProData() {
    try {
        if (_pidEdit) {
            _pidEdit->setValue(_pro->header.PID);
        }

        if (_commonFieldsWidget) {
            _commonFieldsWidget->loadFromPro(_pro);
            bool isItem = (_pro->type() == Pro::OBJECT_TYPE::ITEM);
            _commonFieldsWidget->setItemFieldsVisible(isItem);
        }

    } catch (const std::exception& e) {
        spdlog::error("ProEditorDialog::loadProData() - exception loading common data: {}", e.what());
        throw;
    }

    switch (_pro->type()) {
        case Pro::OBJECT_TYPE::ITEM:
            if (_armorWidget) {
                _armorWidget->loadFromPro(_pro);
            } else if (_containerKeyWidget) {
                _containerKeyWidget->loadFromPro(_pro);
            } else if (_drugWidget) {
                _drugWidget->loadFromPro(_pro);
            } else if (_weaponWidget) {
                _weaponWidget->loadFromPro(_pro);
            } else if (_ammoWidget) {
                _ammoWidget->loadFromPro(_pro);
            } else if (_miscItemWidget) {
                _miscItemWidget->loadFromPro(_pro);
            }
            break;
        case Pro::OBJECT_TYPE::CRITTER:
            loadCritterData();
            break;
        case Pro::OBJECT_TYPE::SCENERY:
            loadSceneryData();
            break;
        case Pro::OBJECT_TYPE::WALL:
            if (_wallWidget) {
                _wallWidget->loadFromPro(_pro);
            }
            break;
        case Pro::OBJECT_TYPE::TILE:
            if (_tileWidget) {
                _tileWidget->loadFromPro(_pro);
            }
            break;
        case Pro::OBJECT_TYPE::MISC:
            break;
    }

    if (_pro && _pro->type() == Pro::OBJECT_TYPE::ITEM && _inventoryPreviewWidget && _groundPreviewWidget) {
        updateInventoryPreview();
        updateGroundPreview();
    }
}

void ProEditorDialog::saveProData() {
    if (_pidEdit) {
        _pro->header.PID = _pidEdit->value();
    }

    if (_commonFieldsWidget) {
        _commonFieldsWidget->saveToPro(_pro);
    }

    switch (_pro->type()) {
        case Pro::OBJECT_TYPE::ITEM:
            if (_armorWidget) {
                _armorWidget->saveToPro(_pro);
            } else if (_containerKeyWidget) {
                _containerKeyWidget->saveToPro(_pro);
            } else if (_drugWidget) {
                _drugWidget->saveToPro(_pro);
            } else if (_weaponWidget) {
                _weaponWidget->saveToPro(_pro);
            } else if (_ammoWidget) {
                _ammoWidget->saveToPro(_pro);
            } else if (_miscItemWidget) {
                _miscItemWidget->saveToPro(_pro);
            }
            break;
        case Pro::OBJECT_TYPE::CRITTER:
            saveCritterData();
            break;
        case Pro::OBJECT_TYPE::SCENERY:
            saveSceneryData();
            break;
        case Pro::OBJECT_TYPE::WALL:
            if (_wallWidget) {
                _wallWidget->saveToPro(_pro);
            }
            break;
        case Pro::OBJECT_TYPE::TILE:
            if (_tileWidget) {
                _tileWidget->saveToPro(_pro);
            }
            break;
        case Pro::OBJECT_TYPE::MISC:
            break;
    }
}

void ProEditorDialog::clearFieldsLayouts() {
    while (QLayoutItem* item = _leftFieldsLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    if (_rightFieldsLayout) {
        while (QLayoutItem* item = _rightFieldsLayout->takeAt(0)) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
    }
}

void ProEditorDialog::setupCritterFields() {
    // _leftFieldsLayout and _rightFieldsLayout should be set by the calling tab method

    // === COLUMN 1: Critter-Specific Properties and SPECIAL Stats ===

    // Critter-Specific Properties (not shown in left panel)
    QGroupBox* critterGroup = new QGroupBox("Critter Properties");
    QFormLayout* critterLayout = new QFormLayout(critterGroup);
    critterLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    critterLayout->setSpacing(ui::constants::SPACING_TIGHT);

    // Head FID with selector button
    QWidget* headFidWidget = new QWidget();
    QHBoxLayout* headFidLayout = new QHBoxLayout(headFidWidget);
    headFidLayout->setContentsMargins(0, 0, 0, 0);
    headFidLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _critterHeadFIDLabel = new QLabel("No FRM");
    _critterHeadFIDLabel->setToolTip("FRM filename for critter head appearance");
    _critterHeadFIDLabel->setStyleSheet(ui::theme::styles::borderedLabel());

    _critterHeadFIDSelectorButton = new QPushButton("...");
    _critterHeadFIDSelectorButton->setMaximumWidth(ui::constants::sizes::ICON_BUTTON);
    _critterHeadFIDSelectorButton->setMaximumHeight(ui::constants::sizes::ICON_BUTTON_HEIGHT);
    _critterHeadFIDSelectorButton->setToolTip("Browse FRM files for critter head");
    connect(_critterHeadFIDSelectorButton, &QPushButton::clicked, this, &ProEditorDialog::onCritterHeadFidSelectorClicked);

    headFidLayout->addWidget(_critterHeadFIDLabel, 1);
    headFidLayout->addWidget(_critterHeadFIDSelectorButton);
    critterLayout->addRow("Head FID:", headFidWidget);

    _critterAIPacketEdit = createSpinBox(0, INT_MAX, "AI packet number for critter behavior");
    connectSpinBox(_critterAIPacketEdit);
    critterLayout->addRow("AI Packet:", _critterAIPacketEdit);

    _critterTeamNumberEdit = createSpinBox(0, INT_MAX, "Team number for faction identification");
    connectSpinBox(_critterTeamNumberEdit);
    critterLayout->addRow("Team Number:", _critterTeamNumberEdit);

    _leftFieldsLayout->addWidget(critterGroup);

    // Critter Flags Group (two-column layout)
    QGroupBox* critterFlagsGroup = new QGroupBox("Critter Flags");
    QGridLayout* critterFlagsLayout = new QGridLayout(critterFlagsGroup);
    critterFlagsLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    critterFlagsLayout->setSpacing(ui::constants::SPACING_TIGHT);

    // Column 1 (left)
    _critterBarterCheck = new QCheckBox("Can Barter");
    _critterBarterCheck->setToolTip("Can barter with this critter");
    connect(_critterBarterCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterBarterCheck, 0, 0);

    _critterNoStealCheck = new QCheckBox("No Steal");
    _critterNoStealCheck->setToolTip("Cannot steal from this critter");
    connect(_critterNoStealCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoStealCheck, 1, 0);

    _critterNoDropCheck = new QCheckBox("No Drop");
    _critterNoDropCheck->setToolTip("Cannot drop items");
    connect(_critterNoDropCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoDropCheck, 2, 0);

    _critterNoLimbsCheck = new QCheckBox("No Limbs");
    _critterNoLimbsCheck->setToolTip("No limb damage");
    connect(_critterNoLimbsCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoLimbsCheck, 3, 0);

    _critterNoAgeCheck = new QCheckBox("No Age");
    _critterNoAgeCheck->setToolTip("Does not age");
    connect(_critterNoAgeCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoAgeCheck, 4, 0);

    _critterNoHealCheck = new QCheckBox("No Heal");
    _critterNoHealCheck->setToolTip("Cannot heal");
    connect(_critterNoHealCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoHealCheck, 5, 0);

    // Column 2 (right)
    _critterInvulnerableCheck = new QCheckBox("Invulnerable");
    _critterInvulnerableCheck->setToolTip("Cannot be damaged");
    connect(_critterInvulnerableCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterInvulnerableCheck, 0, 1);

    _critterNoFlattenCheck = new QCheckBox("No Flatten");
    _critterNoFlattenCheck->setToolTip("Doesn't flatten on death (leaves no dead body)");
    connect(_critterNoFlattenCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoFlattenCheck, 1, 1);

    _critterSpecialDeathCheck = new QCheckBox("Special Death");
    _critterSpecialDeathCheck->setToolTip("Special death animation");
    connect(_critterSpecialDeathCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterSpecialDeathCheck, 2, 1);

    _critterLongLimbsCheck = new QCheckBox("Long Limbs");
    _critterLongLimbsCheck->setToolTip("Has long limbs");
    connect(_critterLongLimbsCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterLongLimbsCheck, 3, 1);

    _critterNoKnockbackCheck = new QCheckBox("No Knockback");
    _critterNoKnockbackCheck->setToolTip("Cannot be knocked back");
    connect(_critterNoKnockbackCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterNoKnockbackCheck, 4, 1);

    _leftFieldsLayout->addWidget(critterFlagsGroup);

    // SPECIAL Stats (compact)
    QGroupBox* specialGroup = new QGroupBox("SPECIAL Stats");
    QGridLayout* specialLayout = new QGridLayout(specialGroup);
    specialLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    specialLayout->setSpacing(ui::constants::SPACING_TIGHT);

    const char* specialNames[] = { "STR", "PER", "END", "CHR", "INT", "AGL", "LCK" };
    for (int i = 0; i < 7; ++i) {
        _critterSpecialStatEdits[i] = new QSpinBox();
        _critterSpecialStatEdits[i]->setRange(MIN_SPECIAL_STAT, MAX_SPECIAL_STAT);
        _critterSpecialStatEdits[i]->setToolTip(QString("Base %1 stat").arg(specialNames[i]));
        connect(_critterSpecialStatEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);

        QLabel* label = new QLabel(specialNames[i]);
        label->setFixedWidth(ui::constants::sizes::LABEL_FRAME);
        specialLayout->addWidget(label, i / 4, (i % 4) * 2);
        specialLayout->addWidget(_critterSpecialStatEdits[i], i / 4, (i % 4) * 2 + 1);
    }
    _leftFieldsLayout->addWidget(specialGroup);

    // Primary Combat Stats
    QGroupBox* combatGroup = new QGroupBox("Combat Stats");
    QFormLayout* combatLayout = new QFormLayout(combatGroup);
    combatLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    combatLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _critterMaxHitPointsEdit = new QSpinBox();
    _critterMaxHitPointsEdit->setRange(1, INT_MAX);
    _critterMaxHitPointsEdit->setToolTip("Maximum hit points");
    connect(_critterMaxHitPointsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Max HP:", _critterMaxHitPointsEdit);

    _critterActionPointsEdit = new QSpinBox();
    _critterActionPointsEdit->setRange(1, INT_MAX);
    _critterActionPointsEdit->setToolTip("Action points per turn");
    connect(_critterActionPointsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Action Points:", _critterActionPointsEdit);

    _critterArmorClassEdit = new QSpinBox();
    _critterArmorClassEdit->setRange(0, INT_MAX);
    _critterArmorClassEdit->setToolTip("Base armor class");
    connect(_critterArmorClassEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Armor Class:", _critterArmorClassEdit);

    _critterMeleeDamageEdit = new QSpinBox();
    _critterMeleeDamageEdit->setRange(0, INT_MAX);
    _critterMeleeDamageEdit->setToolTip("Melee damage bonus");
    connect(_critterMeleeDamageEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Melee Damage:", _critterMeleeDamageEdit);

    _leftFieldsLayout->addWidget(combatGroup);

    // === COLUMN 2: Advanced Stats, Damage Resistance/Threshold, Skills ===

    // Advanced Stats & Characteristics
    QGroupBox* advancedGroup = new QGroupBox("Advanced Stats");
    QFormLayout* advancedLayout = new QFormLayout(advancedGroup);
    advancedLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    advancedLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _critterCarryWeightMaxEdit = new QSpinBox();
    _critterCarryWeightMaxEdit->setRange(0, INT_MAX);
    _critterCarryWeightMaxEdit->setToolTip("Maximum carry weight");
    connect(_critterCarryWeightMaxEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Carry Weight:", _critterCarryWeightMaxEdit);

    _critterSequenceEdit = new QSpinBox();
    _critterSequenceEdit->setRange(0, INT_MAX);
    _critterSequenceEdit->setToolTip("Initiative sequence bonus");
    connect(_critterSequenceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Sequence:", _critterSequenceEdit);

    _critterHealingRateEdit = new QSpinBox();
    _critterHealingRateEdit->setRange(0, INT_MAX);
    _critterHealingRateEdit->setToolTip("Healing rate bonus");
    connect(_critterHealingRateEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Healing Rate:", _critterHealingRateEdit);

    _critterCriticalChanceEdit = new QSpinBox();
    _critterCriticalChanceEdit->setRange(0, INT_MAX);
    _critterCriticalChanceEdit->setToolTip("Critical hit chance bonus");
    connect(_critterCriticalChanceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Critical Chance:", _critterCriticalChanceEdit);

    _critterBetterCriticalsEdit = new QSpinBox();
    _critterBetterCriticalsEdit->setRange(0, INT_MAX);
    _critterBetterCriticalsEdit->setToolTip("Better criticals bonus");
    connect(_critterBetterCriticalsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Better Criticals:", _critterBetterCriticalsEdit);

    _critterAgeEdit = new QSpinBox();
    _critterAgeEdit->setRange(MIN_AGE, MAX_AGE);
    _critterAgeEdit->setToolTip("Critter age");
    connect(_critterAgeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Age:", _critterAgeEdit);

    _critterGenderCombo = new QComboBox();
    _critterGenderCombo->addItems(game::enums::critterGenders());
    _critterGenderCombo->setToolTip("Critter gender");
    connect(_critterGenderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    advancedLayout->addRow("Gender:", _critterGenderCombo);

    _critterBodyTypeCombo = new QComboBox();
    _critterBodyTypeCombo->addItems(game::enums::critterBodyTypes(_resources));
    _critterBodyTypeCombo->setToolTip("Body type for animations");
    connect(_critterBodyTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    advancedLayout->addRow("Body Type:", _critterBodyTypeCombo);

    _critterExperienceEdit = new QSpinBox();
    _critterExperienceEdit->setRange(0, INT_MAX);
    _critterExperienceEdit->setToolTip("Experience points for killing this critter");
    connect(_critterExperienceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Experience:", _critterExperienceEdit);

    _critterKillTypeEdit = new QSpinBox();
    _critterKillTypeEdit->setRange(0, INT_MAX);
    _critterKillTypeEdit->setToolTip("Kill type for karma tracking");
    connect(_critterKillTypeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Kill Type:", _critterKillTypeEdit);

    _critterDamageTypeEdit = new QSpinBox();
    _critterDamageTypeEdit->setRange(0, INT_MAX);
    _critterDamageTypeEdit->setToolTip("Default damage type");
    connect(_critterDamageTypeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Damage Type:", _critterDamageTypeEdit);

    if (_rightFieldsLayout) {
        _rightFieldsLayout->addWidget(advancedGroup);
    }

    // Damage Protection (unified resistance and threshold)
    QGroupBox* damageProtectionGroup = new QGroupBox("Damage Protection");
    QGridLayout* damageProtectionLayout = new QGridLayout(damageProtectionGroup);
    damageProtectionLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    damageProtectionLayout->setSpacing(ui::constants::SPACING_GRID);

    // Headers
    QLabel* thresholdHeader = new QLabel("Threshold");
    thresholdHeader->setStyleSheet(ui::theme::styles::boldLabel());
    thresholdHeader->setAlignment(Qt::AlignCenter);
    damageProtectionLayout->addWidget(thresholdHeader, 0, 1);

    QLabel* resistanceHeader = new QLabel("Resistance");
    resistanceHeader->setStyleSheet(ui::theme::styles::boldLabel());
    resistanceHeader->setAlignment(Qt::AlignCenter);
    damageProtectionLayout->addWidget(resistanceHeader, 0, 2);

    const QStringList damageTypes = game::enums::damageTypes9(_resources);

    // First 7 damage types (threshold + resistance)
    for (int i = 0; i < 7; ++i) {
        // Damage type label
        QLabel* typeLabel = new QLabel(damageTypes.at(i) + ":");
        typeLabel->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(typeLabel, i + 1, 0);

        // Damage threshold
        _critterDamageThresholdEdits[i] = new QSpinBox();
        _critterDamageThresholdEdits[i]->setRange(0, INT_MAX);
        _critterDamageThresholdEdits[i]->setToolTip(QString("%1 damage threshold").arg(damageTypes.at(i)));
        _critterDamageThresholdEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        connect(_critterDamageThresholdEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        damageProtectionLayout->addWidget(_critterDamageThresholdEdits[i], i + 1, 1);

        // Damage resistance
        _critterDamageResistEdits[i] = new QSpinBox();
        _critterDamageResistEdits[i]->setRange(0, INT_MAX);
        _critterDamageResistEdits[i]->setToolTip(QString("%1 damage resistance").arg(damageTypes.at(i)));
        _critterDamageResistEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        connect(_critterDamageResistEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        damageProtectionLayout->addWidget(_critterDamageResistEdits[i], i + 1, 2);
    }

    // Last 2 damage types (Radiation and Poison - resistance only)
    for (int i = 7; i < 9; ++i) {
        QLabel* typeLabel = new QLabel(damageTypes.at(i) + ":");
        typeLabel->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(typeLabel, i + 1, 0);

        // No threshold for Radiation and Poison, add placeholder
        QLabel* placeholder = new QLabel("—");
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet(ui::theme::styles::placeholderText());
        placeholder->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        damageProtectionLayout->addWidget(placeholder, i + 1, 1);

        // Damage resistance
        _critterDamageResistEdits[i] = new QSpinBox();
        _critterDamageResistEdits[i]->setRange(0, INT_MAX);
        _critterDamageResistEdits[i]->setToolTip(QString("%1 damage resistance").arg(damageTypes.at(i)));
        _critterDamageResistEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        connect(_critterDamageResistEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        damageProtectionLayout->addWidget(_critterDamageResistEdits[i], i + 1, 2);
    }

    if (_rightFieldsLayout) {
        _rightFieldsLayout->addWidget(damageProtectionGroup);
    }

    // Skills Section (18 skills)
    QGroupBox* skillsGroup = new QGroupBox("Skills");
    QGridLayout* skillsLayout = new QGridLayout(skillsGroup);
    skillsLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    skillsLayout->setSpacing(ui::constants::SPACING_TIGHT);

    // Headers
    QLabel* skillHeader = new QLabel("Skill");
    skillHeader->setStyleSheet(ui::theme::styles::boldLabel());
    skillsLayout->addWidget(skillHeader, 0, 0);

    QLabel* valueHeader = new QLabel("Value (%)");
    valueHeader->setStyleSheet(ui::theme::styles::boldLabel());
    valueHeader->setAlignment(Qt::AlignCenter);
    skillsLayout->addWidget(valueHeader, 0, 1);

    QLabel* skillHeader2 = new QLabel("Skill");
    skillHeader2->setStyleSheet(ui::theme::styles::boldLabel());
    skillsLayout->addWidget(skillHeader2, 0, 2);

    QLabel* valueHeader2 = new QLabel("Value (%)");
    valueHeader2->setStyleSheet(ui::theme::styles::boldLabel());
    valueHeader2->setAlignment(Qt::AlignCenter);
    skillsLayout->addWidget(valueHeader2, 0, 3);

    QLabel* skillHeader3 = new QLabel("Skill");
    skillHeader3->setStyleSheet(ui::theme::styles::boldLabel());
    skillsLayout->addWidget(skillHeader3, 0, 4);

    QLabel* valueHeader3 = new QLabel("Value (%)");
    valueHeader3->setStyleSheet(ui::theme::styles::boldLabel());
    valueHeader3->setAlignment(Qt::AlignCenter);
    skillsLayout->addWidget(valueHeader3, 0, 5);

    // Skill names based on F2_ProtoManager reference
    const char* skillNames[] = {
        "Small Guns", "Big Guns", "Energy Weapons", "Unarmed", "Melee Weapons", "Throwing",
        "First Aid", "Doctor", "Sneak", "Lockpick", "Steal", "Traps",
        "Science", "Repair", "Speech", "Barter", "Gambling", "Outdoorsman"
    };

    // Create skill controls in 3 columns (6 skills per column)
    for (int i = 0; i < 18; ++i) {
        int column = i / 6;    // 0, 1, or 2
        int row = (i % 6) + 1; // 1-6

        // Skill name label
        QLabel* skillLabel = new QLabel(QString(skillNames[i]) + ":");
        skillLabel->setFixedWidth(ui::constants::sizes::WIDTH_LABEL_SKILL);
        skillsLayout->addWidget(skillLabel, row, column * 2);

        // Skill value spinbox
        _critterSkillEdits[i] = new QSpinBox();
        _critterSkillEdits[i]->setRange(0, MAX_SKILL_PERCENT); // 0-300%
        _critterSkillEdits[i]->setToolTip(QString("%1 skill percentage").arg(skillNames[i]));
        _critterSkillEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_MEDIUM);
        connect(_critterSkillEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        skillsLayout->addWidget(_critterSkillEdits[i], row, column * 2 + 1);
    }

    if (_rightFieldsLayout) {
        _rightFieldsLayout->addWidget(skillsGroup);
    }

    // Add stretch to push everything to top
    _leftFieldsLayout->addStretch();
    if (_rightFieldsLayout) {
        _rightFieldsLayout->addStretch();
    }
}

void ProEditorDialog::setupSceneryFields() {
    clearFieldsLayouts();

    // === COLUMN 1: Basic Scenery Properties ===

    QGroupBox* basicGroup = new QGroupBox("Basic Properties");
    QFormLayout* basicLayout = new QFormLayout(basicGroup);
    basicLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    basicLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _sceneryMaterialIdEdit = createMaterialComboBox("Material type for scenery");
    connectComboBox(_sceneryMaterialIdEdit);
    basicLayout->addRow("Material:", _sceneryMaterialIdEdit);

    _scenerySoundIdEdit = createSpinBox(0, 255, "Sound ID for interactions");
    connectSpinBox(_scenerySoundIdEdit);
    basicLayout->addRow("Sound ID:", _scenerySoundIdEdit);

    _sceneryTypeCombo = createComboBox(game::enums::sceneryTypes(_resources), "Scenery subtype");
    connectComboBox(_sceneryTypeCombo);
    basicLayout->addRow("Type:", _sceneryTypeCombo);

    _leftFieldsLayout->addWidget(basicGroup);

    // === COLUMN 2: Type-Specific Properties ===

    // Door Properties
    QGroupBox* doorGroup = new QGroupBox("Door Properties");
    QFormLayout* doorLayout = new QFormLayout(doorGroup);
    doorLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    doorLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _doorWalkThroughCheck = new QCheckBox("Walk Through");
    _doorWalkThroughCheck->setToolTip("Allow walking through the door");
    connectCheckBox(_doorWalkThroughCheck);
    doorLayout->addRow("", _doorWalkThroughCheck);

    _doorUnknownEdit = createSpinBox(0, 0xFFFFFFFF, "Door unknown field");
    connectSpinBox(_doorUnknownEdit);
    doorLayout->addRow("Unknown Field:", _doorUnknownEdit);

    if (_rightFieldsLayout) {
        _rightFieldsLayout->addWidget(doorGroup);
    }

    // Stairs Properties
    QGroupBox* stairsGroup = new QGroupBox("Stairs Properties");
    QFormLayout* stairsLayout = new QFormLayout(stairsGroup);
    stairsLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    stairsLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _stairsDestTileEdit = createSpinBox(0, 39999, "Destination tile number");
    connectSpinBox(_stairsDestTileEdit);
    stairsLayout->addRow("Dest Tile:", _stairsDestTileEdit);

    _stairsDestElevationEdit = createSpinBox(0, 3, "Destination elevation");
    connectSpinBox(_stairsDestElevationEdit);
    stairsLayout->addRow("Dest Elevation:", _stairsDestElevationEdit);

    if (_rightFieldsLayout) {
        _rightFieldsLayout->addWidget(stairsGroup);
    }

    // Elevator Properties (these will be shown/hidden based on scenery type)
    QGroupBox* elevatorGroup = new QGroupBox("Elevator Properties");
    QFormLayout* elevatorLayout = new QFormLayout(elevatorGroup);
    elevatorLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    elevatorLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _elevatorTypeEdit = createSpinBox(0, INT_MAX, "Elevator type");
    connectSpinBox(_elevatorTypeEdit);
    elevatorLayout->addRow("Type:", _elevatorTypeEdit);

    _elevatorLevelEdit = createSpinBox(0, INT_MAX, "Elevator level");
    connectSpinBox(_elevatorLevelEdit);
    elevatorLayout->addRow("Level:", _elevatorLevelEdit);

    if (_rightFieldsLayout) {
        _rightFieldsLayout->addWidget(elevatorGroup);
    }

    // Ladder Properties
    QGroupBox* ladderGroup = new QGroupBox("Ladder Properties");
    QFormLayout* ladderLayout = new QFormLayout(ladderGroup);
    ladderLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    ladderLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _ladderDestTileElevationEdit = createSpinBox(0, 0xFFFFFFFF, "Destination tile and elevation combined");
    connectSpinBox(_ladderDestTileElevationEdit);
    ladderLayout->addRow("Dest Tile+Elev:", _ladderDestTileElevationEdit);

    if (_rightFieldsLayout) {
        _rightFieldsLayout->addWidget(ladderGroup);
    }

    // Generic Properties
    QGroupBox* genericGroup = new QGroupBox("Generic Properties");
    QFormLayout* genericLayout = new QFormLayout(genericGroup);
    genericLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    genericLayout->setSpacing(ui::constants::SPACING_TIGHT);

    _genericUnknownEdit = createSpinBox(0, 0xFFFFFFFF, "Generic unknown field");
    connectSpinBox(_genericUnknownEdit);
    genericLayout->addRow("Unknown Field:", _genericUnknownEdit);

    if (_rightFieldsLayout) {
        _rightFieldsLayout->addWidget(genericGroup);
    }

    // Add stretch to push content to top
    _leftFieldsLayout->addStretch();
    if (_rightFieldsLayout) {
        _rightFieldsLayout->addStretch();
    }
}

void ProEditorDialog::setupMiscFields() {
    // TODO: Implement misc-specific fields
}

void ProEditorDialog::updatePreview() {
    if (_animationController) {
        _animationController->stop();
    }

    if (_pro && _pro->type() == Pro::OBJECT_TYPE::ITEM && _inventoryPreviewWidget && _groundPreviewWidget) {
        updateInventoryPreview();
        updateGroundPreview();

        if (_animationControls) {
            _animationControls->setEnabled(false);
        }
        return;
    }

    if (_pro && _pro->type() != Pro::OBJECT_TYPE::ITEM && _objectPreviewWidget) {
        std::string frmPath = _resources.frmResolver().resolve(static_cast<unsigned int>(_pro->header.FID));
        spdlog::debug("ObjectPreviewWidget: Setting FRM path: {}", frmPath);
        _objectPreviewWidget->setFrmPath(QString::fromStdString(frmPath));
        _objectPreviewWidget->setFid(_pro->header.FID);
    }
}

int32_t ProEditorDialog::getPreviewFid() {
    if (!_pro) {
        return 0;
    }

    return _pro->header.FID;
}

int32_t ProEditorDialog::getInventoryFid() {
    if (!_pro) {
        return 0;
    }

    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        return _pro->commonItemData.inventoryFID;
    }

    return 0;
}

int32_t ProEditorDialog::getGroundFid() {
    if (!_pro) {
        return 0;
    }

    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        return _pro->header.FID;
    }

    return 0;
}

void ProEditorDialog::updateInventoryPreview() {

    if (!_inventoryPreviewWidget) {
        return;
    }

    int32_t inventoryFid = getInventoryFid();

    if (inventoryFid <= 0) {
        _inventoryPreviewWidget->clear();
        return;
    }

    try {
        std::string frmPath = _resources.frmResolver().resolve(static_cast<unsigned int>(inventoryFid));

        if (frmPath.empty()) {
            _inventoryPreviewWidget->clear();
            return;
        }

        _inventoryPreviewWidget->setFrmPath(QString::fromStdString(frmPath));

    } catch (const std::exception& e) {
        spdlog::error("ProEditorDialog::updateInventoryPreview() - exception: {}", e.what());
        _inventoryPreviewWidget->clear();
    }
}

void ProEditorDialog::updateGroundPreview() {

    if (!_groundPreviewWidget) {
        return;
    }

    int32_t groundFid = getGroundFid();

    if (groundFid <= 0) {
        _groundPreviewWidget->clear();
        return;
    }

    try {
        std::string frmPath = _resources.frmResolver().resolve(static_cast<unsigned int>(groundFid));

        if (frmPath.empty()) {
            _groundPreviewWidget->clear();
            return;
        }

        _groundPreviewWidget->setFrmPath(QString::fromStdString(frmPath));

    } catch (const std::exception& e) {
        spdlog::error("ProEditorDialog::updateGroundPreview() - exception: {}", e.what());
        _groundPreviewWidget->clear();
    }
}

void ProEditorDialog::onAccept() {
    saveProData();

    QString suggestedName = QString::fromStdString(_pro->path().filename().string());
    QString filePath = QFileDialog::getSaveFileName(
        this,
        "Save PRO File",
        suggestedName,
        "PRO Files (*.pro);;All Files (*)");

    if (filePath.isEmpty()) {
        return; // User cancelled
    }

    std::filesystem::path savePath(filePath.toStdString());
    if (std::filesystem::exists(savePath)) {
        std::filesystem::path backupPath = savePath;
        backupPath += ".bak";

        try {
            std::filesystem::copy_file(savePath, backupPath,
                std::filesystem::copy_options::overwrite_existing);
            spdlog::info("Created backup: {}", backupPath.string());
        } catch (const std::exception& e) {
            spdlog::warn("Failed to create backup: {}", e.what());
        }
    }

    try {
        ProWriter writer;
        writer.openFile(savePath, true);

        if (writer.write(*_pro)) {
            QMessageBox::information(this, "Success",
                QString("PRO file saved successfully to:\n%1").arg(filePath));
            accept();
        } else {
            QMessageBox::critical(this, "Error",
                "Failed to save PRO file. Check the log for details.");
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error",
            QString("Failed to save PRO file:\n%1").arg(e.what()));
        spdlog::error("ProEditorDialog: Failed to save PRO file: {}", e.what());
    }
}

void ProEditorDialog::onFieldChanged() {
    Q_UNUSED(qobject_cast<QWidget*>(QObject::sender()));
}

void ProEditorDialog::onComboBoxChanged() {
    Q_UNUSED(qobject_cast<QComboBox*>(QObject::sender()));
}

void ProEditorDialog::onCheckBoxChanged() {
    Q_UNUSED(qobject_cast<QCheckBox*>(QObject::sender()));
}

void ProEditorDialog::onFidSelectorClicked() {
}

void ProEditorDialog::onEditMessageClicked() {
    try {
        const auto* msgFile = ProHelper::msgFile(_resources, _pro->type());
        if (!msgFile) {
            QMessageBox::warning(this, "Message Selection",
                "Could not load MSG file for this object type.");
            return;
        }

        MessageSelectorDialog dialog(msgFile, _pro->header.message_id, this);
        if (dialog.exec() == QDialog::Accepted) {
            int selectedMessageId = dialog.getSelectedMessageId();
            if (selectedMessageId >= 0) {
                _pro->header.message_id = selectedMessageId;

                if (_commonFieldsWidget) {
                    _commonFieldsWidget->loadFromPro(_pro);
                }

                updateWindowTitle();

                spdlog::debug("ProEditorDialog: Message ID changed to {}", selectedMessageId);
            }
        }

    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Message Selection Error",
            QString("Error opening message selector: %1").arg(e.what()));
        spdlog::error("ProEditorDialog::onEditMessageClicked - Error: {}", e.what());
    }
}

void ProEditorDialog::onInventoryFidSelectorClicked() {
}

void ProEditorDialog::openFrmSelectorForLabel(QLabel* targetLabel, int32_t* fidStorage, uint32_t objectType) {
    if (!targetLabel || !fidStorage)
        return;

    FrmSelectorDialog dialog(_resources, this);
    dialog.setObjectTypeFilter(objectType);
    dialog.setInitialFrmPid(static_cast<uint32_t>(*fidStorage));

    if (dialog.exec() == QDialog::Accepted) {
        uint32_t selectedFrmPid = dialog.getSelectedFrmPid();
        if (selectedFrmPid > 0) {
            *fidStorage = static_cast<int32_t>(selectedFrmPid);
            targetLabel->setText(getFrmFilename(*fidStorage));
            updatePreview(); // Update preview when FID changes
        }
    }
}

QSpinBox* ProEditorDialog::createSpinBox(int min, int max, const QString& tooltip) {
    QSpinBox* spinBox = new QSpinBox();
    spinBox->setRange(min, max);
    if (!tooltip.isEmpty()) {
        spinBox->setToolTip(tooltip);
    }
    connectSpinBox(spinBox);
    return spinBox;
}

QSpinBox* ProEditorDialog::createHexSpinBox(int max, const QString& tooltip) {
    QSpinBox* spinBox = new QSpinBox();
    spinBox->setRange(0, max);
    spinBox->setDisplayIntegerBase(16);
    spinBox->setPrefix("0x");
    if (!tooltip.isEmpty()) {
        spinBox->setToolTip(tooltip);
    }
    connectSpinBox(spinBox);
    return spinBox;
}

QComboBox* ProEditorDialog::createComboBox(const QStringList& items, const QString& tooltip) {
    QComboBox* comboBox = new QComboBox();
    comboBox->addItems(items);
    if (!tooltip.isEmpty()) {
        comboBox->setToolTip(tooltip);
    }
    connectComboBox(comboBox);
    return comboBox;
}

void ProEditorDialog::connectSpinBox(QSpinBox* spinBox) {
    connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
}

void ProEditorDialog::connectComboBox(QComboBox* comboBox) {
    connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
}

void ProEditorDialog::connectCheckBox(QCheckBox* checkBox) {
    connect(checkBox, &QCheckBox::toggled, this, &ProEditorDialog::onCheckBoxChanged);
}

QFormLayout* ProEditorDialog::createStandardFormLayout(QWidget* parent) {
    QFormLayout* layout = new QFormLayout(parent);
    layout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    layout->setSpacing(ui::constants::SPACING_TIGHT);
    return layout;
}

void ProEditorDialog::loadIntArrayToWidgets(QSpinBox** widgets, const uint32_t* arrayValues, int count) {
    for (int i = 0; i < count; ++i) {
        if (widgets[i] && arrayValues) {
            widgets[i]->setValue(static_cast<int>(arrayValues[i]));
        }
    }
}

void ProEditorDialog::loadCritterData() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::CRITTER) {
        spdlog::warn("ProEditorDialog: Cannot load critter data - not a critter PRO");
        return;
    }

    const auto& critterData = _pro->critterData;

    // Load basic critter properties
    if (_critterHeadFIDLabel) {
        _critterHeadFID = static_cast<int32_t>(critterData.headFID);
        _critterHeadFIDLabel->setText(getFrmFilename(_critterHeadFID));
    }
    if (_critterAIPacketEdit)
        _critterAIPacketEdit->setValue(static_cast<int>(critterData.aiPacket));
    if (_critterTeamNumberEdit)
        _critterTeamNumberEdit->setValue(static_cast<int>(critterData.teamNumber));
    if (_critterFlagsEdit)
        _critterFlagsEdit->setValue(static_cast<int>(critterData.flags));

    // Load critter flag checkboxes
    if (_critterBarterCheck)
        _critterBarterCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_BARTER));
    if (_critterNoStealCheck)
        _critterNoStealCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_STEAL));
    if (_critterNoDropCheck)
        _critterNoDropCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_DROP));
    if (_critterNoLimbsCheck)
        _critterNoLimbsCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_LIMBS));
    if (_critterNoAgeCheck)
        _critterNoAgeCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_AGE));
    if (_critterNoHealCheck)
        _critterNoHealCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_HEAL));
    if (_critterInvulnerableCheck)
        _critterInvulnerableCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_INVULNERABLE));
    if (_critterNoFlattenCheck)
        _critterNoFlattenCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_FLATTEN));
    if (_critterSpecialDeathCheck)
        _critterSpecialDeathCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_SPECIAL_DEATH));
    if (_critterLongLimbsCheck)
        _critterLongLimbsCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_LONG_LIMBS));
    if (_critterNoKnockbackCheck)
        _critterNoKnockbackCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_KNOCKBACK));
    if (_critterMaxHitPointsEdit)
        _critterMaxHitPointsEdit->setValue(static_cast<int>(critterData.maxHitPoints));
    if (_critterActionPointsEdit)
        _critterActionPointsEdit->setValue(static_cast<int>(critterData.actionPoints));
    if (_critterArmorClassEdit)
        _critterArmorClassEdit->setValue(static_cast<int>(critterData.armorClass));
    if (_critterMeleeDamageEdit)
        _critterMeleeDamageEdit->setValue(static_cast<int>(critterData.meleeDamage));
    if (_critterCarryWeightMaxEdit)
        _critterCarryWeightMaxEdit->setValue(static_cast<int>(critterData.carryWeightMax));
    if (_critterSequenceEdit)
        _critterSequenceEdit->setValue(static_cast<int>(critterData.sequence));
    if (_critterHealingRateEdit)
        _critterHealingRateEdit->setValue(static_cast<int>(critterData.healingRate));
    if (_critterCriticalChanceEdit)
        _critterCriticalChanceEdit->setValue(static_cast<int>(critterData.criticalChance));
    if (_critterBetterCriticalsEdit)
        _critterBetterCriticalsEdit->setValue(static_cast<int>(critterData.betterCriticals));
    if (_critterAgeEdit)
        _critterAgeEdit->setValue(static_cast<int>(critterData.age));
    if (_critterGenderCombo)
        _critterGenderCombo->setCurrentIndex(static_cast<int>(critterData.gender));
    if (_critterBodyTypeCombo)
        _critterBodyTypeCombo->setCurrentIndex(static_cast<int>(critterData.bodyType));
    if (_critterExperienceEdit)
        _critterExperienceEdit->setValue(static_cast<int>(critterData.experienceForKill));
    if (_critterKillTypeEdit)
        _critterKillTypeEdit->setValue(static_cast<int>(critterData.killType));
    if (_critterDamageTypeEdit)
        _critterDamageTypeEdit->setValue(static_cast<int>(critterData.damageType));

    // Load SPECIAL stats (STR, PER, END, CHR, INT, AGL, LCK)
    loadIntArrayToWidgets(_critterSpecialStatEdits, critterData.specialStats, 7);

    // Load damage thresholds (7 damage types)
    loadIntArrayToWidgets(_critterDamageThresholdEdits, critterData.damageThreshold, 7);

    // Load damage resistances (9 damage types)
    loadIntArrayToWidgets(_critterDamageResistEdits, critterData.damageResist, 9);

    // Load skills (18 different skills)
    loadIntArrayToWidgets(_critterSkillEdits, critterData.skills, 18);

    // Load bonus SPECIAL stats (7 stats)
    loadIntArrayToWidgets(_critterBonusSpecialStatEdits, critterData.bonusSpecialStats, 7);

    // Load bonus derived stats
    if (_critterBonusHealthPointsEdit)
        _critterBonusHealthPointsEdit->setValue(static_cast<int>(critterData.bonusHealthPoints));
    if (_critterBonusActionPointsEdit)
        _critterBonusActionPointsEdit->setValue(static_cast<int>(critterData.bonusActionPoints));
    if (_critterBonusArmorClassEdit)
        _critterBonusArmorClassEdit->setValue(static_cast<int>(critterData.bonusArmorClass));
    if (_critterBonusMeleeDamageEdit)
        _critterBonusMeleeDamageEdit->setValue(static_cast<int>(critterData.bonusMeleeDamage));
    if (_critterBonusCarryWeightEdit)
        _critterBonusCarryWeightEdit->setValue(static_cast<int>(critterData.bonusCarryWeight));
    if (_critterBonusSequenceEdit)
        _critterBonusSequenceEdit->setValue(static_cast<int>(critterData.bonusSequence));
    if (_critterBonusHealingRateEdit)
        _critterBonusHealingRateEdit->setValue(static_cast<int>(critterData.bonusHealingRate));
    if (_critterBonusCriticalChanceEdit)
        _critterBonusCriticalChanceEdit->setValue(static_cast<int>(critterData.bonusCriticalChance));
    if (_critterBonusBetterCriticalsEdit)
        _critterBonusBetterCriticalsEdit->setValue(static_cast<int>(critterData.bonusBetterCriticals));
    if (_critterBonusAgeEdit)
        _critterBonusAgeEdit->setValue(static_cast<int>(critterData.bonusAge));
    if (_critterBonusGenderEdit)
        _critterBonusGenderEdit->setValue(static_cast<int>(critterData.bonusGender));

    // Load bonus damage thresholds (8 damage types)
    for (int i = 0; i < 8; ++i) {
        if (_critterBonusDamageThresholdEdits[i]) {
            _critterBonusDamageThresholdEdits[i]->setValue(static_cast<int>(critterData.bonusDamageThreshold[i]));
        }
    }

    // Load bonus damage resistances (8 damage types)
    for (int i = 0; i < 8; ++i) {
        if (_critterBonusDamageResistanceEdits[i]) {
            _critterBonusDamageResistanceEdits[i]->setValue(static_cast<int>(critterData.bonusDamageResistance[i]));
        }
    }
}

void ProEditorDialog::loadSceneryData() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::SCENERY) {
        spdlog::warn("ProEditorDialog: Cannot load scenery data - not a scenery PRO");
        return;
    }

    const auto& sceneryData = _pro->sceneryData;

    // Load basic scenery properties
    if (_sceneryMaterialIdEdit)
        _sceneryMaterialIdEdit->setCurrentIndex(static_cast<int>(sceneryData.materialId));
    if (_scenerySoundIdEdit)
        _scenerySoundIdEdit->setValue(static_cast<int>(sceneryData.soundId));

    // Set scenery type based on object subtype
    if (_sceneryTypeCombo) {
        Pro::SCENERY_TYPE sceneryType = static_cast<Pro::SCENERY_TYPE>(_pro->objectSubtypeId());
        _sceneryTypeCombo->setCurrentIndex(static_cast<int>(sceneryType));
    }

    // Load type-specific data based on scenery subtype
    Pro::SCENERY_TYPE sceneryType = static_cast<Pro::SCENERY_TYPE>(_pro->objectSubtypeId());

    switch (sceneryType) {
        case Pro::SCENERY_TYPE::DOOR:
            if (_doorWalkThroughCheck)
                _doorWalkThroughCheck->setChecked(sceneryData.doorData.walkThroughFlag != 0);
            if (_doorUnknownEdit)
                _doorUnknownEdit->setValue(static_cast<int>(sceneryData.doorData.unknownField));
            break;

        case Pro::SCENERY_TYPE::STAIRS:
            if (_stairsDestTileEdit)
                _stairsDestTileEdit->setValue(static_cast<int>(sceneryData.stairsData.destTile));
            if (_stairsDestElevationEdit)
                _stairsDestElevationEdit->setValue(static_cast<int>(sceneryData.stairsData.destElevation));
            break;

        case Pro::SCENERY_TYPE::ELEVATOR:
            if (_elevatorTypeEdit)
                _elevatorTypeEdit->setValue(static_cast<int>(sceneryData.elevatorData.elevatorType));
            if (_elevatorLevelEdit)
                _elevatorLevelEdit->setValue(static_cast<int>(sceneryData.elevatorData.elevatorLevel));
            break;

        case Pro::SCENERY_TYPE::LADDER_BOTTOM:
        case Pro::SCENERY_TYPE::LADDER_TOP:
            if (_ladderDestTileElevationEdit)
                _ladderDestTileElevationEdit->setValue(static_cast<int>(sceneryData.ladderData.destTileAndElevation));
            break;

        case Pro::SCENERY_TYPE::GENERIC:
        default:
            if (_genericUnknownEdit)
                _genericUnknownEdit->setValue(static_cast<int>(sceneryData.genericData.unknownField));
            break;
    }
}

void ProEditorDialog::saveCritterData() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::CRITTER) {
        spdlog::warn("ProEditorDialog: Cannot save critter data - not a critter PRO");
        return;
    }

    auto& critterData = _pro->critterData;

    // Save basic critter properties
    if (_critterHeadFIDLabel)
        critterData.headFID = static_cast<uint32_t>(_critterHeadFID);
    if (_critterAIPacketEdit)
        critterData.aiPacket = static_cast<uint32_t>(_critterAIPacketEdit->value());
    if (_critterTeamNumberEdit)
        critterData.teamNumber = static_cast<uint32_t>(_critterTeamNumberEdit->value());

    // Save critter flags from checkboxes (preferred) or numeric field (fallback)
    if (_critterBarterCheck) {
        uint32_t flags = 0;
        if (_critterBarterCheck->isChecked())
            flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_BARTER);
        if (_critterNoStealCheck && _critterNoStealCheck->isChecked())
            flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_STEAL);
        if (_critterNoDropCheck && _critterNoDropCheck->isChecked())
            flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_DROP);
        if (_critterNoLimbsCheck && _critterNoLimbsCheck->isChecked())
            flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_LIMBS);
        if (_critterNoAgeCheck && _critterNoAgeCheck->isChecked())
            flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_AGE);
        if (_critterNoHealCheck && _critterNoHealCheck->isChecked())
            flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_HEAL);
        if (_critterInvulnerableCheck && _critterInvulnerableCheck->isChecked())
            flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_INVULNERABLE);
        if (_critterNoFlattenCheck && _critterNoFlattenCheck->isChecked())
            flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_FLATTEN);
        if (_critterSpecialDeathCheck && _critterSpecialDeathCheck->isChecked())
            flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_SPECIAL_DEATH);
        if (_critterLongLimbsCheck && _critterLongLimbsCheck->isChecked())
            flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_LONG_LIMBS);
        if (_critterNoKnockbackCheck && _critterNoKnockbackCheck->isChecked())
            flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_KNOCKBACK);
        critterData.flags = flags;
    } else if (_critterFlagsEdit) {
        critterData.flags = static_cast<uint32_t>(_critterFlagsEdit->value());
    }
    if (_critterMaxHitPointsEdit)
        critterData.maxHitPoints = static_cast<uint32_t>(_critterMaxHitPointsEdit->value());
    if (_critterActionPointsEdit)
        critterData.actionPoints = static_cast<uint32_t>(_critterActionPointsEdit->value());
    if (_critterArmorClassEdit)
        critterData.armorClass = static_cast<uint32_t>(_critterArmorClassEdit->value());
    if (_critterMeleeDamageEdit)
        critterData.meleeDamage = static_cast<uint32_t>(_critterMeleeDamageEdit->value());
    if (_critterCarryWeightMaxEdit)
        critterData.carryWeightMax = static_cast<uint32_t>(_critterCarryWeightMaxEdit->value());
    if (_critterSequenceEdit)
        critterData.sequence = static_cast<uint32_t>(_critterSequenceEdit->value());
    if (_critterHealingRateEdit)
        critterData.healingRate = static_cast<uint32_t>(_critterHealingRateEdit->value());
    if (_critterCriticalChanceEdit)
        critterData.criticalChance = static_cast<uint32_t>(_critterCriticalChanceEdit->value());
    if (_critterBetterCriticalsEdit)
        critterData.betterCriticals = static_cast<uint32_t>(_critterBetterCriticalsEdit->value());
    if (_critterAgeEdit)
        critterData.age = static_cast<uint32_t>(_critterAgeEdit->value());
    if (_critterGenderCombo)
        critterData.gender = static_cast<uint32_t>(_critterGenderCombo->currentIndex());
    if (_critterBodyTypeCombo)
        critterData.bodyType = static_cast<uint32_t>(_critterBodyTypeCombo->currentIndex());
    if (_critterExperienceEdit)
        critterData.experienceForKill = static_cast<uint32_t>(_critterExperienceEdit->value());
    if (_critterKillTypeEdit)
        critterData.killType = static_cast<uint32_t>(_critterKillTypeEdit->value());
    if (_critterDamageTypeEdit)
        critterData.damageType = static_cast<uint32_t>(_critterDamageTypeEdit->value());

    // Save SPECIAL stats (STR, PER, END, CHR, INT, AGL, LCK)
    for (int i = 0; i < 7; ++i) {
        if (_critterSpecialStatEdits[i]) {
            critterData.specialStats[i] = static_cast<uint32_t>(_critterSpecialStatEdits[i]->value());
        }
    }

    // Save damage thresholds (7 damage types)
    for (int i = 0; i < 7; ++i) {
        if (_critterDamageThresholdEdits[i]) {
            critterData.damageThreshold[i] = static_cast<uint32_t>(_critterDamageThresholdEdits[i]->value());
        }
    }

    // Save damage resistances (9 damage types)
    for (int i = 0; i < 9; ++i) {
        if (_critterDamageResistEdits[i]) {
            critterData.damageResist[i] = static_cast<uint32_t>(_critterDamageResistEdits[i]->value());
        }
    }

    // Save skills (18 different skills)
    for (int i = 0; i < 18; ++i) {
        if (_critterSkillEdits[i]) {
            critterData.skills[i] = static_cast<uint32_t>(_critterSkillEdits[i]->value());
        }
    }

    // Save bonus SPECIAL stats (7 stats)
    for (int i = 0; i < 7; ++i) {
        if (_critterBonusSpecialStatEdits[i]) {
            critterData.bonusSpecialStats[i] = static_cast<uint32_t>(_critterBonusSpecialStatEdits[i]->value());
        }
    }

    // Save bonus derived stats
    if (_critterBonusHealthPointsEdit)
        critterData.bonusHealthPoints = static_cast<uint32_t>(_critterBonusHealthPointsEdit->value());
    if (_critterBonusActionPointsEdit)
        critterData.bonusActionPoints = static_cast<uint32_t>(_critterBonusActionPointsEdit->value());
    if (_critterBonusArmorClassEdit)
        critterData.bonusArmorClass = static_cast<uint32_t>(_critterBonusArmorClassEdit->value());
    if (_critterBonusMeleeDamageEdit)
        critterData.bonusMeleeDamage = static_cast<uint32_t>(_critterBonusMeleeDamageEdit->value());
    if (_critterBonusCarryWeightEdit)
        critterData.bonusCarryWeight = static_cast<uint32_t>(_critterBonusCarryWeightEdit->value());
    if (_critterBonusSequenceEdit)
        critterData.bonusSequence = static_cast<uint32_t>(_critterBonusSequenceEdit->value());
    if (_critterBonusHealingRateEdit)
        critterData.bonusHealingRate = static_cast<uint32_t>(_critterBonusHealingRateEdit->value());
    if (_critterBonusCriticalChanceEdit)
        critterData.bonusCriticalChance = static_cast<uint32_t>(_critterBonusCriticalChanceEdit->value());
    if (_critterBonusBetterCriticalsEdit)
        critterData.bonusBetterCriticals = static_cast<uint32_t>(_critterBonusBetterCriticalsEdit->value());
    if (_critterBonusAgeEdit)
        critterData.bonusAge = static_cast<uint32_t>(_critterBonusAgeEdit->value());
    if (_critterBonusGenderEdit)
        critterData.bonusGender = static_cast<uint32_t>(_critterBonusGenderEdit->value());

    // Save bonus damage thresholds (8 damage types)
    for (int i = 0; i < 8; ++i) {
        if (_critterBonusDamageThresholdEdits[i]) {
            critterData.bonusDamageThreshold[i] = static_cast<uint32_t>(_critterBonusDamageThresholdEdits[i]->value());
        }
    }

    // Save bonus damage resistances (8 damage types)
    for (int i = 0; i < 8; ++i) {
        if (_critterBonusDamageResistanceEdits[i]) {
            critterData.bonusDamageResistance[i] = static_cast<uint32_t>(_critterBonusDamageResistanceEdits[i]->value());
        }
    }
}

void ProEditorDialog::saveSceneryData() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::SCENERY) {
        spdlog::warn("ProEditorDialog: Cannot save scenery data - not a scenery PRO");
        return;
    }

    auto& sceneryData = _pro->sceneryData;

    // Save basic scenery properties
    if (_sceneryMaterialIdEdit)
        sceneryData.materialId = static_cast<uint32_t>(_sceneryMaterialIdEdit->currentIndex());
    if (_scenerySoundIdEdit)
        sceneryData.soundId = static_cast<uint8_t>(_scenerySoundIdEdit->value());

    // Update object subtype if scenery type changed
    if (_sceneryTypeCombo) {
        _pro->setObjectSubtypeId(static_cast<unsigned int>(_sceneryTypeCombo->currentIndex()));
    }

    // Save type-specific data based on current scenery subtype
    Pro::SCENERY_TYPE sceneryType = static_cast<Pro::SCENERY_TYPE>(_pro->objectSubtypeId());

    switch (sceneryType) {
        case Pro::SCENERY_TYPE::DOOR:
            if (_doorWalkThroughCheck)
                sceneryData.doorData.walkThroughFlag = _doorWalkThroughCheck->isChecked() ? 1 : 0;
            if (_doorUnknownEdit)
                sceneryData.doorData.unknownField = static_cast<uint32_t>(_doorUnknownEdit->value());
            break;

        case Pro::SCENERY_TYPE::STAIRS:
            if (_stairsDestTileEdit)
                sceneryData.stairsData.destTile = static_cast<uint32_t>(_stairsDestTileEdit->value());
            if (_stairsDestElevationEdit)
                sceneryData.stairsData.destElevation = static_cast<uint32_t>(_stairsDestElevationEdit->value());
            break;

        case Pro::SCENERY_TYPE::ELEVATOR:
            if (_elevatorTypeEdit)
                sceneryData.elevatorData.elevatorType = static_cast<uint32_t>(_elevatorTypeEdit->value());
            if (_elevatorLevelEdit)
                sceneryData.elevatorData.elevatorLevel = static_cast<uint32_t>(_elevatorLevelEdit->value());
            break;

        case Pro::SCENERY_TYPE::LADDER_BOTTOM:
        case Pro::SCENERY_TYPE::LADDER_TOP:
            if (_ladderDestTileElevationEdit)
                sceneryData.ladderData.destTileAndElevation = static_cast<uint32_t>(_ladderDestTileElevationEdit->value());
            break;

        case Pro::SCENERY_TYPE::GENERIC:
        default:
            if (_genericUnknownEdit)
                sceneryData.genericData.unknownField = static_cast<uint32_t>(_genericUnknownEdit->value());
            break;
    }
}

void ProEditorDialog::loadWallData() {
    // Wall data is simple - just load the materialId value that's already been read from the file
    // The UI field is set up in setupCommonTab when _pro->type() == Pro::OBJECT_TYPE::WALL
}

void ProEditorDialog::loadTileData() {
    // Tile data is simple - just load the materialId value that's already been read from the file
    // The UI field is set up in setupCommonTab when _pro->type() == Pro::OBJECT_TYPE::TILE
}

void ProEditorDialog::saveWallData() {
    // Delegate to the widget
    if (_wallWidget) {
        _wallWidget->saveToPro(_pro);
    }
}

void ProEditorDialog::saveTileData() {
    // Delegate to the widget
    if (_tileWidget) {
        _tileWidget->saveToPro(_pro);
    }
}

void ProEditorDialog::loadNameAndDescription() {
    if (!_pro || !_nameLabel || !_descriptionEdit) {
        return;
    }

    try {
        const auto* msgFile = ProHelper::msgFile(_resources, _pro->type());
        if (!msgFile) {
            _nameLabel->setText("MSG file not found");
            _descriptionEdit->setText("Could not load MSG file for " + QString::fromStdString(_pro->typeToString()));
            spdlog::warn("ProEditorDialog::loadNameAndDescription() - MSG file not found for {}", _pro->typeToString());
            return;
        }

        uint32_t messageId = _pro->header.message_id;

        // Get name (message at messageId)
        std::string name;
        try {
            const auto& nameMessage = const_cast<Msg*>(msgFile)->message(messageId);
            name = nameMessage.text;
        } catch (const std::exception& e) {
            name = "No name (ID: " + std::to_string(messageId) + ")";
            spdlog::warn("ProEditorDialog::loadNameAndDescription() - Missing name {} for type {} ({})",
                messageId,
                _pro->typeToString(),
                e.what());
        }

        // Get description (message at messageId + 1)
        std::string description;
        try {
            const auto& descMessage = const_cast<Msg*>(msgFile)->message(messageId + 1);
            description = descMessage.text;
        } catch (const std::exception& e) {
            description = "No description available (ID: " + std::to_string(messageId + 1) + ")";
            spdlog::warn("ProEditorDialog::loadNameAndDescription() - Missing description {} for type {} ({})",
                messageId + 1,
                _pro->typeToString(),
                e.what());
        }

        // Update UI
        _nameLabel->setText(QString::fromStdString(name));
        _descriptionEdit->setText(QString::fromStdString(description));

    } catch (const std::exception& e) {
        _nameLabel->setText("Error loading name");
        _descriptionEdit->setText(QString("Error: %1").arg(e.what()));
        spdlog::error("ProEditorDialog::loadNameAndDescription() - Exception: {}", e.what());
    }
}

void ProEditorDialog::updateWindowTitle() {
    QString objectName = "Unknown";
    QString objectType = "Object";

    // Get object name from name label or fallback
    if (_nameLabel && !_nameLabel->text().isEmpty()) {
        objectName = _nameLabel->text();
    } else {
        objectName = QString("Object %1").arg(_pro->header.PID, 8, 16, QChar('0')).toUpper();
    }

    // Get object type based on PRO type
    try {
        Pro::OBJECT_TYPE proType = _pro->type();

        switch (proType) {
            case Pro::OBJECT_TYPE::ITEM: {
                // For items, get the specific item subtype
                Pro::ITEM_TYPE itemType = _pro->itemType();
                switch (itemType) {
                    case Pro::ITEM_TYPE::ARMOR:
                        objectType = "Armor";
                        break;
                    case Pro::ITEM_TYPE::CONTAINER:
                        objectType = "Container";
                        break;
                    case Pro::ITEM_TYPE::DRUG:
                        objectType = "Drug";
                        break;
                    case Pro::ITEM_TYPE::WEAPON:
                        objectType = "Weapon";
                        break;
                    case Pro::ITEM_TYPE::AMMO:
                        objectType = "Ammo";
                        break;
                    case Pro::ITEM_TYPE::MISC:
                        objectType = "Misc Item";
                        break;
                    case Pro::ITEM_TYPE::KEY:
                        objectType = "Key";
                        break;
                    default:
                        objectType = "Item";
                        break;
                }
                break;
            }
            case Pro::OBJECT_TYPE::CRITTER:
                objectType = "Critter";
                break;
            case Pro::OBJECT_TYPE::SCENERY:
                objectType = "Scenery";
                break;
            case Pro::OBJECT_TYPE::WALL:
                objectType = "Wall";
                break;
            case Pro::OBJECT_TYPE::TILE:
                objectType = "Tile";
                break;
            case Pro::OBJECT_TYPE::MISC:
                objectType = "Misc";
                break;
            default:
                objectType = "Object";
                break;
        }
    } catch (const std::exception& e) {
        spdlog::warn("ProEditorDialog::updateWindowTitle() - Error getting object type: {}", e.what());
        objectType = "Object";
    }

    QString newTitle = QString("%1 (%2) - PRO editor").arg(objectName, objectType);
    setWindowTitle(newTitle);

    spdlog::debug("ProEditorDialog::updateWindowTitle() - Set title to: {}", newTitle.toStdString());
}

void ProEditorDialog::onPlayPauseClicked() {
    if (!_animationController->hasMultipleFrames()) {
        return; // Nothing to animate
    }

    if (_animationController->isPlaying()) {
        _animationController->pause();
    } else {
        _animationController->play();
    }
}

void ProEditorDialog::onFrameSliderChanged(int frame) {
    // User manually moved the slider - update controller
    _animationController->setFrame(frame);
}

void ProEditorDialog::onAnimationFrameChanged(int frame) {
    // Controller changed frame - update UI
    _frameLabel->setText(QString("%1/%2").arg(frame + 1).arg(_animationController->totalFrames()));

    // Update slider without triggering valueChanged (block signals)
    _frameSlider->blockSignals(true);
    _frameSlider->setValue(frame);
    _frameSlider->blockSignals(false);

    // Update preview with new frame
    const QPixmap& pixmap = _animationController->frame(frame);
    if (!pixmap.isNull() && _previewLabel) {
        _previewLabel->setPixmap(pixmap);
    }
}

void ProEditorDialog::onDirectionChanged(int direction) {
    if (direction != _animationController->currentDirection()) {
        _animationController->setDirection(direction);
        // Reload frames for new direction
        loadAnimationFrames();
    }
}

void ProEditorDialog::loadAnimationFrames() {
    _animationController->clearFrames();

    if (!_pro) {
        return;
    }

    try {
        int32_t previewFid = getPreviewFid();
        if (previewFid <= 0) {
            return;
        }

        std::string frmPath = _resources.frmResolver().resolve(static_cast<unsigned int>(previewFid));

        if (frmPath.empty()) {
            return;
        }

        const auto* frm = _resources.repository().load<Frm>(frmPath);
        if (!frm) {
            return;
        }

        const auto& directions = frm->directions();
        int currentDirection = _animationController->currentDirection();
        if (directions.empty() || currentDirection >= static_cast<int>(directions.size())) {
            return;
        }

        const auto& direction = directions[currentDirection];
        const auto& frames = direction.frames();

        int totalDirections = static_cast<int>(directions.size());
        _animationController->setTotalDirections(totalDirections);

        const Pal* palette = _resources.repository().load<Pal>("color.pal");
        if (!palette) {
            return;
        }

        std::vector<QPixmap> frameCache;
        frameCache.reserve(frames.size());
        QSize frameTargetSize(200, 200);

        for (const auto& frame : frames) {
            QPixmap thumbnail = FrmThumbnailGenerator::fromFrame(frame, palette, frameTargetSize);
            frameCache.push_back(thumbnail);
        }

        _animationController->loadFrames(std::move(frameCache));

        int totalFrames = _animationController->totalFrames();
        _frameSlider->setMaximum(totalFrames > 0 ? totalFrames - 1 : 0);
        _frameSlider->setValue(0);
        _frameLabel->setText(QString("1/%1").arg(totalFrames));

        _directionCombo->setEnabled(totalDirections > 1);

        bool shouldEnableAnimation = false;
        if (_pro) {
            if (_pro->type() == Pro::OBJECT_TYPE::CRITTER) {
                shouldEnableAnimation = (totalFrames > 1 || totalDirections > 1);
            } else if (_pro->type() == Pro::OBJECT_TYPE::SCENERY) {
                shouldEnableAnimation = (totalFrames > 1 || totalDirections > 1);
            }
        }
        _animationControls->setEnabled(shouldEnableAnimation);
        _animationControls->setVisible(shouldEnableAnimation);

        const QPixmap& firstFrame = _animationController->frame(0);
        if (!firstFrame.isNull() && _previewLabel) {
            _previewLabel->setPixmap(firstFrame);
        }

    } catch (const std::exception& e) {
        spdlog::warn("ProEditorDialog: Exception loading animation frames: {}", e.what());
        _animationControls->setEnabled(false);
    }
}

void ProEditorDialog::onObjectFlagChanged() {
}

void ProEditorDialog::onTransparencyFlagChanged() {
}

void ProEditorDialog::loadObjectFlags(uint32_t flags) {
    Q_UNUSED(flags);
}

QComboBox* ProEditorDialog::createMaterialComboBox(const QString& tooltip) {
    QComboBox* comboBox = new QComboBox();
    comboBox->addItems(game::enums::materialTypes(_resources));
    if (!tooltip.isEmpty()) {
        comboBox->setToolTip(tooltip);
    }
    return comboBox;
}

QString ProEditorDialog::getFrmFilename(int32_t fid) {
    if (fid <= 0) {
        return "No FRM";
    }
    std::string frmPath = _resources.frmResolver().resolve(static_cast<unsigned int>(fid));
    if (frmPath.empty()) {
        return QString("Invalid FID (%1)").arg(fid);
    }
    return QString::fromStdString(std::filesystem::path(frmPath).filename().string());
}

void ProEditorDialog::onCritterHeadFidSelectorClicked() {
    openFrmSelectorForLabel(_critterHeadFIDLabel, &_critterHeadFID, 1); // Object type 1 for critters
}

void ProEditorDialog::onObjectFidChangeRequested() {
    if (_pro) {
        ObjectPreviewWidget* senderWidget = qobject_cast<ObjectPreviewWidget*>(sender());
        if (!senderWidget) {
            return;
        }
        FrmSelectorDialog dialog(_resources, this);

        uint32_t objectTypeFilter = 0;
        switch (_pro->type()) {
            case Pro::OBJECT_TYPE::CRITTER:
                objectTypeFilter = 1;
                break;
            case Pro::OBJECT_TYPE::SCENERY:
                objectTypeFilter = 2;
                break;
            case Pro::OBJECT_TYPE::WALL:
                objectTypeFilter = 3;
                break;
            case Pro::OBJECT_TYPE::TILE:
                objectTypeFilter = 4;
                break;
            case Pro::OBJECT_TYPE::MISC:
                objectTypeFilter = 5;
                break;
            default:
                objectTypeFilter = 0;
                break;
        }

        uint32_t initialFid = 0;
        if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
            objectTypeFilter = 0;
            if (senderWidget == _inventoryPreviewWidget) {
                initialFid = static_cast<uint32_t>(_pro->commonItemData.inventoryFID > 0 ? _pro->commonItemData.inventoryFID : _pro->header.FID);
            } else if (senderWidget == _groundPreviewWidget) {
                initialFid = static_cast<uint32_t>(_pro->header.FID);
            }
        } else {
            initialFid = static_cast<uint32_t>(_pro->header.FID);
        }

        dialog.setObjectTypeFilter(objectTypeFilter);
        dialog.setInitialFrmPid(initialFid);

        if (dialog.exec() == QDialog::Accepted) {
            uint32_t selectedFrmPid = dialog.getSelectedFrmPid();
            if (selectedFrmPid > 0) {
                if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
                    if (senderWidget == _inventoryPreviewWidget) {
                        _pro->commonItemData.inventoryFID = static_cast<int32_t>(selectedFrmPid);
                        updateInventoryPreview();
                    } else if (senderWidget == _groundPreviewWidget) {
                        _pro->header.FID = static_cast<int32_t>(selectedFrmPid);
                        updateGroundPreview();
                    }
                } else {
                    _pro->header.FID = static_cast<int32_t>(selectedFrmPid);
                    std::string frmPath = _resources.frmResolver().resolve(selectedFrmPid);
                    senderWidget->setFrmPath(QString::fromStdString(frmPath));
                    senderWidget->setFid(static_cast<int32_t>(selectedFrmPid));
                }
            }
        }
    }
}

void ProEditorDialog::onObjectFidChanged(int32_t newFid) {
    if (_pro && _pro->type() != Pro::OBJECT_TYPE::ITEM) {
        _pro->header.FID = newFid;

        std::string frmPath = _resources.frmResolver().resolve(static_cast<unsigned int>(newFid));
        if (_objectPreviewWidget) {
            _objectPreviewWidget->setFrmPath(QString::fromStdString(frmPath));
            _objectPreviewWidget->setFid(newFid);
        }
    }
}

void ProEditorDialog::loadStatAndPerkNames() {
    _statNames = game::enums::statNames(_resources);
    _perkOptions = game::enums::allPerkOptions(_resources);
}

void ProEditorDialog::onCritterFlagChanged() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::CRITTER) {
        return;
    }

    uint32_t flags = 0;

    if (_critterBarterCheck && _critterBarterCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_BARTER);
    if (_critterNoStealCheck && _critterNoStealCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_STEAL);
    if (_critterNoDropCheck && _critterNoDropCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_DROP);
    if (_critterNoLimbsCheck && _critterNoLimbsCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_LIMBS);
    if (_critterNoAgeCheck && _critterNoAgeCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_AGE);
    if (_critterNoHealCheck && _critterNoHealCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_HEAL);
    if (_critterInvulnerableCheck && _critterInvulnerableCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_INVULNERABLE);
    if (_critterNoFlattenCheck && _critterNoFlattenCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_FLATTEN);
    if (_critterSpecialDeathCheck && _critterSpecialDeathCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_SPECIAL_DEATH);
    if (_critterLongLimbsCheck && _critterLongLimbsCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_LONG_LIMBS);
    if (_critterNoKnockbackCheck && _critterNoKnockbackCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_KNOCKBACK);

    _pro->critterData.flags = flags;

    if (_critterFlagsEdit) {
        _critterFlagsEdit->setValue(static_cast<int>(flags));
    }
}

void ProEditorDialog::updateFilenameLabel() {
    if (!_pro || !_filenameEdit) {
        return;
    }

    try {
        std::string proPath = ProHelper::basePath(_resources, _pro->header.PID);

        size_t lastSlash = proPath.find_last_of('/');
        std::string filename;
        if (lastSlash != std::string::npos) {
            filename = proPath.substr(lastSlash + 1);
        } else {
            filename = proPath;
        }

        _filenameEdit->setText(QString::fromStdString(filename));
    } catch (const std::exception& e) {
        _filenameEdit->setText("(Unknown)");
        spdlog::warn("ProEditorDialog::updateFilenameLabel() - Error: {}", e.what());
    }
}

} // namespace geck
