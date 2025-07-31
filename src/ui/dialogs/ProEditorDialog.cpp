#include "ProEditorDialog.h"

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
#include "FrmSelectorDialog.h"

namespace geck {

ProEditorDialog::ProEditorDialog(std::shared_ptr<Pro> pro, QWidget* parent)
    : QDialog(parent)
    , _pro(pro)
    , _mainLayout(nullptr)
    , _contentLayout(nullptr)
    , _tabWidget(nullptr)
    , _buttonBox(nullptr)
    , _previewPanel(nullptr)
    , _previewLayout(nullptr)
    , _previewGroup(nullptr)
    , _previewLabel(nullptr)
    , _dualPreviewWidget(nullptr)
    , _dualPreviewLayout(nullptr)
    , _inventoryPreviewGroup(nullptr)
    , _groundPreviewGroup(nullptr)
    , _inventoryPreviewLabel(nullptr)
    , _groundPreviewLabel(nullptr)
    , _animationControls(nullptr)
    , _animationLayout(nullptr)
    , _playPauseButton(nullptr)
    , _frameSlider(nullptr)
    , _frameLabel(nullptr)
    , _directionCombo(nullptr)
    , _animationTimer(nullptr)
    , _currentFrame(0)
    , _currentDirection(0)
    , _totalFrames(0)
    , _totalDirections(0)
    , _isAnimating(false)
    , _validationPanel(nullptr)
    , _validationLayout(nullptr)
    , _validationGroup(nullptr)
    , _validationList(nullptr)
    , _validationToggleButton(nullptr)
    , _validationStatusLabel(nullptr)
    , _validationPanelVisible(false)
    , _nameLabel(nullptr)
    , _descriptionLabel(nullptr)
    , _fidSelectorButton(nullptr)
    , _inventoryFIDSelectorButton(nullptr)
    , _armorMaleFIDSelectorButton(nullptr)
    , _armorFemaleFIDSelectorButton(nullptr)
    , _extendedFlagsGroup(nullptr)
    , _animationPrimaryEdit(nullptr)
    , _animationSecondaryEdit(nullptr)
    , _bigGunCheck(nullptr)
    , _twoHandedCheck(nullptr)
    , _canUseCheck(nullptr)
    , _canUseOnCheck(nullptr)
    , _generalFlagCheck(nullptr)
    , _interactionFlagCheck(nullptr)
    , _itemHiddenCheck(nullptr)
    , _lightFlag1Check(nullptr)
    , _lightFlag2Check(nullptr)
    , _lightFlag3Check(nullptr)
    , _lightFlag4Check(nullptr)
    , _flagsExtRawEdit(nullptr)
    , _sidEdit(nullptr)
    , _materialIdEdit(nullptr)
    , _containerSizeEdit(nullptr)
    , _weaponTab(nullptr)
    , _weaponAnimationCombo(nullptr)
    , _weaponDamageMinEdit(nullptr)
    , _weaponDamageMaxEdit(nullptr)
    , _weaponDamageTypeCombo(nullptr)
    , _weaponRangePrimaryEdit(nullptr)
    , _weaponRangeSecondaryEdit(nullptr)
    , _weaponProjectilePIDEdit(nullptr)
    , _weaponMinStrengthEdit(nullptr)
    , _weaponAPPrimaryEdit(nullptr)
    , _weaponAPSecondaryEdit(nullptr)
    , _weaponCriticalFailEdit(nullptr)
    , _weaponPerkCombo(nullptr)
    , _weaponBurstRoundsEdit(nullptr)
    , _weaponAmmoTypeCombo(nullptr)
    , _weaponAmmoPIDEdit(nullptr)
    , _weaponAmmoCapacityEdit(nullptr)
    , _weaponSoundIdEdit(nullptr)
    , _weaponEnergyWeaponCheck(nullptr)
    , _weaponAIPriorityLabel(nullptr)
    , _armorAIPriorityLabel(nullptr) {
    
    
    setWindowTitle("PRO Editor");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
    resize(800, 600); // Increased size to accommodate preview panel
    
    
    // Initialize data structures with defaults
    memset(&_commonData, 0, sizeof(_commonData));
    memset(&_armorData, 0, sizeof(_armorData));
    memset(&_containerData, 0, sizeof(_containerData));
    memset(&_drugData, 0, sizeof(_drugData));
    memset(&_weaponData, 0, sizeof(_weaponData));
    memset(&_ammoData, 0, sizeof(_ammoData));
    memset(&_miscData, 0, sizeof(_miscData));
    memset(&_keyData, 0, sizeof(_keyData));
    memset(&_critterData, 0, sizeof(_critterData));
    memset(&_sceneryData, 0, sizeof(_sceneryData));
    
    
    setupUI();
    
    loadProData();
    
    // Load name and description from MSG files
    loadNameAndDescription();
    
    updateTabVisibility();
    // Call updatePreview after a brief delay to ensure all widgets are fully initialized
    QTimer::singleShot(0, this, &ProEditorDialog::updatePreview);
    // Update AI priority displays
    updateAIPriorityDisplays();
}

void ProEditorDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    
    // Create horizontal layout for tabs and preview
    _contentLayout = new QHBoxLayout();
    
    setupTabs();
    setupPreview();
    
    _contentLayout->addWidget(_tabWidget, 2); // Give tabs more space
    _contentLayout->addWidget(_previewPanel, 1); // Preview takes less space
    
    // Button box
    _buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(_buttonBox, &QDialogButtonBox::accepted, this, &ProEditorDialog::onAccept);
    connect(_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    _mainLayout->addLayout(_contentLayout);
    
    // Setup validation panel
    setupValidationPanel();
    
    _mainLayout->addWidget(_buttonBox);
}

void ProEditorDialog::setupTabs() {
    _tabWidget = new QTabWidget(this);
    
    setupCommonTab();
    setupArmorTab();
    setupContainerTab();
    setupDrugTab();
    setupWeaponTab();
    setupAmmoTab();
    setupMiscTab();
    setupKeyTab();
    setupCritterTab();
    setupSceneryTab();
}

void ProEditorDialog::setupCommonTab() {
    _commonTab = new QWidget();
    QFormLayout* layout = new QFormLayout(_commonTab);
    
    // PID (read-only display)
    QLabel* pidLabel = new QLabel(QString::number(_pro->header.PID));
    pidLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 2px; border: 1px solid #ccc; }");
    layout->addRow("PID:", pidLabel);
    
    // Name and Description (loaded from MSG files)
    _nameLabel = new QLabel("Loading...");
    _nameLabel->setStyleSheet("QLabel { background-color: #f0f8ff; padding: 4px; border: 1px solid #add8e6; font-weight: bold; }");
    //_nameLabel->setWordWrap(true);
    _nameLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::MinimumExpanding);
    layout->addRow("Name:", _nameLabel);
    
    _descriptionLabel = new QTextEdit("Loading...");
    _descriptionLabel->setStyleSheet("QTextEdit { background-color: #f0f8ff; padding: 4px; border: 1px solid #add8e6; }");
    _descriptionLabel->setReadOnly(true);
    _descriptionLabel->setMinimumHeight(30);
    _descriptionLabel->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _descriptionLabel->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _descriptionLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::MinimumExpanding);
    layout->addRow("Description:", _descriptionLabel);
    
    // Message ID
    _messageIdEdit = createSpinBox(0, 999999, "Message ID for displaying object name and description");
    _messageIdEdit->setValue(_pro->header.message_id);
    layout->addRow("Message ID:", _messageIdEdit);
    
    // FID with selector button
    QWidget* fidWidget = new QWidget();
    QHBoxLayout* fidLayout = new QHBoxLayout(fidWidget);
    fidLayout->setContentsMargins(0, 0, 0, 0);
    
    _fidEdit = createSpinBox(INT32_MIN, INT32_MAX, "Frame ID - determines the visual appearance of the object");
    _fidEdit->setValue(_pro->header.FID);
    
    _fidSelectorButton = new QPushButton("...");
    _fidSelectorButton->setMaximumWidth(30);
    _fidSelectorButton->setToolTip("Browse FRM files");
    connect(_fidSelectorButton, &QPushButton::clicked, this, &ProEditorDialog::onFidSelectorClicked);
    
    fidLayout->addWidget(_fidEdit);
    fidLayout->addWidget(_fidSelectorButton);
    layout->addRow("FID:", fidWidget);
    
    // Light Distance
    _lightDistanceEdit = createSpinBox(0, 999, "Distance that light from this object reaches (0 = no light)");
    _lightDistanceEdit->setValue(_pro->header.light_distance);
    layout->addRow("Light Distance:", _lightDistanceEdit);
    
    // Light Intensity
    _lightIntensityEdit = createSpinBox(0, 999, "Brightness of the light (higher values = brighter)");
    _lightIntensityEdit->setValue(_pro->header.light_intensity);
    layout->addRow("Light Intensity:", _lightIntensityEdit);
    
    // Flags (hex display)
    _flagsEdit = createHexSpinBox(INT32_MAX, "Object flags (hex) - controls special behaviors and properties");
    _flagsEdit->setValue(_pro->header.flags);
    layout->addRow("Flags:", _flagsEdit);
    
    // Extended Flags (not for TILE and MISC types)
    if (_pro->type() != Pro::OBJECT_TYPE::TILE && _pro->type() != Pro::OBJECT_TYPE::MISC) {
        setupExtendedFlagsGroup(layout);
    }
    
    
    // Script ID (not for TILE and MISC types)
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM || _pro->type() == Pro::OBJECT_TYPE::CRITTER || 
        _pro->type() == Pro::OBJECT_TYPE::SCENERY || _pro->type() == Pro::OBJECT_TYPE::WALL) {
        _sidEdit = createSpinBox(0, 999999, "Script ID - links to associated script file");
        _sidEdit->setValue(_pro->commonItemData.SID);
        layout->addRow("Script ID:", _sidEdit);
    }
    
    // Add item-specific common fields (only for items)
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        // Material ID
        _materialIdEdit = new QSpinBox();
        _materialIdEdit->setRange(0, 999999);
        _materialIdEdit->setValue(_pro->commonItemData.materialId);
        _materialIdEdit->setToolTip("Material ID - determines material properties");
        connect(_materialIdEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        layout->addRow("Material ID:", _materialIdEdit);
        
        // Container Size
        _containerSizeEdit = new QSpinBox();
        _containerSizeEdit->setRange(0, 999999);
        _containerSizeEdit->setValue(_pro->commonItemData.containerSize);
        _containerSizeEdit->setToolTip("Container size - volume capacity for containers");
        connect(_containerSizeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        layout->addRow("Container Size:", _containerSizeEdit);
        // Weight
        _weightEdit = new QSpinBox();
        _weightEdit->setRange(0, 999999);
        _weightEdit->setValue(_pro->commonItemData.weight);
        _weightEdit->setToolTip("Weight in pounds - affects carry capacity");
        connect(_weightEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        layout->addRow("Weight:", _weightEdit);
        
        // Base Price
        _basePriceEdit = new QSpinBox();
        _basePriceEdit->setRange(0, 999999);
        _basePriceEdit->setValue(_pro->commonItemData.basePrice);
        _basePriceEdit->setToolTip("Base price in caps for trading");
        connect(_basePriceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        layout->addRow("Base Price:", _basePriceEdit);
        
        // Inventory FID with selector button
        QWidget* invFidWidget = new QWidget();
        QHBoxLayout* invFidLayout = new QHBoxLayout(invFidWidget);
        invFidLayout->setContentsMargins(0, 0, 0, 0);
        
        _inventoryFIDEdit = new QSpinBox();
        _inventoryFIDEdit->setRange(INT32_MIN, INT32_MAX);
        _inventoryFIDEdit->setValue(_pro->commonItemData.inventoryFID);
        _inventoryFIDEdit->setToolTip("Frame ID for inventory/interface view");
        connect(_inventoryFIDEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        
        _inventoryFIDSelectorButton = new QPushButton("...");
        _inventoryFIDSelectorButton->setMaximumWidth(30);
        _inventoryFIDSelectorButton->setToolTip("Browse FRM files for inventory view");
        connect(_inventoryFIDSelectorButton, &QPushButton::clicked, this, &ProEditorDialog::onInventoryFidSelectorClicked);
        
        invFidLayout->addWidget(_inventoryFIDEdit);
        invFidLayout->addWidget(_inventoryFIDSelectorButton);
        layout->addRow("Inventory FID:", invFidWidget);
        
        // Sound ID
        _soundIdEdit = new QSpinBox();
        _soundIdEdit->setRange(0, 255);
        _soundIdEdit->setValue(_pro->commonItemData.soundId);
        _soundIdEdit->setToolTip("Sound effect ID when using/manipulating the item");
        connect(_soundIdEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        layout->addRow("Sound ID:", _soundIdEdit);
    }
    
    _tabWidget->addTab(_commonTab, "Common");
}

void ProEditorDialog::setupExtendedFlagsGroup(QFormLayout* layout) {
    _extendedFlagsGroup = new QGroupBox("Extended Flags");
    QVBoxLayout* flagsLayout = new QVBoxLayout(_extendedFlagsGroup);
    
    // Determine which type-specific flags to show
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        switch (_pro->itemType()) {
            case Pro::ITEM_TYPE::WEAPON:
                setupWeaponExtendedFlags(flagsLayout);
                break;
            case Pro::ITEM_TYPE::CONTAINER:
                setupContainerExtendedFlags(flagsLayout);
                break;
            default:
                setupItemExtendedFlags(flagsLayout);
                break;
        }
    } else {
        setupOtherExtendedFlags(flagsLayout);
    }
    
    // Raw hex editor for advanced users (always shown)
    QGroupBox* rawGroup = new QGroupBox("Raw Editor (Advanced)");
    QFormLayout* rawLayout = new QFormLayout(rawGroup);
    
    _flagsExtRawEdit = createHexSpinBox(UINT32_MAX, "Raw extended flags value in hexadecimal");
    // Set initial value based on object type
    uint32_t initialFlags = _pro->type() == Pro::OBJECT_TYPE::ITEM ? 
        _pro->commonItemData.flagsExt : 
        (_pro->header.flags & 0xF0000000); // Only high bits for non-items
    _flagsExtRawEdit->setValue(initialFlags);
    connect(_flagsExtRawEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onExtendedFlagRawChanged);
    rawLayout->addRow("Raw Hex:", _flagsExtRawEdit);
    
    flagsLayout->addWidget(rawGroup);
    
    layout->addRow("Extended Flags:", _extendedFlagsGroup);
}

void ProEditorDialog::setupWeaponExtendedFlags(QVBoxLayout* layout) {
    // Animation flags group
    QGroupBox* animGroup = new QGroupBox("Animation");
    QFormLayout* animLayout = new QFormLayout(animGroup);
    
    _animationPrimaryEdit = new QSpinBox();
    _animationPrimaryEdit->setRange(0, 15);
    _animationPrimaryEdit->setValue(Pro::getAnimationPrimary(_pro->commonItemData.flagsExt));
    _animationPrimaryEdit->setToolTip("Primary attack animation index (0-15)");
    connect(_animationPrimaryEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onExtendedFlagChanged);
    animLayout->addRow("Primary Anim:", _animationPrimaryEdit);
    
    _animationSecondaryEdit = new QSpinBox();
    _animationSecondaryEdit->setRange(0, 15);
    _animationSecondaryEdit->setValue(Pro::getAnimationSecondary(_pro->commonItemData.flagsExt));
    _animationSecondaryEdit->setToolTip("Secondary attack animation index (0-15)");
    connect(_animationSecondaryEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onExtendedFlagChanged);
    animLayout->addRow("Secondary Anim:", _animationSecondaryEdit);
    
    layout->addWidget(animGroup);
    
    // Weapon behavior flags
    QGroupBox* behaviorGroup = new QGroupBox("Weapon Behavior");
    QVBoxLayout* behaviorLayout = new QVBoxLayout(behaviorGroup);
    
    _bigGunCheck = new QCheckBox("Big Gun");
    _bigGunCheck->setChecked(_pro->commonItemData.flagsExt & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::BIG_GUN));
    _bigGunCheck->setToolTip("Forces weapon to use Big Guns skill instead of Small Guns");
    connect(_bigGunCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    behaviorLayout->addWidget(_bigGunCheck);
    
    _twoHandedCheck = new QCheckBox("Two-Handed");
    _twoHandedCheck->setChecked(_pro->commonItemData.flagsExt & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::TWO_HANDED));
    _twoHandedCheck->setToolTip("Weapon requires both hands, prevents dual-wielding");
    connect(_twoHandedCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    behaviorLayout->addWidget(_twoHandedCheck);
    
    _itemHiddenCheck = new QCheckBox("Hidden Item");
    _itemHiddenCheck->setChecked(_pro->commonItemData.flagsExt & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::ITEM_HIDDEN));
    _itemHiddenCheck->setToolTip("Item is integral part of owner, cannot be dropped (creature weapons)");
    connect(_itemHiddenCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    behaviorLayout->addWidget(_itemHiddenCheck);
    
    layout->addWidget(behaviorGroup);
}

void ProEditorDialog::setupContainerExtendedFlags(QVBoxLayout* layout) {
    // Action flags group for containers
    QGroupBox* actionGroup = new QGroupBox("Container Actions");
    QVBoxLayout* actionLayout = new QVBoxLayout(actionGroup);
    
    _canUseCheck = new QCheckBox("Can Use");
    _canUseCheck->setChecked(_pro->commonItemData.flagsExt & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::CAN_USE));
    _canUseCheck->setToolTip("Container can be 'used' (automatically set for containers)");
    connect(_canUseCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    actionLayout->addWidget(_canUseCheck);
    
    _canUseOnCheck = new QCheckBox("Can Use On");
    _canUseOnCheck->setChecked(_pro->commonItemData.flagsExt & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::CAN_USE_ON));
    _canUseOnCheck->setToolTip("Container can be 'used on' target");
    connect(_canUseOnCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    actionLayout->addWidget(_canUseOnCheck);
    
    _interactionFlagCheck = new QCheckBox("Interaction Flag");
    _interactionFlagCheck->setChecked(_pro->commonItemData.flagsExt & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::INTERACTION_FLAG));
    _interactionFlagCheck->setToolTip("Related to item interactions");
    connect(_interactionFlagCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    actionLayout->addWidget(_interactionFlagCheck);
    
    layout->addWidget(actionGroup);
}

void ProEditorDialog::setupItemExtendedFlags(QVBoxLayout* layout) {
    // General item flags (for armor, drugs, ammo, misc, keys)
    QGroupBox* generalGroup = new QGroupBox("Item Actions");
    QVBoxLayout* generalLayout = new QVBoxLayout(generalGroup);
    
    _canUseCheck = new QCheckBox("Can Use");
    _canUseCheck->setChecked(_pro->commonItemData.flagsExt & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::CAN_USE));
    _canUseCheck->setToolTip("Item can be 'used'");
    connect(_canUseCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    generalLayout->addWidget(_canUseCheck);
    
    _canUseOnCheck = new QCheckBox("Can Use On");
    _canUseOnCheck->setChecked(_pro->commonItemData.flagsExt & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::CAN_USE_ON));
    _canUseOnCheck->setToolTip("Item can be 'used on' target (automatically set for drugs)");
    connect(_canUseOnCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    generalLayout->addWidget(_canUseOnCheck);
    
    _generalFlagCheck = new QCheckBox("General Flag");
    _generalFlagCheck->setChecked(_pro->commonItemData.flagsExt & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::GENERAL_FLAG));
    _generalFlagCheck->setToolTip("General purpose flag");
    connect(_generalFlagCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    generalLayout->addWidget(_generalFlagCheck);
    
    layout->addWidget(generalGroup);
}

void ProEditorDialog::setupOtherExtendedFlags(QVBoxLayout* layout) {
    // For critters, scenery, walls - show general flags
    QGroupBox* generalGroup = new QGroupBox("Extended Flags");
    QVBoxLayout* generalLayout = new QVBoxLayout(generalGroup);
    
    _generalFlagCheck = new QCheckBox("General Flag");
    _generalFlagCheck->setChecked(_pro->type() == Pro::OBJECT_TYPE::ITEM ? 
        (_pro->commonItemData.flagsExt & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::GENERAL_FLAG)) : 
        (_pro->header.flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::GENERAL_FLAG)));
    _generalFlagCheck->setToolTip("General purpose flag (scenery/walls/tiles)");
    connect(_generalFlagCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    generalLayout->addWidget(_generalFlagCheck);
    
    // Light flags for objects that can have light
    if (_pro->type() != Pro::OBJECT_TYPE::TILE) {
        QGroupBox* lightGroup = new QGroupBox("Light Flags");
        QVBoxLayout* lightLayout = new QVBoxLayout(lightGroup);
        
        uint32_t flagsToCheck = _pro->type() == Pro::OBJECT_TYPE::ITEM ? 
            _pro->commonItemData.flagsExt : _pro->header.flags;
        
        _lightFlag1Check = new QCheckBox("Light Flag 1");
        _lightFlag1Check->setChecked(flagsToCheck & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_1));
        _lightFlag1Check->setToolTip("Light rendering flag 1");
        connect(_lightFlag1Check, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
        lightLayout->addWidget(_lightFlag1Check);
        
        _lightFlag2Check = new QCheckBox("Light Flag 2");
        _lightFlag2Check->setChecked(flagsToCheck & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_2));
        _lightFlag2Check->setToolTip("Light rendering flag 2");
        connect(_lightFlag2Check, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
        lightLayout->addWidget(_lightFlag2Check);
        
        _lightFlag3Check = new QCheckBox("Light Flag 3");
        _lightFlag3Check->setChecked(flagsToCheck & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_3));
        _lightFlag3Check->setToolTip("Light rendering flag 3");
        connect(_lightFlag3Check, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
        lightLayout->addWidget(_lightFlag3Check);
        
        _lightFlag4Check = new QCheckBox("Light Flag 4");
        _lightFlag4Check->setChecked(flagsToCheck & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_4));
        _lightFlag4Check->setToolTip("Light rendering flag 4");
        connect(_lightFlag4Check, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
        lightLayout->addWidget(_lightFlag4Check);
        
        generalLayout->addWidget(lightGroup);
    }
    
    layout->addWidget(generalGroup);
}

void ProEditorDialog::setupArmorTab() {
    _armorTab = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(_armorTab);
    
    // Armor Class
    QGroupBox* acGroup = new QGroupBox("Armor Class");
    QFormLayout* acLayout = new QFormLayout(acGroup);
    
    _armorClassEdit = new QSpinBox();
    _armorClassEdit->setRange(0, 999);
    _armorClassEdit->setToolTip("Armor Class - higher values provide better protection");
    connect(_armorClassEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    acLayout->addRow("AC:", _armorClassEdit);
    
    mainLayout->addWidget(acGroup);
    
    // Damage Resistance/Threshold
    QGroupBox* damageGroup = new QGroupBox("Damage Resistance & Threshold");
    QGridLayout* damageLayout = new QGridLayout(damageGroup);
    
    const QStringList damageTypes = {"Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion"};
    
    damageLayout->addWidget(new QLabel("Type"), 0, 0);
    damageLayout->addWidget(new QLabel("Resist"), 0, 1);
    damageLayout->addWidget(new QLabel("Threshold"), 0, 2);
    
    for (int i = 0; i < 7; ++i) {
        damageLayout->addWidget(new QLabel(damageTypes[i]), i + 1, 0);
        
        _damageResistEdits[i] = new QSpinBox();
        _damageResistEdits[i]->setRange(0, 100);
        _damageResistEdits[i]->setSuffix("%");
        connect(_damageResistEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        damageLayout->addWidget(_damageResistEdits[i], i + 1, 1);
        
        _damageThresholdEdits[i] = new QSpinBox();
        _damageThresholdEdits[i]->setRange(0, 999);
        connect(_damageThresholdEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        damageLayout->addWidget(_damageThresholdEdits[i], i + 1, 2);
    }
    
    mainLayout->addWidget(damageGroup);
    
    // Perk and FIDs
    QGroupBox* miscGroup = new QGroupBox("Misc Properties");
    QFormLayout* miscLayout = new QFormLayout(miscGroup);
    
    _armorPerkCombo = new QComboBox();
    _armorPerkCombo->addItems({"None", "PowerArmor", "CombatArmor", "Other"});
    connect(_armorPerkCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    miscLayout->addRow("Perk:", _armorPerkCombo);
    
    // Male FID with selector button
    QWidget* maleFidWidget = new QWidget();
    QHBoxLayout* maleFidLayout = new QHBoxLayout(maleFidWidget);
    maleFidLayout->setContentsMargins(0, 0, 0, 0);
    
    _armorMaleFIDEdit = new QSpinBox();
    _armorMaleFIDEdit->setRange(INT32_MIN, INT32_MAX);
    _armorMaleFIDEdit->setToolTip("Frame ID for male character wearing this armor");
    connect(_armorMaleFIDEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    
    _armorMaleFIDSelectorButton = new QPushButton("...");
    _armorMaleFIDSelectorButton->setMaximumWidth(30);
    _armorMaleFIDSelectorButton->setToolTip("Browse FRM files for male armor");
    connect(_armorMaleFIDSelectorButton, &QPushButton::clicked, this, &ProEditorDialog::onArmorMaleFidSelectorClicked);
    
    maleFidLayout->addWidget(_armorMaleFIDEdit);
    maleFidLayout->addWidget(_armorMaleFIDSelectorButton);
    miscLayout->addRow("Male FID:", maleFidWidget);
    
    // Female FID with selector button
    QWidget* femaleFidWidget = new QWidget();
    QHBoxLayout* femaleFidLayout = new QHBoxLayout(femaleFidWidget);
    femaleFidLayout->setContentsMargins(0, 0, 0, 0);
    
    _armorFemaleFIDEdit = new QSpinBox();
    _armorFemaleFIDEdit->setRange(INT32_MIN, INT32_MAX);
    _armorFemaleFIDEdit->setToolTip("Frame ID for female character wearing this armor");
    connect(_armorFemaleFIDEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    
    _armorFemaleFIDSelectorButton = new QPushButton("...");
    _armorFemaleFIDSelectorButton->setMaximumWidth(30);
    _armorFemaleFIDSelectorButton->setToolTip("Browse FRM files for female armor");
    connect(_armorFemaleFIDSelectorButton, &QPushButton::clicked, this, &ProEditorDialog::onArmorFemaleFidSelectorClicked);
    
    femaleFidLayout->addWidget(_armorFemaleFIDEdit);
    femaleFidLayout->addWidget(_armorFemaleFIDSelectorButton);
    miscLayout->addRow("Female FID:", femaleFidWidget);
    
    // AI Priority display
    _armorAIPriorityLabel = new QLabel("0");
    _armorAIPriorityLabel->setStyleSheet("font-weight: bold; color: #0066CC;");
    _armorAIPriorityLabel->setToolTip("AI Priority = AC + all DT values + all DR values (used by AI to select best armor)");
    miscLayout->addRow("AI Priority:", _armorAIPriorityLabel);
    
    mainLayout->addWidget(miscGroup);
    mainLayout->addStretch();
    
    _tabWidget->addTab(_armorTab, "Armor");
}

void ProEditorDialog::setupContainerTab() {
    _containerTab = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(_containerTab);
    
    QFormLayout* formLayout = new QFormLayout();
    
    // Max Size
    _containerMaxSizeEdit = new QSpinBox();
    _containerMaxSizeEdit->setRange(1, 999999);
    connect(_containerMaxSizeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    formLayout->addRow("Max Size:", _containerMaxSizeEdit);
    
    mainLayout->addLayout(formLayout);
    
    // Action Flags
    QGroupBox* flagsGroup = new QGroupBox("Action Flags");
    QVBoxLayout* flagsLayout = new QVBoxLayout(flagsGroup);
    
    const QStringList flagNames = {"Use", "Use On", "Look", "Talk", "Pickup"};
    for (int i = 0; i < 5; ++i) {
        _containerFlagChecks[i] = new QCheckBox(flagNames[i]);
        connect(_containerFlagChecks[i], &QCheckBox::toggled, this, &ProEditorDialog::onCheckBoxChanged);
        flagsLayout->addWidget(_containerFlagChecks[i]);
    }
    
    mainLayout->addWidget(flagsGroup);
    mainLayout->addStretch();
    
    _tabWidget->addTab(_containerTab, "Container");
}

void ProEditorDialog::setupDrugTab() {
    _drugTab = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(_drugTab);
    
    // Immediate Effects
    QGroupBox* immediateGroup = new QGroupBox("Immediate Effects");
    QGridLayout* immediateLayout = new QGridLayout(immediateGroup);
    
    const QStringList statNames = {"STR", "PER", "END", "CHR", "INT", "AGL", "LCK", "HP", "AP", "AC", "Melee", "Carry", "Sequence", "Heal", "Critical"};
    
    immediateLayout->addWidget(new QLabel("Stat"), 0, 0);
    immediateLayout->addWidget(new QLabel("Amount"), 0, 1);
    
    for (int i = 0; i < 3; ++i) {
        _drugStatCombos[i] = new QComboBox();
        _drugStatCombos[i]->addItems(statNames);
        connect(_drugStatCombos[i], QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
        immediateLayout->addWidget(_drugStatCombos[i], i + 1, 0);
        
        _drugStatAmountEdits[i] = new QSpinBox();
        _drugStatAmountEdits[i]->setRange(-999, 999);
        connect(_drugStatAmountEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        immediateLayout->addWidget(_drugStatAmountEdits[i], i + 1, 1);
    }
    
    mainLayout->addWidget(immediateGroup);
    
    // First Delayed Effect
    QGroupBox* firstDelayGroup = new QGroupBox("First Delayed Effect");
    QGridLayout* firstDelayLayout = new QGridLayout(firstDelayGroup);
    
    _drugFirstDelayEdit = new QSpinBox();
    _drugFirstDelayEdit->setRange(0, 999);
    _drugFirstDelayEdit->setSuffix(" min");
    connect(_drugFirstDelayEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    firstDelayLayout->addWidget(new QLabel("Delay:"), 0, 0);
    firstDelayLayout->addWidget(_drugFirstDelayEdit, 0, 1);
    
    for (int i = 0; i < 3; ++i) {
        _drugFirstStatAmountEdits[i] = new QSpinBox();
        _drugFirstStatAmountEdits[i]->setRange(-999, 999);
        connect(_drugFirstStatAmountEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        firstDelayLayout->addWidget(new QLabel(QString("Stat %1 Amount:").arg(i)), i + 1, 0);
        firstDelayLayout->addWidget(_drugFirstStatAmountEdits[i], i + 1, 1);
    }
    
    mainLayout->addWidget(firstDelayGroup);
    
    // Second Delayed Effect
    QGroupBox* secondDelayGroup = new QGroupBox("Second Delayed Effect");
    QGridLayout* secondDelayLayout = new QGridLayout(secondDelayGroup);
    
    _drugSecondDelayEdit = new QSpinBox();
    _drugSecondDelayEdit->setRange(0, 999);
    _drugSecondDelayEdit->setSuffix(" min");
    connect(_drugSecondDelayEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    secondDelayLayout->addWidget(new QLabel("Delay:"), 0, 0);
    secondDelayLayout->addWidget(_drugSecondDelayEdit, 0, 1);
    
    for (int i = 0; i < 3; ++i) {
        _drugSecondStatAmountEdits[i] = new QSpinBox();
        _drugSecondStatAmountEdits[i]->setRange(-999, 999);
        connect(_drugSecondStatAmountEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        secondDelayLayout->addWidget(new QLabel(QString("Stat %1 Amount:").arg(i)), i + 1, 0);
        secondDelayLayout->addWidget(_drugSecondStatAmountEdits[i], i + 1, 1);
    }
    
    mainLayout->addWidget(secondDelayGroup);
    
    // Addiction
    QGroupBox* addictionGroup = new QGroupBox("Addiction");
    QFormLayout* addictionLayout = new QFormLayout(addictionGroup);
    
    _drugAddictionChanceEdit = new QSpinBox();
    _drugAddictionChanceEdit->setRange(0, 100);
    _drugAddictionChanceEdit->setSuffix("%");
    connect(_drugAddictionChanceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    addictionLayout->addRow("Chance:", _drugAddictionChanceEdit);
    
    _drugAddictionPerkCombo = new QComboBox();
    _drugAddictionPerkCombo->addItems({"None", "JetAddiction", "AlcoholAddiction", "Other"});
    connect(_drugAddictionPerkCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    addictionLayout->addRow("Perk:", _drugAddictionPerkCombo);
    
    _drugAddictionDelayEdit = new QSpinBox();
    _drugAddictionDelayEdit->setRange(0, 999);
    _drugAddictionDelayEdit->setSuffix(" min");
    connect(_drugAddictionDelayEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    addictionLayout->addRow("Delay:", _drugAddictionDelayEdit);
    
    mainLayout->addWidget(addictionGroup);
    mainLayout->addStretch();
    
    _tabWidget->addTab(_drugTab, "Drug");
}

void ProEditorDialog::setupWeaponTab() {
    _weaponTab = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(_weaponTab);
    
    // Basic Properties
    QGroupBox* basicGroup = new QGroupBox("Basic Properties");
    QFormLayout* basicLayout = new QFormLayout(basicGroup);
    
    _weaponAnimationCombo = new QComboBox();
    _weaponAnimationCombo->addItems({"None", "OneHanded", "TwoHanded", "Rifle"});
    connect(_weaponAnimationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    basicLayout->addRow("Animation:", _weaponAnimationCombo);
    
    _weaponDamageMinEdit = new QSpinBox();
    _weaponDamageMinEdit->setRange(0, 999);
    connect(_weaponDamageMinEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    basicLayout->addRow("Min Damage:", _weaponDamageMinEdit);
    
    _weaponDamageMaxEdit = new QSpinBox();
    _weaponDamageMaxEdit->setRange(0, 999);
    connect(_weaponDamageMaxEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    basicLayout->addRow("Max Damage:", _weaponDamageMaxEdit);
    
    _weaponDamageTypeCombo = new QComboBox();
    _weaponDamageTypeCombo->addItems({"Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion"});
    connect(_weaponDamageTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    basicLayout->addRow("Damage Type:", _weaponDamageTypeCombo);
    
    _weaponMinStrengthEdit = new QSpinBox();
    _weaponMinStrengthEdit->setRange(1, 10);
    connect(_weaponMinStrengthEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    basicLayout->addRow("Min Strength:", _weaponMinStrengthEdit);
    
    mainLayout->addWidget(basicGroup);
    
    // Attack Properties
    QGroupBox* attackGroup = new QGroupBox("Attack Properties");
    QFormLayout* attackLayout = new QFormLayout(attackGroup);
    
    _weaponRangePrimaryEdit = new QSpinBox();
    _weaponRangePrimaryEdit->setRange(1, 50);
    connect(_weaponRangePrimaryEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    attackLayout->addRow("Primary Range:", _weaponRangePrimaryEdit);
    
    _weaponRangeSecondaryEdit = new QSpinBox();
    _weaponRangeSecondaryEdit->setRange(1, 50);
    connect(_weaponRangeSecondaryEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    attackLayout->addRow("Secondary Range:", _weaponRangeSecondaryEdit);
    
    _weaponProjectilePIDEdit = new QSpinBox();
    _weaponProjectilePIDEdit->setRange(INT32_MIN, INT32_MAX);
    connect(_weaponProjectilePIDEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    attackLayout->addRow("Projectile PID:", _weaponProjectilePIDEdit);
    
    _weaponAPPrimaryEdit = new QSpinBox();
    _weaponAPPrimaryEdit->setRange(1, 10);
    connect(_weaponAPPrimaryEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    attackLayout->addRow("Primary AP Cost:", _weaponAPPrimaryEdit);
    
    _weaponAPSecondaryEdit = new QSpinBox();
    _weaponAPSecondaryEdit->setRange(1, 10);
    connect(_weaponAPSecondaryEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    attackLayout->addRow("Secondary AP Cost:", _weaponAPSecondaryEdit);
    
    _weaponCriticalFailEdit = new QSpinBox();
    _weaponCriticalFailEdit->setRange(0, 100);
    connect(_weaponCriticalFailEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    attackLayout->addRow("Critical Fail:", _weaponCriticalFailEdit);
    
    _weaponPerkCombo = new QComboBox();
    _weaponPerkCombo->addItems({"None", "Penetrate", "Flameboy", "Bonus HtH Damage", "Bonus HtH Attacks", 
                                "Bonus Rate of Fire", "Bonus Ranged Damage", "Silent Running", "Weapon Long Range"});
    connect(_weaponPerkCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    attackLayout->addRow("Perk:", _weaponPerkCombo);
    
    mainLayout->addWidget(attackGroup);
    
    // Ammo Properties
    QGroupBox* ammoGroup = new QGroupBox("Ammo Properties");
    QFormLayout* ammoLayout = new QFormLayout(ammoGroup);
    
    _weaponAmmoTypeCombo = new QComboBox();
    _weaponAmmoTypeCombo->addItems({"None", ".223", "5mm", ".44", "14mm", "Rocket", "Energy"});
    connect(_weaponAmmoTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    ammoLayout->addRow("Ammo Type:", _weaponAmmoTypeCombo);
    
    _weaponAmmoPIDEdit = new QSpinBox();
    _weaponAmmoPIDEdit->setRange(INT32_MIN, INT32_MAX);
    connect(_weaponAmmoPIDEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    ammoLayout->addRow("Ammo PID:", _weaponAmmoPIDEdit);
    
    _weaponAmmoCapacityEdit = new QSpinBox();
    _weaponAmmoCapacityEdit->setRange(1, 999);
    connect(_weaponAmmoCapacityEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    ammoLayout->addRow("Capacity:", _weaponAmmoCapacityEdit);
    
    _weaponBurstRoundsEdit = new QSpinBox();
    _weaponBurstRoundsEdit->setRange(1, 50);
    connect(_weaponBurstRoundsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    ammoLayout->addRow("Burst Rounds:", _weaponBurstRoundsEdit);
    
    _weaponSoundIdEdit = new QSpinBox();
    _weaponSoundIdEdit->setRange(0, 255);
    connect(_weaponSoundIdEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    ammoLayout->addRow("Sound ID:", _weaponSoundIdEdit);
    
    mainLayout->addWidget(ammoGroup);
    
    // Advanced weapon flags
    QGroupBox* flagsGroup = new QGroupBox("Weapon Flags");
    QVBoxLayout* flagsLayout = new QVBoxLayout(flagsGroup);
    
    _weaponEnergyWeaponCheck = new QCheckBox("Energy Weapon");
    _weaponEnergyWeaponCheck->setToolTip("Forces weapon to use Energy Weapons skill regardless of damage type (sfall 4.2/3.8.20)");
    connect(_weaponEnergyWeaponCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCheckBoxChanged);
    flagsLayout->addWidget(_weaponEnergyWeaponCheck);
    
    mainLayout->addWidget(flagsGroup);
    
    // AI Priority display
    QGroupBox* aiGroup = new QGroupBox("AI Information");
    QFormLayout* aiLayout = new QFormLayout(aiGroup);
    
    _weaponAIPriorityLabel = new QLabel("0");
    _weaponAIPriorityLabel->setStyleSheet("font-weight: bold; color: #0066CC;");
    _weaponAIPriorityLabel->setToolTip("AI Priority = damage + range + accuracy factors (used by AI to select best weapon)");
    aiLayout->addRow("AI Priority:", _weaponAIPriorityLabel);
    
    mainLayout->addWidget(aiGroup);
    mainLayout->addStretch();
    
    _tabWidget->addTab(_weaponTab, "Weapon");
}

void ProEditorDialog::setupAmmoTab() {
    _ammoTab = new QWidget();
    QFormLayout* layout = new QFormLayout(_ammoTab);
    
    _ammoCaliberCombo = new QComboBox();
    _ammoCaliberCombo->addItems({".223", "5mm", ".44", "14mm", "Rocket", "Energy"});
    connect(_ammoCaliberCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    layout->addRow("Caliber:", _ammoCaliberCombo);
    
    _ammoQuantityEdit = new QSpinBox();
    _ammoQuantityEdit->setRange(1, 999);
    connect(_ammoQuantityEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    layout->addRow("Quantity:", _ammoQuantityEdit);
    
    _ammoDamageModEdit = new QSpinBox();
    _ammoDamageModEdit->setRange(-100, 100);
    connect(_ammoDamageModEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    layout->addRow("Damage Modifier:", _ammoDamageModEdit);
    
    _ammoDRModEdit = new QSpinBox();
    _ammoDRModEdit->setRange(-100, 100);
    connect(_ammoDRModEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    layout->addRow("DR Modifier:", _ammoDRModEdit);
    
    _ammoDamageMultEdit = new QSpinBox();
    _ammoDamageMultEdit->setRange(1, 10);
    connect(_ammoDamageMultEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    layout->addRow("Damage Multiplier:", _ammoDamageMultEdit);
    
    _ammoDamageTypeModCombo = new QComboBox();
    _ammoDamageTypeModCombo->addItems({"Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion"});
    connect(_ammoDamageTypeModCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    layout->addRow("Damage Type Mod:", _ammoDamageTypeModCombo);
    
    _tabWidget->addTab(_ammoTab, "Ammo");
}

void ProEditorDialog::setupMiscTab() {
    _miscTab = new QWidget();
    QFormLayout* layout = new QFormLayout(_miscTab);
    
    _miscPowerTypeCombo = new QComboBox();
    _miscPowerTypeCombo->addItems({"None", "SmallEnergyCell", "MicroFusionCell"});
    connect(_miscPowerTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    layout->addRow("Power Type:", _miscPowerTypeCombo);
    
    _miscChargesEdit = new QSpinBox();
    _miscChargesEdit->setRange(0, 999);
    connect(_miscChargesEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    layout->addRow("Charges:", _miscChargesEdit);
    
    _tabWidget->addTab(_miscTab, "Misc");
}

void ProEditorDialog::setupKeyTab() {
    _keyTab = new QWidget();
    QFormLayout* layout = new QFormLayout(_keyTab);
    
    _keyIdEdit = new QSpinBox();
    _keyIdEdit->setRange(0, 999999);
    connect(_keyIdEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    layout->addRow("Key ID:", _keyIdEdit);
    
    _tabWidget->addTab(_keyTab, "Key");
}

void ProEditorDialog::setupPreview() {
    _previewPanel = new QWidget();
    _previewLayout = new QVBoxLayout(_previewPanel);
    
    // Setup dual preview system for items, single preview for others
    if (_pro && _pro->type() == Pro::OBJECT_TYPE::ITEM) {
        setupDualPreview();
    } else {
        // Original single preview for non-items
        _previewGroup = new QGroupBox("FRM Preview");
        QVBoxLayout* previewGroupLayout = new QVBoxLayout(_previewGroup);
        
        _previewLabel = new QLabel();
        _previewLabel->setAlignment(Qt::AlignCenter);
        _previewLabel->setMinimumHeight(200);
        _previewLabel->setMaximumHeight(250);
        _previewLabel->setMinimumWidth(250);
        _previewLabel->setScaledContents(false);
        _previewLabel->setStyleSheet("QLabel { border: 1px solid gray; background-color: #f0f0f0; }");
        _previewLabel->setText("No FRM loaded");
        previewGroupLayout->addWidget(_previewLabel);
        
        // Add animation controls to single preview
        setupAnimationControls();
        previewGroupLayout->addWidget(_animationControls);
        
        _previewLayout->addWidget(_previewGroup);
    }
    
    // Animation controls are set up in setupAnimationControls() or setupDualPreview()  
    _previewLayout->addStretch(); // Push preview to top
}

void ProEditorDialog::setupDualPreview() {
    // Create dual preview widget for side-by-side display
    _dualPreviewWidget = new QWidget();
    _dualPreviewLayout = new QHBoxLayout(_dualPreviewWidget);
    
    // Inventory preview group (left side)
    _inventoryPreviewGroup = new QGroupBox("Inventory View");
    QVBoxLayout* inventoryLayout = new QVBoxLayout(_inventoryPreviewGroup);
    
    _inventoryPreviewLabel = new QLabel();
    _inventoryPreviewLabel->setAlignment(Qt::AlignCenter);
    _inventoryPreviewLabel->setMinimumHeight(150);
    _inventoryPreviewLabel->setMaximumHeight(200);
    _inventoryPreviewLabel->setMinimumWidth(150);
    _inventoryPreviewLabel->setScaledContents(false);
    _inventoryPreviewLabel->setStyleSheet("QLabel { border: 1px solid gray; background-color: #f0f0f0; }");
    _inventoryPreviewLabel->setText("No inventory FRM");
    inventoryLayout->addWidget(_inventoryPreviewLabel);
    
    // Ground preview group (right side)
    _groundPreviewGroup = new QGroupBox("Ground View");
    QVBoxLayout* groundLayout = new QVBoxLayout(_groundPreviewGroup);
    
    _groundPreviewLabel = new QLabel();
    _groundPreviewLabel->setAlignment(Qt::AlignCenter);
    _groundPreviewLabel->setMinimumHeight(150);
    _groundPreviewLabel->setMaximumHeight(200);
    _groundPreviewLabel->setMinimumWidth(150);
    _groundPreviewLabel->setScaledContents(false);
    _groundPreviewLabel->setStyleSheet("QLabel { border: 1px solid gray; background-color: #f0f0f0; }");
    _groundPreviewLabel->setText("No ground FRM");
    groundLayout->addWidget(_groundPreviewLabel);
    
    // Add both preview groups to the layout
    _dualPreviewLayout->addWidget(_inventoryPreviewGroup);
    _dualPreviewLayout->addWidget(_groundPreviewGroup);
    
    _previewLayout->addWidget(_dualPreviewWidget);
    
    // No copy buttons or animation controls - items use static dual preview
}

void ProEditorDialog::setupAnimationControls() {
    // Animation controls
    _animationControls = new QWidget();
    _animationLayout = new QHBoxLayout(_animationControls);
    _animationLayout->setContentsMargins(0, 0, 0, 0);
    
    // Direction selection
    _directionCombo = new QComboBox();
    _directionCombo->addItems({"NE", "E", "SE", "SW", "W", "NW"});
    _directionCombo->setToolTip("Select animation direction");
    _animationLayout->addWidget(new QLabel("Direction:"));
    _animationLayout->addWidget(_directionCombo);
    
    _animationLayout->addSpacing(10);
    
    // Play/pause button
    _playPauseButton = new QPushButton("▶");
    _playPauseButton->setMaximumWidth(30);
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
    _frameLabel->setMinimumWidth(40);
    _animationLayout->addWidget(_frameLabel);
    
    // Setup animation timer
    _animationTimer = new QTimer(this);
    _animationTimer->setSingleShot(false);
    _animationTimer->setInterval(200); // 5 FPS default
    
    // Connect signals
    connect(_playPauseButton, &QPushButton::clicked, this, &ProEditorDialog::onPlayPauseClicked);
    connect(_frameSlider, &QSlider::valueChanged, this, &ProEditorDialog::onFrameChanged);
    connect(_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onDirectionChanged);
    connect(_animationTimer, &QTimer::timeout, this, &ProEditorDialog::onAnimationTick);
    
    // Initially disable controls
    _animationControls->setEnabled(false);
}

void ProEditorDialog::setupValidationPanel() {
    // Create validation panel (initially hidden)
    _validationPanel = new QWidget();
    _validationLayout = new QVBoxLayout(_validationPanel);
    _validationLayout->setContentsMargins(5, 0, 5, 5);
    
    // Create header with toggle button and status
    QWidget* validationHeader = new QWidget();
    QHBoxLayout* headerLayout = new QHBoxLayout(validationHeader);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    
    _validationToggleButton = new QPushButton("▶ Validation Issues");
    _validationToggleButton->setFlat(true);
    _validationToggleButton->setStyleSheet("text-align: left; font-weight: bold;");
    connect(_validationToggleButton, &QPushButton::clicked, this, &ProEditorDialog::onValidationToggleClicked);
    
    _validationStatusLabel = new QLabel("No issues");
    _validationStatusLabel->setStyleSheet("color: green; font-weight: bold;");
    
    headerLayout->addWidget(_validationToggleButton);
    headerLayout->addStretch();
    headerLayout->addWidget(_validationStatusLabel);
    
    // Create collapsible content
    _validationGroup = new QGroupBox();
    _validationGroup->setVisible(false); // Initially collapsed
    QVBoxLayout* groupLayout = new QVBoxLayout(_validationGroup);
    
    _validationList = new QListWidget();
    _validationList->setMaximumHeight(150);
    _validationList->setAlternatingRowColors(true);
    connect(_validationList, &QListWidget::itemDoubleClicked, this, &ProEditorDialog::onValidationItemDoubleClicked);
    
    // Add help text
    QLabel* helpLabel = new QLabel("Double-click an issue to jump to the field. Issues are categorized by severity:");
    QLabel* legendLabel = new QLabel("🔴 <span style='color: red;'>Error</span> - Must be fixed  "
                                    "🟠 <span style='color: orange;'>Warning</span> - Should be reviewed  "
                                    "🔵 <span style='color: blue;'>Info</span> - Suggestion");
    helpLabel->setWordWrap(true);
    legendLabel->setWordWrap(true);
    
    groupLayout->addWidget(helpLabel);
    groupLayout->addWidget(legendLabel);
    groupLayout->addWidget(_validationList);
    
    _validationLayout->addWidget(validationHeader);
    _validationLayout->addWidget(_validationGroup);
    
    _mainLayout->addWidget(_validationPanel);
}

void ProEditorDialog::loadProData() {
    
    try {
        // Load common data from PRO header
        _commonData.PID = _pro->header.PID;
        _commonData.message_id = _pro->header.message_id;
        _commonData.FID = _pro->header.FID;
        _commonData.light_distance = _pro->header.light_distance;
        _commonData.light_intensity = _pro->header.light_intensity;
        _commonData.flags = _pro->header.flags;
        
        
        // Load extended data
        _commonData.flagsExt = _pro->commonItemData.flagsExt;
        _commonData.SID = _pro->commonItemData.SID;
        _commonData.materialId = _pro->commonItemData.materialId;
        _commonData.containerSize = _pro->commonItemData.containerSize;
        
    } catch (const std::exception& e) {
        spdlog::error("ProEditorDialog::loadProData() - exception loading header/extended data: {}", e.what());
        throw;
    }
    
    try {
        // Update common UI controls
        
        if (_messageIdEdit) {
            _messageIdEdit->setValue(_commonData.message_id);
        }
        
        if (_fidEdit) {
            _fidEdit->setValue(_commonData.FID);
        }
        
        if (_lightDistanceEdit) {
            _lightDistanceEdit->setValue(_commonData.light_distance);
        }
        
        if (_lightIntensityEdit) {
            _lightIntensityEdit->setValue(_commonData.light_intensity);
        }
        
        if (_flagsEdit) {
            _flagsEdit->setValue(_commonData.flags);
        }
    } catch (const std::exception& e) {
        spdlog::error("ProEditorDialog::loadProData() - exception updating UI controls: {}", e.what());
        throw;
    }
    
    // Load type-specific data based on object type
    
    Pro::OBJECT_TYPE objectType = _pro->type();
    
    switch (objectType) {
        case Pro::OBJECT_TYPE::ITEM: {
            
            Pro::ITEM_TYPE itemType = _pro->itemType();
            
            switch (itemType) {
                case Pro::ITEM_TYPE::ARMOR:
                    try {
                        loadArmorData();
                    } catch (const std::exception& e) {
                        spdlog::error("ProEditorDialog::loadProData() - exception in loadArmorData(): {}", e.what());
                        throw;
                    }
                    break;
                case Pro::ITEM_TYPE::CONTAINER:
                    loadContainerData();
                    break;
                case Pro::ITEM_TYPE::DRUG:
                    loadDrugData();
                    break;
                case Pro::ITEM_TYPE::WEAPON:
                    loadWeaponData();
                    break;
                case Pro::ITEM_TYPE::AMMO:
                    loadAmmoData();
                    break;
                case Pro::ITEM_TYPE::MISC:
                    loadMiscData();
                    break;
                case Pro::ITEM_TYPE::KEY:
                    loadKeyData();
                    break;
            }
            break;
        }
        case Pro::OBJECT_TYPE::CRITTER:
            loadCritterData();
            break;
        case Pro::OBJECT_TYPE::SCENERY:
            loadSceneryData();
            break;
        case Pro::OBJECT_TYPE::WALL:
        case Pro::OBJECT_TYPE::TILE:
        case Pro::OBJECT_TYPE::MISC:
            // These types only have common data
            break;
    }
    
}

void ProEditorDialog::loadArmorData() {
    _armorClassEdit->setValue(_pro->armorData.armorClass);
    
    for (int i = 0; i < 7; ++i) {
        _damageResistEdits[i]->setValue(_pro->armorData.damageResist[i]);
        _damageThresholdEdits[i]->setValue(_pro->armorData.damageThreshold[i]);
    }
    
    _armorPerkCombo->setCurrentIndex(_pro->armorData.perk);
    _armorMaleFIDEdit->setValue(_pro->armorData.armorMaleFID);
    _armorFemaleFIDEdit->setValue(_pro->armorData.armorFemaleFID);
}

void ProEditorDialog::loadContainerData() {
    _containerMaxSizeEdit->setValue(_pro->containerData.maxSize);
    
    // Load container flags (bit flags)
    uint32_t flags = _pro->containerData.flags;
    for (int i = 0; i < 5; ++i) {
        _containerFlagChecks[i]->setChecked(flags & (1 << i));
    }
}

void ProEditorDialog::loadDrugData() {
    _drugStatCombos[0]->setCurrentIndex(_pro->drugData.stat0Base);
    _drugStatCombos[1]->setCurrentIndex(_pro->drugData.stat1Base);
    _drugStatCombos[2]->setCurrentIndex(_pro->drugData.stat2Base);
    
    _drugStatAmountEdits[0]->setValue(_pro->drugData.stat0Amount);
    _drugStatAmountEdits[1]->setValue(_pro->drugData.stat1Amount);
    _drugStatAmountEdits[2]->setValue(_pro->drugData.stat2Amount);
    
    _drugFirstDelayEdit->setValue(_pro->drugData.firstDelayMinutes);
    _drugFirstStatAmountEdits[0]->setValue(_pro->drugData.firstStat0Amount);
    _drugFirstStatAmountEdits[1]->setValue(_pro->drugData.firstStat1Amount);
    _drugFirstStatAmountEdits[2]->setValue(_pro->drugData.firstStat2Amount);
    
    _drugSecondDelayEdit->setValue(_pro->drugData.secondDelayMinutes);
    _drugSecondStatAmountEdits[0]->setValue(_pro->drugData.secondStat0Amount);
    _drugSecondStatAmountEdits[1]->setValue(_pro->drugData.secondStat1Amount);
    _drugSecondStatAmountEdits[2]->setValue(_pro->drugData.secondStat2Amount);
    
    _drugAddictionChanceEdit->setValue(_pro->drugData.addictionChance);
    _drugAddictionPerkCombo->setCurrentIndex(_pro->drugData.addictionPerk);
    _drugAddictionDelayEdit->setValue(_pro->drugData.addictionDelay);
}

void ProEditorDialog::loadWeaponData() {
    _weaponAnimationCombo->setCurrentIndex(_pro->weaponData.animationCode);
    _weaponDamageMinEdit->setValue(_pro->weaponData.damageMin);
    _weaponDamageMaxEdit->setValue(_pro->weaponData.damageMax);
    _weaponDamageTypeCombo->setCurrentIndex(_pro->weaponData.damageType);
    _weaponRangePrimaryEdit->setValue(_pro->weaponData.rangePrimary);
    _weaponRangeSecondaryEdit->setValue(_pro->weaponData.rangeSecondary);
    _weaponProjectilePIDEdit->setValue(_pro->weaponData.projectilePID);
    _weaponMinStrengthEdit->setValue(_pro->weaponData.minimumStrength);
    _weaponAPPrimaryEdit->setValue(_pro->weaponData.actionCostPrimary);
    _weaponAPSecondaryEdit->setValue(_pro->weaponData.actionCostSecondary);
    _weaponCriticalFailEdit->setValue(_pro->weaponData.criticalFail);
    _weaponPerkCombo->setCurrentIndex(_pro->weaponData.perk);
    _weaponBurstRoundsEdit->setValue(_pro->weaponData.burstRounds);
    _weaponAmmoTypeCombo->setCurrentIndex(_pro->weaponData.ammoType);
    _weaponAmmoPIDEdit->setValue(_pro->weaponData.ammoPID);
    _weaponAmmoCapacityEdit->setValue(_pro->weaponData.ammoCapacity);
    _weaponSoundIdEdit->setValue(_pro->weaponData.soundId);
    
    // Load weapon flags
    bool isEnergyWeapon = (_pro->weaponData.weaponFlags & static_cast<uint32_t>(Pro::WEAPON_FLAGS::ENERGY_WEAPON)) != 0;
    _weaponEnergyWeaponCheck->setChecked(isEnergyWeapon);
}

void ProEditorDialog::loadAmmoData() {
    _ammoCaliberCombo->setCurrentIndex(_pro->ammoData.caliber);
    _ammoQuantityEdit->setValue(_pro->ammoData.quantity);
    _ammoDamageModEdit->setValue(_pro->ammoData.damageModifier);
    _ammoDRModEdit->setValue(_pro->ammoData.damageResistModifier);
    _ammoDamageMultEdit->setValue(_pro->ammoData.damageMultiplier);
    _ammoDamageTypeModCombo->setCurrentIndex(_pro->ammoData.damageTypeModifier);
}

void ProEditorDialog::loadMiscData() {
    _miscPowerTypeCombo->setCurrentIndex(_pro->miscData.powerType);
    _miscChargesEdit->setValue(_pro->miscData.charges);
}

void ProEditorDialog::loadKeyData() {
    _keyIdEdit->setValue(_pro->keyData.keyId);
}

void ProEditorDialog::saveProData() {
    // Save common data back to PRO header
    _pro->header.message_id = _messageIdEdit->value();
    _pro->header.FID = _fidEdit->value();
    _pro->header.light_distance = _lightDistanceEdit->value();
    _pro->header.light_intensity = _lightIntensityEdit->value();
    _pro->header.flags = _flagsEdit->value();
    
    // Save extended fields if they exist (flagsExt is updated real-time via signal handlers)
    if (_flagsExtRawEdit) {
        _pro->commonItemData.flagsExt = _flagsExtRawEdit->value();
    }
    if (_sidEdit) {
        _pro->commonItemData.SID = _sidEdit->value();
    }
    if (_materialIdEdit) {
        _pro->commonItemData.materialId = _materialIdEdit->value();
    }
    if (_containerSizeEdit) {
        _pro->commonItemData.containerSize = _containerSizeEdit->value();
    }
    
    // Save type-specific data based on object type
    switch (_pro->type()) {
        case Pro::OBJECT_TYPE::ITEM: {
            Pro::ITEM_TYPE itemType = _pro->itemType();
            
            switch (itemType) {
                case Pro::ITEM_TYPE::ARMOR:
                    saveArmorData();
                    break;
                case Pro::ITEM_TYPE::CONTAINER:
                    saveContainerData();
                    break;
                case Pro::ITEM_TYPE::DRUG:
                    saveDrugData();
                    break;
                case Pro::ITEM_TYPE::WEAPON:
                    saveWeaponData();
                    break;
                case Pro::ITEM_TYPE::AMMO:
                    saveAmmoData();
                    break;
                case Pro::ITEM_TYPE::MISC:
                    saveMiscData();
                    break;
                case Pro::ITEM_TYPE::KEY:
                    saveKeyData();
                    break;
            }
            break;
        }
        case Pro::OBJECT_TYPE::CRITTER:
            saveCritterData();
            break;
        case Pro::OBJECT_TYPE::SCENERY:
            saveSceneryData();
            break;
        case Pro::OBJECT_TYPE::WALL:
        case Pro::OBJECT_TYPE::TILE:
        case Pro::OBJECT_TYPE::MISC:
            // These types only have common data
            break;
    }
    
}

void ProEditorDialog::saveArmorData() {
    _pro->armorData.armorClass = _armorClassEdit->value();
    
    for (int i = 0; i < 7; ++i) {
        _pro->armorData.damageResist[i] = _damageResistEdits[i]->value();
        _pro->armorData.damageThreshold[i] = _damageThresholdEdits[i]->value();
    }
    
    _pro->armorData.perk = _armorPerkCombo->currentIndex();
    _pro->armorData.armorMaleFID = _armorMaleFIDEdit->value();
    _pro->armorData.armorFemaleFID = _armorFemaleFIDEdit->value();
}

void ProEditorDialog::saveContainerData() {
    _pro->containerData.maxSize = _containerMaxSizeEdit->value();
    
    // Save container flags (bit flags)
    uint32_t flags = 0;
    for (int i = 0; i < 5; ++i) {
        if (_containerFlagChecks[i]->isChecked()) {
            flags |= (1 << i);
        }
    }
    _pro->containerData.flags = flags;
}

void ProEditorDialog::saveDrugData() {
    _pro->drugData.stat0Base = _drugStatCombos[0]->currentIndex();
    _pro->drugData.stat1Base = _drugStatCombos[1]->currentIndex();
    _pro->drugData.stat2Base = _drugStatCombos[2]->currentIndex();
    
    _pro->drugData.stat0Amount = _drugStatAmountEdits[0]->value();
    _pro->drugData.stat1Amount = _drugStatAmountEdits[1]->value();
    _pro->drugData.stat2Amount = _drugStatAmountEdits[2]->value();
    
    _pro->drugData.firstDelayMinutes = _drugFirstDelayEdit->value();
    _pro->drugData.firstStat0Amount = _drugFirstStatAmountEdits[0]->value();
    _pro->drugData.firstStat1Amount = _drugFirstStatAmountEdits[1]->value();
    _pro->drugData.firstStat2Amount = _drugFirstStatAmountEdits[2]->value();
    
    _pro->drugData.secondDelayMinutes = _drugSecondDelayEdit->value();
    _pro->drugData.secondStat0Amount = _drugSecondStatAmountEdits[0]->value();
    _pro->drugData.secondStat1Amount = _drugSecondStatAmountEdits[1]->value();
    _pro->drugData.secondStat2Amount = _drugSecondStatAmountEdits[2]->value();
    
    _pro->drugData.addictionChance = _drugAddictionChanceEdit->value();
    _pro->drugData.addictionPerk = _drugAddictionPerkCombo->currentIndex();
    _pro->drugData.addictionDelay = _drugAddictionDelayEdit->value();
}

void ProEditorDialog::saveWeaponData() {
    _pro->weaponData.animationCode = _weaponAnimationCombo->currentIndex();
    _pro->weaponData.damageMin = _weaponDamageMinEdit->value();
    _pro->weaponData.damageMax = _weaponDamageMaxEdit->value();
    _pro->weaponData.damageType = _weaponDamageTypeCombo->currentIndex();
    _pro->weaponData.rangePrimary = _weaponRangePrimaryEdit->value();
    _pro->weaponData.rangeSecondary = _weaponRangeSecondaryEdit->value();
    _pro->weaponData.projectilePID = _weaponProjectilePIDEdit->value();
    _pro->weaponData.minimumStrength = _weaponMinStrengthEdit->value();
    _pro->weaponData.actionCostPrimary = _weaponAPPrimaryEdit->value();
    _pro->weaponData.actionCostSecondary = _weaponAPSecondaryEdit->value();
    _pro->weaponData.criticalFail = _weaponCriticalFailEdit->value();
    _pro->weaponData.perk = _weaponPerkCombo->currentIndex();
    _pro->weaponData.burstRounds = _weaponBurstRoundsEdit->value();
    _pro->weaponData.ammoType = _weaponAmmoTypeCombo->currentIndex();
    _pro->weaponData.ammoPID = _weaponAmmoPIDEdit->value();
    _pro->weaponData.ammoCapacity = _weaponAmmoCapacityEdit->value();
    _pro->weaponData.soundId = _weaponSoundIdEdit->value();
    
    // Save weapon flags
    if (_weaponEnergyWeaponCheck->isChecked()) {
        _pro->weaponData.weaponFlags |= static_cast<uint32_t>(Pro::WEAPON_FLAGS::ENERGY_WEAPON);
    } else {
        _pro->weaponData.weaponFlags &= ~static_cast<uint32_t>(Pro::WEAPON_FLAGS::ENERGY_WEAPON);
    }
}

void ProEditorDialog::saveAmmoData() {
    _pro->ammoData.caliber = _ammoCaliberCombo->currentIndex();
    _pro->ammoData.quantity = _ammoQuantityEdit->value();
    _pro->ammoData.damageModifier = _ammoDamageModEdit->value();
    _pro->ammoData.damageResistModifier = _ammoDRModEdit->value();
    _pro->ammoData.damageMultiplier = _ammoDamageMultEdit->value();
    _pro->ammoData.damageTypeModifier = _ammoDamageTypeModCombo->currentIndex();
}

void ProEditorDialog::saveMiscData() {
    _pro->miscData.powerType = _miscPowerTypeCombo->currentIndex();
    _pro->miscData.charges = _miscChargesEdit->value();
}

void ProEditorDialog::saveKeyData() {
    _pro->keyData.keyId = _keyIdEdit->value();
}

void ProEditorDialog::updateTabVisibility() {
    if (!_pro) return;
    
    // Show/hide tabs based on PRO type
    Pro::OBJECT_TYPE type = _pro->type();
    
    // Hide all type-specific tabs first
    for (int i = 1; i < _tabWidget->count(); ++i) {
        _tabWidget->setTabVisible(i, false);
    }
    
    switch (type) {
        case Pro::OBJECT_TYPE::ITEM: {
            Pro::ITEM_TYPE itemType = static_cast<Pro::ITEM_TYPE>(_pro->objectSubtypeId());
            
            switch (itemType) {
                case Pro::ITEM_TYPE::ARMOR:
                    _tabWidget->setTabVisible(_tabWidget->indexOf(_armorTab), true);
                    break;
                case Pro::ITEM_TYPE::CONTAINER:
                    _tabWidget->setTabVisible(_tabWidget->indexOf(_containerTab), true);
                    break;
                case Pro::ITEM_TYPE::DRUG:
                    _tabWidget->setTabVisible(_tabWidget->indexOf(_drugTab), true);
                    break;
                case Pro::ITEM_TYPE::WEAPON:
                    _tabWidget->setTabVisible(_tabWidget->indexOf(_weaponTab), true);
                    break;
                case Pro::ITEM_TYPE::AMMO:
                    _tabWidget->setTabVisible(_tabWidget->indexOf(_ammoTab), true);
                    break;
                case Pro::ITEM_TYPE::MISC:
                    _tabWidget->setTabVisible(_tabWidget->indexOf(_miscTab), true);
                    break;
                case Pro::ITEM_TYPE::KEY:
                    _tabWidget->setTabVisible(_tabWidget->indexOf(_keyTab), true);
                    break;
            }
            break;
        }
        case Pro::OBJECT_TYPE::CRITTER:
            _tabWidget->setTabVisible(_tabWidget->indexOf(_critterTab), true);
            break;
        case Pro::OBJECT_TYPE::SCENERY:
            _tabWidget->setTabVisible(_tabWidget->indexOf(_sceneryTab), true);
            break;
        case Pro::OBJECT_TYPE::WALL:
        case Pro::OBJECT_TYPE::TILE:
        case Pro::OBJECT_TYPE::MISC:
            // These types only have common tab, no additional tabs
            break;
    }
}

void ProEditorDialog::validateField(QWidget* field) {
    if (!field) return;
    
    // Clear previous validation issues for this field
    clearValidationIssues(field);
    
    // Clear any error styling first
    field->setStyleSheet("");
    field->setToolTip("");
    
    // Get field value as spinbox (most common case)
    QSpinBox* spinBox = qobject_cast<QSpinBox*>(field);
    QComboBox* comboBox = qobject_cast<QComboBox*>(field);
    
    // === WEAPON VALIDATIONS ===
    if (_pro && _pro->type() == Pro::OBJECT_TYPE::ITEM && _pro->itemType() == Pro::ITEM_TYPE::WEAPON) {
        // Weapon damage validation
        if ((field == _weaponDamageMinEdit || field == _weaponDamageMaxEdit) && 
            _weaponDamageMinEdit && _weaponDamageMaxEdit) {
            int minDamage = _weaponDamageMinEdit->value();
            int maxDamage = _weaponDamageMaxEdit->value();
            
            if (minDamage > maxDamage) {
                addValidationIssue(field, "Minimum damage cannot be greater than maximum damage", ValidationLevel::ERROR, "Weapon Stats");
            } else if (minDamage == 0 && maxDamage == 0) {
                addValidationIssue(field, "Weapon should have some damage", ValidationLevel::WARNING, "Weapon Stats");
            }
        }
        
        // Action Points validation
        if (field == _weaponAPPrimaryEdit && spinBox) {
            int ap = spinBox->value();
            if (ap <= 0) {
                addValidationIssue(field, "Primary attack should cost at least 1 AP", ValidationLevel::ERROR, "Weapon Stats");
            } else if (ap > 12) {
                addValidationIssue(field, "Very high AP cost may make weapon unusable", ValidationLevel::WARNING, "Weapon Stats");
            }
        }
        
        if (field == _weaponAPSecondaryEdit && spinBox) {
            int ap = spinBox->value();
            if (ap > 0 && ap > 15) {
                addValidationIssue(field, "Extremely high AP cost for secondary attack", ValidationLevel::WARNING, "Weapon Stats");
            }
        }
        
        // Range validation
        if (field == _weaponRangePrimaryEdit && spinBox) {
            int range = spinBox->value();
            if (range <= 0) {
                addValidationIssue(field, "Weapon range should be greater than 0", ValidationLevel::ERROR, "Weapon Stats");
            } else if (range > 50) {
                addValidationIssue(field, "Very long range weapon - check if intended", ValidationLevel::INFO, "Weapon Stats");
            }
        }
        
        // Strength requirement validation
        if (field == _weaponMinStrengthEdit && spinBox) {
            int str = spinBox->value();
            if (str > 10) {
                addValidationIssue(field, "Strength requirement exceeds maximum character stat (10)", ValidationLevel::WARNING, "Weapon Stats");
            }
        }
        
        // Projectile PID validation
        if (field == _weaponProjectilePIDEdit && spinBox) {
            int32_t pid = spinBox->value();
            if (pid != 0 && pid != -1 && !validateFIDReference(pid, "Projectile")) {
                addValidationIssue(field, "Projectile PID may not reference a valid game object", ValidationLevel::WARNING, "References");
            }
        }
    }
    
    // === CRITTER VALIDATIONS ===
    if (_pro && _pro->type() == Pro::OBJECT_TYPE::CRITTER) {
        // SPECIAL stats validation (1-10 range)
        for (int i = 0; i < 7; ++i) {
            if (field == _critterSpecialStatEdits[i] && spinBox) {
                int stat = spinBox->value();
                if (!validateStatValue(stat, 1, 10, "SPECIAL stat")) {
                    if (stat < 1) {
                        addValidationIssue(field, "SPECIAL stats cannot be less than 1", ValidationLevel::ERROR, "Critter Stats");
                    } else if (stat > 10) {
                        addValidationIssue(field, "SPECIAL stats cannot exceed 10", ValidationLevel::ERROR, "Critter Stats");
                    }
                }
            }
        }
        
        // Hit points validation
        if (field == _critterMaxHitPointsEdit && spinBox) {
            int hp = spinBox->value();
            if (hp <= 0) {
                addValidationIssue(field, "Hit points must be greater than 0", ValidationLevel::ERROR, "Critter Stats");
            } else if (hp > 999) {
                addValidationIssue(field, "Very high hit points - boss-level critter?", ValidationLevel::INFO, "Critter Stats");
            }
        }
        
        // Action points validation
        if (field == _critterActionPointsEdit && spinBox) {
            int ap = spinBox->value();
            if (ap <= 0) {
                addValidationIssue(field, "Action points must be greater than 0", ValidationLevel::ERROR, "Critter Stats");
            } else if (ap > 20) {
                addValidationIssue(field, "Very high action points - may unbalance combat", ValidationLevel::WARNING, "Critter Stats");
            }
        }
        
        // Armor class validation
        if (field == _critterArmorClassEdit && spinBox) {
            int ac = spinBox->value();
            if (ac < 0) {
                addValidationIssue(field, "Armor class cannot be negative", ValidationLevel::ERROR, "Critter Stats");
            } else if (ac > 50) {
                addValidationIssue(field, "Very high armor class - may be unhittable", ValidationLevel::WARNING, "Critter Stats");
            }
        }
    }
    
    // === GENERAL ITEM VALIDATIONS ===
    if (_pro && _pro->type() == Pro::OBJECT_TYPE::ITEM) {
        // Weight validation
        if (field == _weightEdit && spinBox) {
            int weight = spinBox->value();
            if (weight < 0) {
                addValidationIssue(field, "Weight cannot be negative", ValidationLevel::ERROR, "Item Properties");
            } else if (weight > 1000) {
                addValidationIssue(field, "Very heavy item - check if realistic", ValidationLevel::WARNING, "Item Properties");
            }
        }
        
        // Price validation
        if (field == _basePriceEdit && spinBox) {
            int price = spinBox->value();
            if (price < 0) {
                addValidationIssue(field, "Base price cannot be negative", ValidationLevel::ERROR, "Item Properties");
            } else if (price == 0) {
                addValidationIssue(field, "Item has no value - is this intended?", ValidationLevel::INFO, "Item Properties");
            } else if (price > 100000) {
                addValidationIssue(field, "Very expensive item - luxury goods?", ValidationLevel::INFO, "Item Properties");
            }
        }
    }
    
    // === FID REFERENCE VALIDATIONS ===
    if (field == _fidEdit && spinBox) {
        int32_t fid = spinBox->value();
        if (fid != 0 && fid != -1 && !validateFIDReference(fid, "Main FRM")) {
            addValidationIssue(field, "FID may not reference a valid FRM file", ValidationLevel::WARNING, "References");
        }
    }
    
    if (field == _inventoryFIDEdit && spinBox) {
        int32_t fid = spinBox->value();
        if (fid != 0 && fid != -1 && !validateFIDReference(fid, "Inventory FRM")) {
            addValidationIssue(field, "Inventory FID may not reference a valid FRM file", ValidationLevel::WARNING, "References");
        }
    }
    
    // Apply visual styling based on validation results
    for (const auto& issue : _validationIssues) {
        if (issue.field == field) {
            switch (issue.level) {
                case ValidationLevel::ERROR:
                    field->setStyleSheet("border: 2px solid red;");
                    break;
                case ValidationLevel::WARNING:
                    field->setStyleSheet("border: 2px solid orange;");
                    break;
                case ValidationLevel::INFO:
                    field->setStyleSheet("border: 1px solid blue;");
                    break;
            }
            field->setToolTip(issue.message);
            break; // Use the first (highest priority) issue for styling
        }
    }
    
    // Update validation status indicators
    updateValidationStatus();
}

void ProEditorDialog::updatePreview() {
    
    // Stop current animation
    if (_animationTimer && _animationTimer->isActive()) {
        _animationTimer->stop();
        _isAnimating = false;
        if (_playPauseButton) {
            _playPauseButton->setText("▶");
        }
    }
    
    // Check if we're using dual preview system (for items)
    if (_pro && _pro->type() == Pro::OBJECT_TYPE::ITEM && _inventoryPreviewLabel && _groundPreviewLabel) {
        // Update both dual previews (items use static thumbnails)
        updateInventoryPreview();
        updateGroundPreview();
        
        // Items don't animate, so disable animation controls and return
        if (_animationControls) {
            _animationControls->setEnabled(false);
        }
        return;
    }
    
    // Original single preview for non-items
    if (!_previewLabel) {
        return;
    }
    
    // Determine which FID to use for preview
    int32_t previewFid = 0;
    try {
        previewFid = getPreviewFid();
    } catch (const std::exception& e) {
        spdlog::error("ProEditorDialog::updatePreview() - exception getting preview FID: {}", e.what());
        _previewLabel->clear();
        _previewLabel->setText("Failed to get FID");
        if (_animationControls) {
            _animationControls->setEnabled(false);
        }
        return;
    }
    
    if (previewFid <= 0) {
        _previewLabel->clear();
        _previewLabel->setText("No FRM loaded");
        if (_animationControls) {
            _animationControls->setEnabled(false);
        }
        return;
    }
    
    // Load animation frames for the new FRM
    loadAnimationFrames();
}

int32_t ProEditorDialog::getPreviewFid() {
    
    // Check if basic widgets are initialized
    if (!_fidEdit) {
        return 0;
    }
    
    // Check if PRO is valid
    if (!_pro) {
        return 0;
    }
    
    // Items use dual preview system, not single preview, so they don't need getPreviewFid()
    
    // For items without dual preview, prefer inventory FID over world FID for preview
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM && _inventoryFIDEdit) {
        int32_t inventoryFid = _inventoryFIDEdit->value();
        if (inventoryFid > 0) {
            return inventoryFid;
        }
        
        // For armor, check for male/female specific FIDs
        if (_pro->itemType() == Pro::ITEM_TYPE::ARMOR && _armorMaleFIDEdit && _armorFemaleFIDEdit) {
            int32_t maleFid = _armorMaleFIDEdit->value();
            if (maleFid > 0) {
                return maleFid; // Default to male version
            }
            
            int32_t femaleFid = _armorFemaleFIDEdit->value();
            if (femaleFid > 0) {
                return femaleFid;
            }
        }
    }
    
    // Fall back to main FID
    int32_t basicFid = _fidEdit->value();
    return basicFid;
}

int32_t ProEditorDialog::getInventoryFid() {
    
    // Check if basic widgets are initialized
    if (!_pro || !_inventoryFIDEdit) {
        return 0;
    }
    
    // For items, return inventory FID
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        int32_t inventoryFid = _inventoryFIDEdit->value();
        return inventoryFid;
    }
    
    return 0;
}

int32_t ProEditorDialog::getGroundFid() {
    
    // Check if basic widgets are initialized
    if (!_pro || !_fidEdit) {
        return 0;
    }
    
    // For items, return main FID (ground view)
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        int32_t groundFid = _fidEdit->value();
        return groundFid;
    }
    
    return 0;
}

void ProEditorDialog::updateInventoryPreview() {
    
    if (!_inventoryPreviewLabel) {
        return;
    }
    
    int32_t inventoryFid = getInventoryFid();
    if (inventoryFid <= 0) {
        _inventoryPreviewLabel->clear();
        _inventoryPreviewLabel->setText("No inventory FRM");
        return;
    }
    
    // Generate thumbnail for inventory view
    try {
        auto& resourceManager = ResourceManager::getInstance();
        std::string frmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(inventoryFid));
        
        if (frmPath.empty()) {
            _inventoryPreviewLabel->clear();
            _inventoryPreviewLabel->setText("Invalid inventory FID");
            return;
        }
        
        QPixmap thumbnail = createFrmThumbnail(frmPath, QSize(150, 150));
        if (!thumbnail.isNull()) {
            _inventoryPreviewLabel->setPixmap(thumbnail);
        } else {
            _inventoryPreviewLabel->clear();
            _inventoryPreviewLabel->setText("Failed to load inventory FRM");
        }
        
    } catch (const std::exception& e) {
        spdlog::error("ProEditorDialog::updateInventoryPreview() - exception: {}", e.what());
        _inventoryPreviewLabel->clear();
        _inventoryPreviewLabel->setText("Error loading inventory FRM");
    }
}

void ProEditorDialog::updateGroundPreview() {
    
    if (!_groundPreviewLabel) {
        return;
    }
    
    int32_t groundFid = getGroundFid();
    if (groundFid <= 0) {
        _groundPreviewLabel->clear();
        _groundPreviewLabel->setText("No ground FRM");
        return;
    }
    
    // Generate thumbnail for ground view
    try {
        auto& resourceManager = ResourceManager::getInstance();
        std::string frmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(groundFid));
        
        if (frmPath.empty()) {
            _groundPreviewLabel->clear();
            _groundPreviewLabel->setText("Invalid ground FID");
            return;
        }
        
        QPixmap thumbnail = createFrmThumbnail(frmPath, QSize(150, 150));
        if (!thumbnail.isNull()) {
            _groundPreviewLabel->setPixmap(thumbnail);
        } else {
            _groundPreviewLabel->clear();
            _groundPreviewLabel->setText("Failed to load ground FRM");
        }
        
    } catch (const std::exception& e) {
        spdlog::error("ProEditorDialog::updateGroundPreview() - exception: {}", e.what());
        _groundPreviewLabel->clear();
        _groundPreviewLabel->setText("Error loading ground FRM");
    }
}

void ProEditorDialog::onAccept() {
    saveProData();
    
    // Ask user where to save the file
    QString suggestedName = QString::fromStdString(_pro->path().filename().string());
    QString filePath = QFileDialog::getSaveFileName(
        this,
        "Save PRO File",
        suggestedName,
        "PRO Files (*.pro);;All Files (*)"
    );
    
    if (filePath.isEmpty()) {
        return; // User cancelled
    }
    
    // Create backup if file exists
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
    
    // Save the PRO file
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
    QWidget* sender = qobject_cast<QWidget*>(QObject::sender());
    if (sender) {
        validateField(sender);
        
        // Update preview if any FID field changed
        if (sender == _fidEdit || 
            (sender == _inventoryFIDEdit && _inventoryFIDEdit) || 
            (sender == _armorMaleFIDEdit && _armorMaleFIDEdit) || 
            (sender == _armorFemaleFIDEdit && _armorFemaleFIDEdit)) {
            updatePreview();
        }
        
        // Update AI priority displays when relevant fields change
        updateAIPriorityDisplays();
    }
}

void ProEditorDialog::onComboBoxChanged() {
    QComboBox* sender = qobject_cast<QComboBox*>(QObject::sender());
    if (sender) {
        validateField(sender);
        // Update AI priority displays when relevant fields change
        updateAIPriorityDisplays();
    }
}

void ProEditorDialog::onCheckBoxChanged() {
    QCheckBox* sender = qobject_cast<QCheckBox*>(QObject::sender());
    if (sender) {
        validateField(sender);
    }
}

void ProEditorDialog::onFidSelectorClicked() {
    openFrmSelector(_fidEdit, 0); // Items type
}

void ProEditorDialog::onInventoryFidSelectorClicked() {
    openFrmSelector(_inventoryFIDEdit, 7); // Inventory type
}

void ProEditorDialog::onArmorMaleFidSelectorClicked() {
    openFrmSelector(_armorMaleFIDEdit, 7); // Inventory type for armor
}

void ProEditorDialog::onArmorFemaleFidSelectorClicked() {
    openFrmSelector(_armorFemaleFIDEdit, 7); // Inventory type for armor
}

void ProEditorDialog::openFrmSelector(QSpinBox* targetField, uint32_t objectType) {
    if (!targetField) return;
    
    FrmSelectorDialog dialog(this);
    dialog.setObjectTypeFilter(objectType);
    dialog.setInitialFrmPid(static_cast<uint32_t>(targetField->value()));
    
    if (dialog.exec() == QDialog::Accepted) {
        uint32_t selectedFrmPid = dialog.getSelectedFrmPid();
        if (selectedFrmPid > 0) {
            targetField->setValue(static_cast<int>(selectedFrmPid));
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

void ProEditorDialog::setupCritterTab() {
    _critterTab = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(_critterTab);
    
    // Create scroll area for all the critter fields
    QScrollArea* scrollArea = new QScrollArea();
    QWidget* scrollWidget = new QWidget();
    QFormLayout* layout = new QFormLayout(scrollWidget);
    
    // Basic critter data
    _critterHeadFIDEdit = new QSpinBox();
    _critterHeadFIDEdit->setRange(0, 999999);
    _critterHeadFIDEdit->setToolTip("Head FID for critter appearance");
    layout->addRow("Head FID:", _critterHeadFIDEdit);
    
    _critterAIPacketEdit = new QSpinBox();
    _critterAIPacketEdit->setRange(0, 999);
    _critterAIPacketEdit->setToolTip("AI packet number for critter behavior");
    layout->addRow("AI Packet:", _critterAIPacketEdit);
    
    _critterTeamNumberEdit = new QSpinBox();
    _critterTeamNumberEdit->setRange(0, 999);
    _critterTeamNumberEdit->setToolTip("Team number for faction identification");
    layout->addRow("Team Number:", _critterTeamNumberEdit);
    
    _critterFlagsEdit = new QSpinBox();
    _critterFlagsEdit->setRange(0, 0xFFFFFF);
    _critterFlagsEdit->setDisplayIntegerBase(16);
    _critterFlagsEdit->setToolTip("Critter behavior flags (hex)");
    layout->addRow("Flags:", _critterFlagsEdit);
    
    // SPECIAL stats group
    QGroupBox* specialGroup = new QGroupBox("SPECIAL Stats");
    QGridLayout* specialLayout = new QGridLayout(specialGroup);
    
    const char* specialNames[] = {"Strength", "Perception", "Endurance", "Charisma", "Intelligence", "Agility", "Luck"};
    for (int i = 0; i < 7; ++i) {
        _critterSpecialStatEdits[i] = new QSpinBox();
        _critterSpecialStatEdits[i]->setRange(1, 10);
        _critterSpecialStatEdits[i]->setToolTip(QString("Base %1 stat").arg(specialNames[i]));
        specialLayout->addWidget(new QLabel(specialNames[i]), i / 4, (i % 4) * 2);
        specialLayout->addWidget(_critterSpecialStatEdits[i], i / 4, (i % 4) * 2 + 1);
    }
    layout->addWidget(specialGroup);
    
    // Combat stats
    QGroupBox* combatGroup = new QGroupBox("Combat Stats");
    QGridLayout* combatLayout = new QGridLayout(combatGroup);
    
    _critterMaxHitPointsEdit = new QSpinBox();
    _critterMaxHitPointsEdit->setRange(1, 9999);
    _critterMaxHitPointsEdit->setToolTip("Maximum hit points");
    combatLayout->addWidget(new QLabel("Max HP:"), 0, 0);
    combatLayout->addWidget(_critterMaxHitPointsEdit, 0, 1);
    
    _critterActionPointsEdit = new QSpinBox();
    _critterActionPointsEdit->setRange(1, 20);
    _critterActionPointsEdit->setToolTip("Action points per turn");
    combatLayout->addWidget(new QLabel("Action Points:"), 0, 2);
    combatLayout->addWidget(_critterActionPointsEdit, 0, 3);
    
    _critterArmorClassEdit = new QSpinBox();
    _critterArmorClassEdit->setRange(0, 90);
    _critterArmorClassEdit->setToolTip("Base armor class");
    combatLayout->addWidget(new QLabel("Armor Class:"), 1, 0);
    combatLayout->addWidget(_critterArmorClassEdit, 1, 1);
    
    _critterMeleeDamageEdit = new QSpinBox();
    _critterMeleeDamageEdit->setRange(0, 999);
    _critterMeleeDamageEdit->setToolTip("Melee damage bonus");
    combatLayout->addWidget(new QLabel("Melee Damage:"), 1, 2);
    combatLayout->addWidget(_critterMeleeDamageEdit, 1, 3);
    
    layout->addWidget(combatGroup);
    
    // Other stats
    _critterCarryWeightMaxEdit = new QSpinBox();
    _critterCarryWeightMaxEdit->setRange(0, 99999);
    _critterCarryWeightMaxEdit->setToolTip("Maximum carrying capacity");
    layout->addRow("Carry Weight:", _critterCarryWeightMaxEdit);
    
    _critterSequenceEdit = new QSpinBox();
    _critterSequenceEdit->setRange(0, 50);
    _critterSequenceEdit->setToolTip("Combat sequence modifier");
    layout->addRow("Sequence:", _critterSequenceEdit);
    
    _critterHealingRateEdit = new QSpinBox();
    _critterHealingRateEdit->setRange(0, 50);
    _critterHealingRateEdit->setToolTip("Natural healing rate");
    layout->addRow("Healing Rate:", _critterHealingRateEdit);
    
    _critterCriticalChanceEdit = new QSpinBox();
    _critterCriticalChanceEdit->setRange(0, 100);
    _critterCriticalChanceEdit->setToolTip("Critical hit chance percentage");
    layout->addRow("Critical Chance:", _critterCriticalChanceEdit);
    
    _critterBetterCriticalsEdit = new QSpinBox();
    _critterBetterCriticalsEdit->setRange(0, 100);
    _critterBetterCriticalsEdit->setToolTip("Better criticals bonus");
    layout->addRow("Better Criticals:", _critterBetterCriticalsEdit);
    
    // Damage threshold group (7 damage types: Normal, Laser, Fire, Plasma, Electrical, EMP, Explosion)
    QGroupBox* thresholdGroup = new QGroupBox("Damage Threshold");
    QGridLayout* thresholdLayout = new QGridLayout(thresholdGroup);
    const char* damageTypes[] = {"Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion"};
    
    for (int i = 0; i < 7; ++i) {
        _critterDamageThresholdEdits[i] = new QSpinBox();
        _critterDamageThresholdEdits[i]->setRange(0, 200);
        _critterDamageThresholdEdits[i]->setToolTip(QString("%1 damage threshold").arg(damageTypes[i]));
        thresholdLayout->addWidget(new QLabel(damageTypes[i]), i / 4, (i % 4) * 2);
        thresholdLayout->addWidget(_critterDamageThresholdEdits[i], i / 4, (i % 4) * 2 + 1);
    }
    layout->addWidget(thresholdGroup);
    
    // Damage resistance group (9 damage types: Normal, Laser, Fire, Plasma, Electrical, EMP, Explosion, Combat, Radiation)
    QGroupBox* resistanceGroup = new QGroupBox("Damage Resistance");
    QGridLayout* resistanceLayout = new QGridLayout(resistanceGroup);
    const char* resistanceTypes[] = {"Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion", "Combat", "Radiation"};
    
    for (int i = 0; i < 9; ++i) {
        _critterDamageResistEdits[i] = new QSpinBox();
        _critterDamageResistEdits[i]->setRange(0, 100);
        _critterDamageResistEdits[i]->setSuffix("%");
        _critterDamageResistEdits[i]->setToolTip(QString("%1 damage resistance percentage").arg(resistanceTypes[i]));
        resistanceLayout->addWidget(new QLabel(resistanceTypes[i]), i / 3, (i % 3) * 2);
        resistanceLayout->addWidget(_critterDamageResistEdits[i], i / 3, (i % 3) * 2 + 1);
    }
    layout->addWidget(resistanceGroup);
    
    // Skills group (18 skills)
    QGroupBox* skillsGroup = new QGroupBox("Skills");
    QGridLayout* skillsLayout = new QGridLayout(skillsGroup);
    const char* skillNames[] = {
        "Small Guns", "Big Guns", "Energy Weapons", "Unarmed", "Melee Weapons", "Throwing",
        "First Aid", "Doctor", "Sneak", "Lockpick", "Steal", "Traps",
        "Science", "Repair", "Speech", "Barter", "Gambling", "Outdoorsman"
    };
    
    for (int i = 0; i < 18; ++i) {
        _critterSkillEdits[i] = new QSpinBox();
        _critterSkillEdits[i]->setRange(0, 300);
        _critterSkillEdits[i]->setSuffix("%");
        _critterSkillEdits[i]->setToolTip(QString("%1 skill percentage").arg(skillNames[i]));
        skillsLayout->addWidget(new QLabel(skillNames[i]), i / 3, (i % 3) * 2);
        skillsLayout->addWidget(_critterSkillEdits[i], i / 3, (i % 3) * 2 + 1);
    }
    layout->addWidget(skillsGroup);
    
    // Bonus SPECIAL stats group
    QGroupBox* bonusSpecialGroup = new QGroupBox("Bonus SPECIAL Stats");
    QGridLayout* bonusSpecialLayout = new QGridLayout(bonusSpecialGroup);
    const char* bonusSpecialNames[] = {"STR", "PER", "END", "CHR", "INT", "AGL", "LCK"};
    
    for (int i = 0; i < 7; ++i) {
        _critterBonusSpecialStatEdits[i] = new QSpinBox();
        _critterBonusSpecialStatEdits[i]->setRange(-10, 10);
        _critterBonusSpecialStatEdits[i]->setToolTip(QString("Bonus %1 modifier").arg(bonusSpecialNames[i]));
        bonusSpecialLayout->addWidget(new QLabel(QString("Bonus %1:").arg(bonusSpecialNames[i])), i / 4, (i % 4) * 2);
        bonusSpecialLayout->addWidget(_critterBonusSpecialStatEdits[i], i / 4, (i % 4) * 2 + 1);
    }
    layout->addWidget(bonusSpecialGroup);
    
    // Bonus derived stats group
    QGroupBox* bonusDerivedGroup = new QGroupBox("Bonus Derived Stats");
    QFormLayout* bonusDerivedLayout = new QFormLayout(bonusDerivedGroup);
    
    _critterBonusHealthPointsEdit = new QSpinBox();
    _critterBonusHealthPointsEdit->setRange(-999, 999);
    _critterBonusHealthPointsEdit->setToolTip("Bonus hit points modifier");
    bonusDerivedLayout->addRow("Bonus Hit Points:", _critterBonusHealthPointsEdit);
    
    _critterBonusActionPointsEdit = new QSpinBox();
    _critterBonusActionPointsEdit->setRange(-20, 20);
    _critterBonusActionPointsEdit->setToolTip("Bonus action points modifier");
    bonusDerivedLayout->addRow("Bonus Action Points:", _critterBonusActionPointsEdit);
    
    _critterBonusArmorClassEdit = new QSpinBox();
    _critterBonusArmorClassEdit->setRange(-50, 50);
    _critterBonusArmorClassEdit->setToolTip("Bonus armor class modifier");
    bonusDerivedLayout->addRow("Bonus Armor Class:", _critterBonusArmorClassEdit);
    
    _critterBonusMeleeDamageEdit = new QSpinBox();
    _critterBonusMeleeDamageEdit->setRange(-50, 50);
    _critterBonusMeleeDamageEdit->setToolTip("Bonus melee damage modifier");
    bonusDerivedLayout->addRow("Bonus Melee Damage:", _critterBonusMeleeDamageEdit);
    
    _critterBonusCarryWeightEdit = new QSpinBox();
    _critterBonusCarryWeightEdit->setRange(-999, 999);
    _critterBonusCarryWeightEdit->setToolTip("Bonus carry weight modifier");
    bonusDerivedLayout->addRow("Bonus Carry Weight:", _critterBonusCarryWeightEdit);
    
    _critterBonusSequenceEdit = new QSpinBox();
    _critterBonusSequenceEdit->setRange(-20, 20);
    _critterBonusSequenceEdit->setToolTip("Bonus sequence modifier");
    bonusDerivedLayout->addRow("Bonus Sequence:", _critterBonusSequenceEdit);
    
    _critterBonusHealingRateEdit = new QSpinBox();
    _critterBonusHealingRateEdit->setRange(-10, 10);
    _critterBonusHealingRateEdit->setToolTip("Bonus healing rate modifier");
    bonusDerivedLayout->addRow("Bonus Healing Rate:", _critterBonusHealingRateEdit);
    
    _critterBonusCriticalChanceEdit = new QSpinBox();
    _critterBonusCriticalChanceEdit->setRange(-50, 50);
    _critterBonusCriticalChanceEdit->setToolTip("Bonus critical chance modifier");
    bonusDerivedLayout->addRow("Bonus Critical Chance:", _critterBonusCriticalChanceEdit);
    
    _critterBonusBetterCriticalsEdit = new QSpinBox();
    _critterBonusBetterCriticalsEdit->setRange(-50, 50);
    _critterBonusBetterCriticalsEdit->setToolTip("Bonus better criticals modifier");
    bonusDerivedLayout->addRow("Bonus Better Criticals:", _critterBonusBetterCriticalsEdit);
    
    _critterBonusAgeEdit = new QSpinBox();
    _critterBonusAgeEdit->setRange(-999, 999);
    _critterBonusAgeEdit->setToolTip("Bonus age modifier");
    bonusDerivedLayout->addRow("Bonus Age:", _critterBonusAgeEdit);
    
    _critterBonusGenderEdit = new QSpinBox();
    _critterBonusGenderEdit->setRange(-1, 1);
    _critterBonusGenderEdit->setToolTip("Bonus gender modifier");
    bonusDerivedLayout->addRow("Bonus Gender:", _critterBonusGenderEdit);
    
    layout->addWidget(bonusDerivedGroup);
    
    // Bonus damage threshold group (8 damage types)
    QGroupBox* bonusThresholdGroup = new QGroupBox("Bonus Damage Threshold");
    QGridLayout* bonusThresholdLayout = new QGridLayout(bonusThresholdGroup);
    const char* bonusThresholdTypes[] = {"Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion", "Special"};
    
    for (int i = 0; i < 8; ++i) {
        _critterBonusDamageThresholdEdits[i] = new QSpinBox();
        _critterBonusDamageThresholdEdits[i]->setRange(-200, 200);
        _critterBonusDamageThresholdEdits[i]->setToolTip(QString("Bonus %1 damage threshold").arg(bonusThresholdTypes[i]));
        bonusThresholdLayout->addWidget(new QLabel(bonusThresholdTypes[i]), i / 4, (i % 4) * 2);
        bonusThresholdLayout->addWidget(_critterBonusDamageThresholdEdits[i], i / 4, (i % 4) * 2 + 1);
    }
    layout->addWidget(bonusThresholdGroup);
    
    // Bonus damage resistance group (8 damage types)
    QGroupBox* bonusResistanceGroup = new QGroupBox("Bonus Damage Resistance");
    QGridLayout* bonusResistanceLayout = new QGridLayout(bonusResistanceGroup);
    const char* bonusResistanceTypes[] = {"Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion", "Special"};
    
    for (int i = 0; i < 8; ++i) {
        _critterBonusDamageResistanceEdits[i] = new QSpinBox();
        _critterBonusDamageResistanceEdits[i]->setRange(-100, 100);
        _critterBonusDamageResistanceEdits[i]->setSuffix("%");
        _critterBonusDamageResistanceEdits[i]->setToolTip(QString("Bonus %1 damage resistance percentage").arg(bonusResistanceTypes[i]));
        bonusResistanceLayout->addWidget(new QLabel(bonusResistanceTypes[i]), i / 4, (i % 4) * 2);
        bonusResistanceLayout->addWidget(_critterBonusDamageResistanceEdits[i], i / 4, (i % 4) * 2 + 1);
    }
    layout->addWidget(bonusResistanceGroup);
    
    _critterAgeEdit = new QSpinBox();
    _critterAgeEdit->setRange(0, 999);
    _critterAgeEdit->setToolTip("Critter age");
    layout->addRow("Age:", _critterAgeEdit);
    
    _critterGenderCombo = new QComboBox();
    _critterGenderCombo->addItems({"Male", "Female"});
    _critterGenderCombo->setToolTip("Critter gender");
    layout->addRow("Gender:", _critterGenderCombo);
    
    _critterBodyTypeCombo = new QComboBox();
    _critterBodyTypeCombo->addItems({"Biped", "Quadruped", "Robotic"});
    _critterBodyTypeCombo->setToolTip("Body type for animations");
    layout->addRow("Body Type:", _critterBodyTypeCombo);
    
    _critterExperienceEdit = new QSpinBox();
    _critterExperienceEdit->setRange(0, 99999);
    _critterExperienceEdit->setToolTip("Experience points for killing this critter");
    layout->addRow("Experience:", _critterExperienceEdit);
    
    _critterKillTypeEdit = new QSpinBox();
    _critterKillTypeEdit->setRange(0, 999);
    _critterKillTypeEdit->setToolTip("Kill type for karma tracking");
    layout->addRow("Kill Type:", _critterKillTypeEdit);
    
    _critterDamageTypeEdit = new QSpinBox();
    _critterDamageTypeEdit->setRange(0, 10);
    _critterDamageTypeEdit->setToolTip("Default damage type");
    layout->addRow("Damage Type:", _critterDamageTypeEdit);
    
    // Set up scroll area
    scrollArea->setWidget(scrollWidget);
    scrollArea->setWidgetResizable(true);
    mainLayout->addWidget(scrollArea);
    
    _tabWidget->addTab(_critterTab, "Critter");
    
    // Connect signals
    connect(_critterHeadFIDEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterAIPacketEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterTeamNumberEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterFlagsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterGenderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    connect(_critterBodyTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    
    for (int i = 0; i < 7; ++i) {
        connect(_critterSpecialStatEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    }
    
    // Connect damage threshold controls
    for (int i = 0; i < 7; ++i) {
        connect(_critterDamageThresholdEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    }
    
    // Connect damage resistance controls
    for (int i = 0; i < 9; ++i) {
        connect(_critterDamageResistEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    }
    
    // Connect skill controls
    for (int i = 0; i < 18; ++i) {
        connect(_critterSkillEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    }
    
    // Connect bonus SPECIAL stat controls
    for (int i = 0; i < 7; ++i) {
        connect(_critterBonusSpecialStatEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    }
    
    // Connect bonus derived stat controls
    connect(_critterBonusHealthPointsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterBonusActionPointsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterBonusArmorClassEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterBonusMeleeDamageEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterBonusCarryWeightEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterBonusSequenceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterBonusHealingRateEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterBonusCriticalChanceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterBonusBetterCriticalsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterBonusAgeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_critterBonusGenderEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    
    // Connect bonus damage threshold controls
    for (int i = 0; i < 8; ++i) {
        connect(_critterBonusDamageThresholdEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    }
    
    // Connect bonus damage resistance controls
    for (int i = 0; i < 8; ++i) {
        connect(_critterBonusDamageResistanceEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    }
}

void ProEditorDialog::setupSceneryTab() {
    _sceneryTab = new QWidget();
    QFormLayout* layout = new QFormLayout(_sceneryTab);
    
    _sceneryMaterialIdEdit = new QSpinBox();
    _sceneryMaterialIdEdit->setRange(0, 999999);
    _sceneryMaterialIdEdit->setToolTip("Material ID for scenery");
    layout->addRow("Material ID:", _sceneryMaterialIdEdit);
    
    _scenerySoundIdEdit = new QSpinBox();
    _scenerySoundIdEdit->setRange(0, 255);
    _scenerySoundIdEdit->setToolTip("Sound ID for interactions");
    layout->addRow("Sound ID:", _scenerySoundIdEdit);
    
    _sceneryTypeCombo = new QComboBox();
    _sceneryTypeCombo->addItems({"Door", "Stairs", "Elevator", "Ladder Bottom", "Ladder Top", "Generic"});
    _sceneryTypeCombo->setToolTip("Type of scenery object");
    layout->addRow("Scenery Type:", _sceneryTypeCombo);
    
    // Door-specific controls
    QGroupBox* doorGroup = new QGroupBox("Door Properties");
    QFormLayout* doorLayout = new QFormLayout(doorGroup);
    
    _doorWalkThroughCheck = new QCheckBox();
    _doorWalkThroughCheck->setToolTip("Can walk through this door");
    doorLayout->addRow("Walk Through:", _doorWalkThroughCheck);
    
    _doorUnknownEdit = new QSpinBox();
    _doorUnknownEdit->setRange(0, 999999);
    _doorUnknownEdit->setToolTip("Unknown door field");
    doorLayout->addRow("Unknown Field:", _doorUnknownEdit);
    
    layout->addWidget(doorGroup);
    
    // Stairs-specific controls
    QGroupBox* stairsGroup = new QGroupBox("Stairs Properties");
    QFormLayout* stairsLayout = new QFormLayout(stairsGroup);
    
    _stairsDestTileEdit = new QSpinBox();
    _stairsDestTileEdit->setRange(0, 39999);
    _stairsDestTileEdit->setToolTip("Destination tile for stairs");
    stairsLayout->addRow("Dest Tile:", _stairsDestTileEdit);
    
    _stairsDestElevationEdit = new QSpinBox();
    _stairsDestElevationEdit->setRange(0, 3);
    _stairsDestElevationEdit->setToolTip("Destination elevation");
    stairsLayout->addRow("Dest Elevation:", _stairsDestElevationEdit);
    
    layout->addWidget(stairsGroup);
    
    // Elevator-specific controls
    QGroupBox* elevatorGroup = new QGroupBox("Elevator Properties");
    QFormLayout* elevatorLayout = new QFormLayout(elevatorGroup);
    
    _elevatorTypeEdit = new QSpinBox();
    _elevatorTypeEdit->setRange(0, 999);
    _elevatorTypeEdit->setToolTip("Elevator type");
    elevatorLayout->addRow("Elevator Type:", _elevatorTypeEdit);
    
    _elevatorLevelEdit = new QSpinBox();
    _elevatorLevelEdit->setRange(0, 999);
    _elevatorLevelEdit->setToolTip("Elevator level");
    elevatorLayout->addRow("Elevator Level:", _elevatorLevelEdit);
    
    layout->addWidget(elevatorGroup);
    
    // Ladder-specific controls
    QGroupBox* ladderGroup = new QGroupBox("Ladder Properties");
    QFormLayout* ladderLayout = new QFormLayout(ladderGroup);
    
    _ladderDestTileElevationEdit = new QSpinBox();
    _ladderDestTileElevationEdit->setRange(0, 999999);
    _ladderDestTileElevationEdit->setToolTip("Combined destination tile and elevation");
    ladderLayout->addRow("Dest Tile+Elevation:", _ladderDestTileElevationEdit);
    
    layout->addWidget(ladderGroup);
    
    // Generic-specific controls
    QGroupBox* genericGroup = new QGroupBox("Generic Properties");
    QFormLayout* genericLayout = new QFormLayout(genericGroup);
    
    _genericUnknownEdit = new QSpinBox();
    _genericUnknownEdit->setRange(0, 999999);
    _genericUnknownEdit->setToolTip("Unknown generic field");
    genericLayout->addRow("Unknown Field:", _genericUnknownEdit);
    
    layout->addWidget(genericGroup);
    
    _tabWidget->addTab(_sceneryTab, "Scenery");
    
    // Connect signals
    connect(_sceneryMaterialIdEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_scenerySoundIdEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_sceneryTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    connect(_doorWalkThroughCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCheckBoxChanged);
    connect(_doorUnknownEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_stairsDestTileEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    connect(_stairsDestElevationEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
}

void ProEditorDialog::loadCritterData() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::CRITTER) {
        spdlog::warn("ProEditorDialog: Cannot load critter data - not a critter PRO");
        return;
    }
    
    const auto& critterData = _pro->critterData;
    
    // Load basic critter properties
    if (_critterHeadFIDEdit) _critterHeadFIDEdit->setValue(static_cast<int>(critterData.headFID));
    if (_critterAIPacketEdit) _critterAIPacketEdit->setValue(static_cast<int>(critterData.aiPacket));
    if (_critterTeamNumberEdit) _critterTeamNumberEdit->setValue(static_cast<int>(critterData.teamNumber));
    if (_critterFlagsEdit) _critterFlagsEdit->setValue(static_cast<int>(critterData.flags));
    if (_critterMaxHitPointsEdit) _critterMaxHitPointsEdit->setValue(static_cast<int>(critterData.maxHitPoints));
    if (_critterActionPointsEdit) _critterActionPointsEdit->setValue(static_cast<int>(critterData.actionPoints));
    if (_critterArmorClassEdit) _critterArmorClassEdit->setValue(static_cast<int>(critterData.armorClass));
    if (_critterMeleeDamageEdit) _critterMeleeDamageEdit->setValue(static_cast<int>(critterData.meleeDamage));
    if (_critterCarryWeightMaxEdit) _critterCarryWeightMaxEdit->setValue(static_cast<int>(critterData.carryWeightMax));
    if (_critterSequenceEdit) _critterSequenceEdit->setValue(static_cast<int>(critterData.sequence));
    if (_critterHealingRateEdit) _critterHealingRateEdit->setValue(static_cast<int>(critterData.healingRate));
    if (_critterCriticalChanceEdit) _critterCriticalChanceEdit->setValue(static_cast<int>(critterData.criticalChance));
    if (_critterBetterCriticalsEdit) _critterBetterCriticalsEdit->setValue(static_cast<int>(critterData.betterCriticals));
    if (_critterAgeEdit) _critterAgeEdit->setValue(static_cast<int>(critterData.age));
    if (_critterGenderCombo) _critterGenderCombo->setCurrentIndex(static_cast<int>(critterData.gender));
    if (_critterBodyTypeCombo) _critterBodyTypeCombo->setCurrentIndex(static_cast<int>(critterData.bodyType));
    if (_critterExperienceEdit) _critterExperienceEdit->setValue(static_cast<int>(critterData.experienceForKill));
    if (_critterKillTypeEdit) _critterKillTypeEdit->setValue(static_cast<int>(critterData.killType));
    if (_critterDamageTypeEdit) _critterDamageTypeEdit->setValue(static_cast<int>(critterData.damageType));
    
    // Load SPECIAL stats (STR, PER, END, CHR, INT, AGL, LCK)
    for (int i = 0; i < 7; ++i) {
        if (_critterSpecialStatEdits[i]) {
            _critterSpecialStatEdits[i]->setValue(static_cast<int>(critterData.specialStats[i]));
        }
    }
    
    // Load damage thresholds (7 damage types)
    for (int i = 0; i < 7; ++i) {
        if (_critterDamageThresholdEdits[i]) {
            _critterDamageThresholdEdits[i]->setValue(static_cast<int>(critterData.damageThreshold[i]));
        }
    }
    
    // Load damage resistances (9 damage types)
    for (int i = 0; i < 9; ++i) {
        if (_critterDamageResistEdits[i]) {
            _critterDamageResistEdits[i]->setValue(static_cast<int>(critterData.damageResist[i]));
        }
    }
    
    // Load skills (18 different skills)
    for (int i = 0; i < 18; ++i) {
        if (_critterSkillEdits[i]) {
            _critterSkillEdits[i]->setValue(static_cast<int>(critterData.skills[i]));
        }
    }
    
    // Load bonus SPECIAL stats (7 stats)
    for (int i = 0; i < 7; ++i) {
        if (_critterBonusSpecialStatEdits[i]) {
            _critterBonusSpecialStatEdits[i]->setValue(static_cast<int>(critterData.bonusSpecialStats[i]));
        }
    }
    
    // Load bonus derived stats
    if (_critterBonusHealthPointsEdit) _critterBonusHealthPointsEdit->setValue(static_cast<int>(critterData.bonusHealthPoints));
    if (_critterBonusActionPointsEdit) _critterBonusActionPointsEdit->setValue(static_cast<int>(critterData.bonusActionPoints));
    if (_critterBonusArmorClassEdit) _critterBonusArmorClassEdit->setValue(static_cast<int>(critterData.bonusArmorClass));
    if (_critterBonusMeleeDamageEdit) _critterBonusMeleeDamageEdit->setValue(static_cast<int>(critterData.bonusMeleeDamage));
    if (_critterBonusCarryWeightEdit) _critterBonusCarryWeightEdit->setValue(static_cast<int>(critterData.bonusCarryWeight));
    if (_critterBonusSequenceEdit) _critterBonusSequenceEdit->setValue(static_cast<int>(critterData.bonusSequence));
    if (_critterBonusHealingRateEdit) _critterBonusHealingRateEdit->setValue(static_cast<int>(critterData.bonusHealingRate));
    if (_critterBonusCriticalChanceEdit) _critterBonusCriticalChanceEdit->setValue(static_cast<int>(critterData.bonusCriticalChance));
    if (_critterBonusBetterCriticalsEdit) _critterBonusBetterCriticalsEdit->setValue(static_cast<int>(critterData.bonusBetterCriticals));
    if (_critterBonusAgeEdit) _critterBonusAgeEdit->setValue(static_cast<int>(critterData.bonusAge));
    if (_critterBonusGenderEdit) _critterBonusGenderEdit->setValue(static_cast<int>(critterData.bonusGender));
    
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
    if (_sceneryMaterialIdEdit) _sceneryMaterialIdEdit->setValue(static_cast<int>(sceneryData.materialId));
    if (_scenerySoundIdEdit) _scenerySoundIdEdit->setValue(static_cast<int>(sceneryData.soundId));
    
    // Set scenery type based on object subtype
    if (_sceneryTypeCombo) {
        Pro::SCENERY_TYPE sceneryType = static_cast<Pro::SCENERY_TYPE>(_pro->objectSubtypeId());
        _sceneryTypeCombo->setCurrentIndex(static_cast<int>(sceneryType));
    }
    
    // Load type-specific data based on scenery subtype
    Pro::SCENERY_TYPE sceneryType = static_cast<Pro::SCENERY_TYPE>(_pro->objectSubtypeId());
    
    switch (sceneryType) {
        case Pro::SCENERY_TYPE::DOOR:
            if (_doorWalkThroughCheck) _doorWalkThroughCheck->setChecked(sceneryData.doorData.walkThroughFlag != 0);
            if (_doorUnknownEdit) _doorUnknownEdit->setValue(static_cast<int>(sceneryData.doorData.unknownField));
            break;
            
        case Pro::SCENERY_TYPE::STAIRS:
            if (_stairsDestTileEdit) _stairsDestTileEdit->setValue(static_cast<int>(sceneryData.stairsData.destTile));
            if (_stairsDestElevationEdit) _stairsDestElevationEdit->setValue(static_cast<int>(sceneryData.stairsData.destElevation));
            break;
            
        case Pro::SCENERY_TYPE::ELEVATOR:
            if (_elevatorTypeEdit) _elevatorTypeEdit->setValue(static_cast<int>(sceneryData.elevatorData.elevatorType));
            if (_elevatorLevelEdit) _elevatorLevelEdit->setValue(static_cast<int>(sceneryData.elevatorData.elevatorLevel));
            break;
            
        case Pro::SCENERY_TYPE::LADDER_BOTTOM:
        case Pro::SCENERY_TYPE::LADDER_TOP:
            if (_ladderDestTileElevationEdit) _ladderDestTileElevationEdit->setValue(static_cast<int>(sceneryData.ladderData.destTileAndElevation));
            break;
            
        case Pro::SCENERY_TYPE::GENERIC:
        default:
            if (_genericUnknownEdit) _genericUnknownEdit->setValue(static_cast<int>(sceneryData.genericData.unknownField));
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
    if (_critterHeadFIDEdit) critterData.headFID = static_cast<uint32_t>(_critterHeadFIDEdit->value());
    if (_critterAIPacketEdit) critterData.aiPacket = static_cast<uint32_t>(_critterAIPacketEdit->value());
    if (_critterTeamNumberEdit) critterData.teamNumber = static_cast<uint32_t>(_critterTeamNumberEdit->value());
    if (_critterFlagsEdit) critterData.flags = static_cast<uint32_t>(_critterFlagsEdit->value());
    if (_critterMaxHitPointsEdit) critterData.maxHitPoints = static_cast<uint32_t>(_critterMaxHitPointsEdit->value());
    if (_critterActionPointsEdit) critterData.actionPoints = static_cast<uint32_t>(_critterActionPointsEdit->value());
    if (_critterArmorClassEdit) critterData.armorClass = static_cast<uint32_t>(_critterArmorClassEdit->value());
    if (_critterMeleeDamageEdit) critterData.meleeDamage = static_cast<uint32_t>(_critterMeleeDamageEdit->value());
    if (_critterCarryWeightMaxEdit) critterData.carryWeightMax = static_cast<uint32_t>(_critterCarryWeightMaxEdit->value());
    if (_critterSequenceEdit) critterData.sequence = static_cast<uint32_t>(_critterSequenceEdit->value());
    if (_critterHealingRateEdit) critterData.healingRate = static_cast<uint32_t>(_critterHealingRateEdit->value());
    if (_critterCriticalChanceEdit) critterData.criticalChance = static_cast<uint32_t>(_critterCriticalChanceEdit->value());
    if (_critterBetterCriticalsEdit) critterData.betterCriticals = static_cast<uint32_t>(_critterBetterCriticalsEdit->value());
    if (_critterAgeEdit) critterData.age = static_cast<uint32_t>(_critterAgeEdit->value());
    if (_critterGenderCombo) critterData.gender = static_cast<uint32_t>(_critterGenderCombo->currentIndex());
    if (_critterBodyTypeCombo) critterData.bodyType = static_cast<uint32_t>(_critterBodyTypeCombo->currentIndex());
    if (_critterExperienceEdit) critterData.experienceForKill = static_cast<uint32_t>(_critterExperienceEdit->value());
    if (_critterKillTypeEdit) critterData.killType = static_cast<uint32_t>(_critterKillTypeEdit->value());
    if (_critterDamageTypeEdit) critterData.damageType = static_cast<uint32_t>(_critterDamageTypeEdit->value());
    
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
    if (_critterBonusHealthPointsEdit) critterData.bonusHealthPoints = static_cast<uint32_t>(_critterBonusHealthPointsEdit->value());
    if (_critterBonusActionPointsEdit) critterData.bonusActionPoints = static_cast<uint32_t>(_critterBonusActionPointsEdit->value());
    if (_critterBonusArmorClassEdit) critterData.bonusArmorClass = static_cast<uint32_t>(_critterBonusArmorClassEdit->value());
    if (_critterBonusMeleeDamageEdit) critterData.bonusMeleeDamage = static_cast<uint32_t>(_critterBonusMeleeDamageEdit->value());
    if (_critterBonusCarryWeightEdit) critterData.bonusCarryWeight = static_cast<uint32_t>(_critterBonusCarryWeightEdit->value());
    if (_critterBonusSequenceEdit) critterData.bonusSequence = static_cast<uint32_t>(_critterBonusSequenceEdit->value());
    if (_critterBonusHealingRateEdit) critterData.bonusHealingRate = static_cast<uint32_t>(_critterBonusHealingRateEdit->value());
    if (_critterBonusCriticalChanceEdit) critterData.bonusCriticalChance = static_cast<uint32_t>(_critterBonusCriticalChanceEdit->value());
    if (_critterBonusBetterCriticalsEdit) critterData.bonusBetterCriticals = static_cast<uint32_t>(_critterBonusBetterCriticalsEdit->value());
    if (_critterBonusAgeEdit) critterData.bonusAge = static_cast<uint32_t>(_critterBonusAgeEdit->value());
    if (_critterBonusGenderEdit) critterData.bonusGender = static_cast<uint32_t>(_critterBonusGenderEdit->value());
    
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
    if (_sceneryMaterialIdEdit) sceneryData.materialId = static_cast<uint32_t>(_sceneryMaterialIdEdit->value());
    if (_scenerySoundIdEdit) sceneryData.soundId = static_cast<uint8_t>(_scenerySoundIdEdit->value());
    
    // Update object subtype if scenery type changed
    if (_sceneryTypeCombo) {
        _pro->setObjectSubtypeId(static_cast<unsigned int>(_sceneryTypeCombo->currentIndex()));
    }
    
    // Save type-specific data based on current scenery subtype
    Pro::SCENERY_TYPE sceneryType = static_cast<Pro::SCENERY_TYPE>(_pro->objectSubtypeId());
    
    switch (sceneryType) {
        case Pro::SCENERY_TYPE::DOOR:
            if (_doorWalkThroughCheck) sceneryData.doorData.walkThroughFlag = _doorWalkThroughCheck->isChecked() ? 1 : 0;
            if (_doorUnknownEdit) sceneryData.doorData.unknownField = static_cast<uint32_t>(_doorUnknownEdit->value());
            break;
            
        case Pro::SCENERY_TYPE::STAIRS:
            if (_stairsDestTileEdit) sceneryData.stairsData.destTile = static_cast<uint32_t>(_stairsDestTileEdit->value());
            if (_stairsDestElevationEdit) sceneryData.stairsData.destElevation = static_cast<uint32_t>(_stairsDestElevationEdit->value());
            break;
            
        case Pro::SCENERY_TYPE::ELEVATOR:
            if (_elevatorTypeEdit) sceneryData.elevatorData.elevatorType = static_cast<uint32_t>(_elevatorTypeEdit->value());
            if (_elevatorLevelEdit) sceneryData.elevatorData.elevatorLevel = static_cast<uint32_t>(_elevatorLevelEdit->value());
            break;
            
        case Pro::SCENERY_TYPE::LADDER_BOTTOM:
        case Pro::SCENERY_TYPE::LADDER_TOP:
            if (_ladderDestTileElevationEdit) sceneryData.ladderData.destTileAndElevation = static_cast<uint32_t>(_ladderDestTileElevationEdit->value());
            break;
            
        case Pro::SCENERY_TYPE::GENERIC:
        default:
            if (_genericUnknownEdit) sceneryData.genericData.unknownField = static_cast<uint32_t>(_genericUnknownEdit->value());
            break;
    }
    
}

QPixmap ProEditorDialog::createFrmThumbnail(const std::string& frmPath, const QSize& targetSize) {
    
    QPixmap thumbnail(targetSize);
    thumbnail.fill(Qt::transparent);

    try {
        auto& resourceManager = ResourceManager::getInstance();

        // Load the FRM object directly (not the texture)
        const auto* frm = resourceManager.loadResource<Frm>(frmPath);
        if (!frm) {
            return thumbnail;
        }

        // Get the first direction and first frame for preview
        const auto& directions = frm->directions();
        if (!directions.empty()) {
            const auto& firstDirection = directions[0];
            const auto& frames = firstDirection.frames();
            
            if (!frames.empty()) {
                const auto& firstFrame = frames[0];
                
                // Load default palette for color conversion
                const Pal* palette = nullptr;
                try {
                    palette = resourceManager.loadResource<Pal>("color.pal");
                } catch (const std::exception& e) {
                    spdlog::warn("ProEditorDialog: Could not load color.pal for {}, falling back to placeholder", frmPath);
                    return thumbnail; // Return empty thumbnail without palette
                }
                
                if (palette) {
                    // Convert single frame to thumbnail
                    thumbnail = createFrameThumbnail(firstFrame, palette, targetSize);
                    return thumbnail;
                }
            } else {
            }
        } else {
        }
    } catch (const std::exception& e) {
        spdlog::warn("ProEditorDialog: Exception creating thumbnail for {}: {}", frmPath, e.what());
    }

    return thumbnail; // Return empty/transparent thumbnail on failure
}

QPixmap ProEditorDialog::createFrameThumbnail(const Frame& frame, const Pal* palette, const QSize& targetSize) {
    QPixmap thumbnail(targetSize);
    thumbnail.fill(Qt::transparent);

    // Get frame dimensions
    uint16_t frameWidth = frame.width();
    uint16_t frameHeight = frame.height();
    
    if (frameWidth == 0 || frameHeight == 0) {
        return thumbnail;
    }

    // Always use RGBA data with palette - no fallback to grayscale
    uint8_t* rgbaData = const_cast<Frame&>(frame).rgba(const_cast<Pal*>(palette));
    if (!rgbaData) {
        return thumbnail;
    }
    
    QImage frameImage(rgbaData, frameWidth, frameHeight, QImage::Format_RGBA8888);
    frameImage = frameImage.copy(); // Make a copy since rgbaData might be temporary

    // Scale frame to fit thumbnail size while preserving aspect ratio
    QPixmap framePixmap = QPixmap::fromImage(frameImage);
    
    // Calculate optimal scaling with maximum 2x original size
    constexpr double MAX_SCALE = 2.0;
    
    // Calculate scaling factors to fit target size
    double scaleX = static_cast<double>(targetSize.width()) / frameWidth;
    double scaleY = static_cast<double>(targetSize.height()) / frameHeight;
    double scale = qMin(scaleX, scaleY);
    
    // Limit scale to maximum 2x original size for crisp display
    scale = qMin(scale, MAX_SCALE);
    
    // Calculate final size
    int scaledWidth = static_cast<int>(frameWidth * scale);
    int scaledHeight = static_cast<int>(frameHeight * scale);
    
    // Use high-quality scaling directly from QImage to QPixmap
    QImage scaledImage = frameImage.scaled(scaledWidth, scaledHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap scaledFrame = QPixmap::fromImage(scaledImage);
    
    // Center the scaled frame in the thumbnail with enhanced rendering
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    int x = (targetSize.width() - scaledFrame.width()) / 2;
    int y = (targetSize.height() - scaledFrame.height()) / 2;
    
    painter.drawPixmap(x, y, scaledFrame);
    painter.end();
    
    return thumbnail;
}

void ProEditorDialog::loadNameAndDescription() {
    if (!_nameLabel || !_descriptionLabel) {
        return;
    }
    
    try {
        auto& resourceManager = ResourceManager::getInstance();
        
        // Determine MSG file based on object type
        std::string msgFileName;
        switch (_pro->type()) {
            case Pro::OBJECT_TYPE::ITEM:
                msgFileName = "text/english/game/pro_item.msg";
                break;
            case Pro::OBJECT_TYPE::CRITTER:
                msgFileName = "text/english/game/pro_crit.msg";
                break;
            case Pro::OBJECT_TYPE::SCENERY:
                msgFileName = "text/english/game/pro_scen.msg";
                break;
            case Pro::OBJECT_TYPE::WALL:
                msgFileName = "text/english/game/pro_wall.msg";
                break;
            case Pro::OBJECT_TYPE::TILE:
                msgFileName = "text/english/game/pro_tile.msg";
                break;
            case Pro::OBJECT_TYPE::MISC:
                msgFileName = "text/english/game/pro_misc.msg";
                break;
            default:
                _nameLabel->setText("Unknown object type");
                _descriptionLabel->setPlainText("Cannot load description for unknown object type");
                return;
        }
        
        
        // Load the MSG file
        const auto* msgFile = resourceManager.loadResource<Msg>(msgFileName);
        if (!msgFile) {
            _nameLabel->setText("MSG file not found");
            _descriptionLabel->setPlainText(QString("Could not load: %1").arg(QString::fromStdString(msgFileName)));
            spdlog::warn("ProEditorDialog::loadNameAndDescription() - MSG file not found: {}", msgFileName);
            return;
        }
        
        uint32_t messageId = _pro->header.message_id;
        
        // Get name (message at messageId)
        std::string name;
        try {
            const auto& nameMessage = const_cast<Msg*>(msgFile)->message(messageId);
            name = nameMessage.text;
        } catch (const std::exception& e) {
            name = QString("No name (ID: %1)").arg(messageId).toStdString();
        }
        
        if (name.empty()) {
            name = QString("No name (ID: %1)").arg(messageId).toStdString();
        }
        
        // Get description (message at messageId + 1)
        std::string description;
        try {
            const auto& descMessage = const_cast<Msg*>(msgFile)->message(messageId + 1);
            description = descMessage.text;
        } catch (const std::exception& e) {
            description = QString("No description (ID: %1)").arg(messageId + 1).toStdString();
        }
        
        if (description.empty()) {
            description = QString("No description (ID: %1)").arg(messageId + 1).toStdString();
        }
        
        
        // Update labels
        _nameLabel->setText(QString::fromStdString(name));
        _descriptionLabel->setPlainText(QString::fromStdString(description));
        
        // Auto-resize the description text area to fit content
        QTextDocument* doc = _descriptionLabel->document();
        doc->setTextWidth(_descriptionLabel->viewport()->width());
        int docHeight = doc->size().height() + 10; // Add some padding
        int clampedHeight = std::max(30, std::min(docHeight, 120)); // Clamp between min and max
        _descriptionLabel->setFixedHeight(clampedHeight);
        
    } catch (const std::exception& e) {
        spdlog::error("ProEditorDialog::loadNameAndDescription() - exception: {}", e.what());
        _nameLabel->setText("Error loading name");
        _descriptionLabel->setPlainText(QString("Error: %1").arg(e.what()));
    }
}

void ProEditorDialog::onPlayPauseClicked() {
    if (_totalFrames <= 1) {
        return; // Nothing to animate
    }
    
    if (_isAnimating) {
        _animationTimer->stop();
        _playPauseButton->setText("▶");
        _playPauseButton->setToolTip("Play animation");
        _isAnimating = false;
    } else {
        _animationTimer->start();
        _playPauseButton->setText("⏸");
        _playPauseButton->setToolTip("Pause animation");
        _isAnimating = true;
    }
    
}

void ProEditorDialog::onFrameChanged(int frame) {
    if (frame != _currentFrame) {
        _currentFrame = frame;
        _frameLabel->setText(QString("%1/%2").arg(frame + 1).arg(_totalFrames));
        
        // Update preview with new frame
        if (frame < _frameCache.size() && !_frameCache[frame].isNull()) {
            // Items use static dual preview, not animation frames
            if (_previewLabel) {
                // Single preview mode for critters and scenery
                _previewLabel->setPixmap(_frameCache[frame]);
            }
        }
        
    }
}

void ProEditorDialog::onDirectionChanged(int direction) {
    if (direction != _currentDirection) {
        _currentDirection = direction;
        
        // Reload frames for new direction
        loadAnimationFrames();
        
    }
}

void ProEditorDialog::onAnimationTick() {
    if (_totalFrames > 1) {
        int nextFrame = (_currentFrame + 1) % _totalFrames;
        _frameSlider->setValue(nextFrame);
    }
}

void ProEditorDialog::loadAnimationFrames() {
    _frameCache.clear();
    
    if (!_pro) {
        return;
    }
    
    try {
        int32_t previewFid = getPreviewFid();
        if (previewFid <= 0) {
            return;
        }
        
        auto& resourceManager = ResourceManager::getInstance();
        std::string frmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(previewFid));
        
        if (frmPath.empty()) {
            return;
        }
        
        // Load the FRM
        const auto* frm = resourceManager.loadResource<Frm>(frmPath);
        if (!frm) {
            return;
        }
        
        const auto& directions = frm->directions();
        if (directions.empty() || _currentDirection >= static_cast<int>(directions.size())) {
            return;
        }
        
        const auto& direction = directions[_currentDirection];
        const auto& frames = direction.frames();
        
        _totalFrames = static_cast<int>(frames.size());
        _totalDirections = static_cast<int>(directions.size());
        
        // Load palette
        const Pal* palette = resourceManager.loadResource<Pal>("color.pal");
        if (!palette) {
            return;
        }
        
        // Generate thumbnails for all frames using fixed target size for better quality
        _frameCache.reserve(_totalFrames);
        QSize frameTargetSize(200, 200); // Fixed size for crisp animation frames
        
        for (const auto& frame : frames) {
            QPixmap thumbnail = createFrameThumbnail(frame, palette, frameTargetSize);
            _frameCache.push_back(thumbnail);
        }
        
        // Update UI
        _frameSlider->setMaximum(_totalFrames - 1);
        _frameSlider->setValue(0);
        _frameLabel->setText(QString("1/%1").arg(_totalFrames));
        
        // Enable/disable direction combo based on available directions
        _directionCombo->setEnabled(_totalDirections > 1);
        
        // Enable animation controls only for critters and scenery with multiple frames
        // Items never animate (they have static single-frame FRMs)
        bool shouldEnableAnimation = false;
        if (_pro) {
            if (_pro->type() == Pro::OBJECT_TYPE::CRITTER) {
                // Critters can have animations
                shouldEnableAnimation = (_totalFrames > 1 || _totalDirections > 1);
            } else if (_pro->type() == Pro::OBJECT_TYPE::SCENERY) {
                // Some scenery can have animations (doors, etc.)
                shouldEnableAnimation = (_totalFrames > 1 || _totalDirections > 1);
            }
            // Items, walls, tiles, misc never animate
        }
        _animationControls->setEnabled(shouldEnableAnimation);
        
 
        
        // Show the first frame in the appropriate preview label
        if (!_frameCache.empty() && !_frameCache[0].isNull()) {
            // Items use static dual preview, not animation frames
            if (_previewLabel) {
                // Single preview mode for critters and scenery
                _previewLabel->setPixmap(_frameCache[0]);
            }
        }
        
        
    } catch (const std::exception& e) {
        spdlog::warn("ProEditorDialog: Exception loading animation frames: {}", e.what());
        _animationControls->setEnabled(false);
    }
}

void ProEditorDialog::addValidationIssue(QWidget* field, const QString& message, ValidationLevel level, const QString& category) {
    ValidationIssue issue;
    issue.field = field;
    issue.message = message;
    issue.level = level;
    issue.category = category;
    _validationIssues.push_back(issue);
    
}

void ProEditorDialog::clearValidationIssues(QWidget* field) {
    if (field) {
        // Clear issues for specific field
        _validationIssues.erase(
            std::remove_if(_validationIssues.begin(), _validationIssues.end(),
                [field](const ValidationIssue& issue) { return issue.field == field; }),
            _validationIssues.end());
    } else {
        // Clear all issues
        _validationIssues.clear();
    }
}

bool ProEditorDialog::validateFIDReference(int32_t fid, const QString& context) {
    if (fid == 0) {
        return true; // 0 is valid (no FRM)
    }
    
    if (fid == -1) {
        return true; // -1 is valid (indicates no FRM/invalid reference)
    }
    
    try {
        auto& resourceManager = ResourceManager::getInstance();
        std::string frmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(fid));
        
        if (frmPath.empty()) {
            return false;
        }
        
        // Try to load the FRM to verify it exists
        const auto* frm = resourceManager.loadResource<Frm>(frmPath);
        if (!frm) {
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

bool ProEditorDialog::validateStatValue(int value, int min, int max, const QString& statName) {
    bool isValid = (value >= min && value <= max);
    if (!isValid) {
    }
    return isValid;
}

void ProEditorDialog::updateValidationStatus() {
    // Count validation issues by level
    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;
    
    for (const auto& issue : _validationIssues) {
        switch (issue.level) {
            case ValidationLevel::ERROR:
                errorCount++;
                break;
            case ValidationLevel::WARNING:
                warningCount++;
                break;
            case ValidationLevel::INFO:
                infoCount++;
                break;
        }
    }
    
    // Update window title with validation status
    QString title = "PRO Editor";
    if (errorCount > 0) {
        title += QString(" - %1 Error%2").arg(errorCount).arg(errorCount > 1 ? "s" : "");
    }
    if (warningCount > 0) {
        title += QString("%1%2 Warning%3")
            .arg(errorCount > 0 ? ", " : " - ")
            .arg(warningCount)
            .arg(warningCount > 1 ? "s" : "");
    }
    if (infoCount > 0) {
        title += QString("%1%2 Info")
            .arg((errorCount > 0 || warningCount > 0) ? ", " : " - ")
            .arg(infoCount);
    }
    
    setWindowTitle(title);
    
    
    // Update validation panel
    updateValidationPanel();
}

void ProEditorDialog::onValidationToggleClicked() {
    _validationPanelVisible = !_validationPanelVisible;
    _validationGroup->setVisible(_validationPanelVisible);
    
    if (_validationPanelVisible) {
        _validationToggleButton->setText("▼ Validation Issues");
    } else {
        _validationToggleButton->setText("▶ Validation Issues");
    }
    
}

void ProEditorDialog::onValidationItemDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    
    // Get the field widget from the item data
    QWidget* field = qvariant_cast<QWidget*>(item->data(Qt::UserRole));
    if (field) {
        // Find the tab containing this field and switch to it
        QWidget* parent = field;
        while (parent && parent->parent()) {
            parent = qobject_cast<QWidget*>(parent->parent());
            if (parent && _tabWidget) {
                int tabIndex = _tabWidget->indexOf(parent);
                if (tabIndex >= 0) {
                    _tabWidget->setCurrentIndex(tabIndex);
                    break;
                }
            }
        }
        
        // Focus and scroll to the field
        field->setFocus();
        
        // If it's in a scroll area, scroll to make it visible
        QScrollArea* scrollArea = nullptr;
        QWidget* scrollParent = field;
        while (scrollParent && !scrollArea) {
            scrollParent = qobject_cast<QWidget*>(scrollParent->parent());
            scrollArea = qobject_cast<QScrollArea*>(scrollParent);
        }
        
        if (scrollArea) {
            scrollArea->ensureWidgetVisible(field);
        }
        
    }
}

void ProEditorDialog::onExtendedFlagChanged() {
    uint32_t flags = 0;
    
    // Rebuild flags from individual controls
    flags |= Pro::setAnimationPrimary(flags, _animationPrimaryEdit ? _animationPrimaryEdit->value() : 0);
    flags |= Pro::setAnimationSecondary(flags, _animationSecondaryEdit ? _animationSecondaryEdit->value() : 0);
    
    if (_bigGunCheck && _bigGunCheck->isChecked()) {
        flags |= static_cast<uint32_t>(Pro::EXTENDED_FLAGS::BIG_GUN);
    }
    if (_twoHandedCheck && _twoHandedCheck->isChecked()) {
        flags |= static_cast<uint32_t>(Pro::EXTENDED_FLAGS::TWO_HANDED);
    }
    if (_canUseCheck && _canUseCheck->isChecked()) {
        flags |= static_cast<uint32_t>(Pro::EXTENDED_FLAGS::CAN_USE);
    }
    if (_canUseOnCheck && _canUseOnCheck->isChecked()) {
        flags |= static_cast<uint32_t>(Pro::EXTENDED_FLAGS::CAN_USE_ON);
    }
    if (_generalFlagCheck && _generalFlagCheck->isChecked()) {
        flags |= static_cast<uint32_t>(Pro::EXTENDED_FLAGS::GENERAL_FLAG);
    }
    if (_interactionFlagCheck && _interactionFlagCheck->isChecked()) {
        flags |= static_cast<uint32_t>(Pro::EXTENDED_FLAGS::INTERACTION_FLAG);
    }
    if (_itemHiddenCheck && _itemHiddenCheck->isChecked()) {
        flags |= static_cast<uint32_t>(Pro::EXTENDED_FLAGS::ITEM_HIDDEN);
    }
    if (_lightFlag1Check && _lightFlag1Check->isChecked()) {
        flags |= static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_1);
    }
    if (_lightFlag2Check && _lightFlag2Check->isChecked()) {
        flags |= static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_2);
    }
    if (_lightFlag3Check && _lightFlag3Check->isChecked()) {
        flags |= static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_3);
    }
    if (_lightFlag4Check && _lightFlag4Check->isChecked()) {
        flags |= static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_4);
    }
    
    // Update the raw editor
    if (_flagsExtRawEdit) {
        _flagsExtRawEdit->blockSignals(true);
        _flagsExtRawEdit->setValue(flags);
        _flagsExtRawEdit->blockSignals(false);
    }
    
    // Store in pro data (different location based on object type)
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        _pro->commonItemData.flagsExt = flags;
    } else {
        // For critters, scenery, walls - store in header flags
        _pro->header.flags = (_pro->header.flags & 0x0FFFFFFF) | (flags & 0xF0000000); // Keep lower bits, update upper bits
    }
    
}

void ProEditorDialog::onExtendedFlagRawChanged() {
    if (!_flagsExtRawEdit) return;
    
    uint32_t flags = _flagsExtRawEdit->value();
    
    // Update all individual controls based on the raw value
    if (_animationPrimaryEdit) {
        _animationPrimaryEdit->blockSignals(true);
        _animationPrimaryEdit->setValue(Pro::getAnimationPrimary(flags));
        _animationPrimaryEdit->blockSignals(false);
    }
    
    if (_animationSecondaryEdit) {
        _animationSecondaryEdit->blockSignals(true);
        _animationSecondaryEdit->setValue(Pro::getAnimationSecondary(flags));
        _animationSecondaryEdit->blockSignals(false);
    }
    
    // Update checkboxes
    if (_bigGunCheck) {
        _bigGunCheck->blockSignals(true);
        _bigGunCheck->setChecked(flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::BIG_GUN));
        _bigGunCheck->blockSignals(false);
    }
    
    if (_twoHandedCheck) {
        _twoHandedCheck->blockSignals(true);
        _twoHandedCheck->setChecked(flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::TWO_HANDED));
        _twoHandedCheck->blockSignals(false);
    }
    
    if (_canUseCheck) {
        _canUseCheck->blockSignals(true);
        _canUseCheck->setChecked(flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::CAN_USE));
        _canUseCheck->blockSignals(false);
    }
    
    if (_canUseOnCheck) {
        _canUseOnCheck->blockSignals(true);
        _canUseOnCheck->setChecked(flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::CAN_USE_ON));
        _canUseOnCheck->blockSignals(false);
    }
    
    if (_generalFlagCheck) {
        _generalFlagCheck->blockSignals(true);
        _generalFlagCheck->setChecked(flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::GENERAL_FLAG));
        _generalFlagCheck->blockSignals(false);
    }
    
    if (_interactionFlagCheck) {
        _interactionFlagCheck->blockSignals(true);
        _interactionFlagCheck->setChecked(flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::INTERACTION_FLAG));
        _interactionFlagCheck->blockSignals(false);
    }
    
    if (_itemHiddenCheck) {
        _itemHiddenCheck->blockSignals(true);
        _itemHiddenCheck->setChecked(flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::ITEM_HIDDEN));
        _itemHiddenCheck->blockSignals(false);
    }
    
    if (_lightFlag1Check) {
        _lightFlag1Check->blockSignals(true);
        _lightFlag1Check->setChecked(flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_1));
        _lightFlag1Check->blockSignals(false);
    }
    
    if (_lightFlag2Check) {
        _lightFlag2Check->blockSignals(true);
        _lightFlag2Check->setChecked(flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_2));
        _lightFlag2Check->blockSignals(false);
    }
    
    if (_lightFlag3Check) {
        _lightFlag3Check->blockSignals(true);
        _lightFlag3Check->setChecked(flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_3));
        _lightFlag3Check->blockSignals(false);
    }
    
    if (_lightFlag4Check) {
        _lightFlag4Check->blockSignals(true);
        _lightFlag4Check->setChecked(flags & static_cast<uint32_t>(Pro::EXTENDED_FLAGS::LIGHT_FLAG_4));
        _lightFlag4Check->blockSignals(false);
    }
    
    // Store in pro data (different location based on object type)
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        _pro->commonItemData.flagsExt = flags;
    } else {
        // For critters, scenery, walls - store in header flags
        _pro->header.flags = (_pro->header.flags & 0x0FFFFFFF) | (flags & 0xF0000000); // Keep lower bits, update upper bits
    }
    
}

// Copy button methods removed - functionality deemed unnecessary

void ProEditorDialog::onPreviewViewChanged() {
    // No longer used - items use static dual preview without animation controls
}

void ProEditorDialog::updateValidationPanel() {
    if (!_validationList || !_validationStatusLabel || !_validationToggleButton) {
        return;
    }
    
    // Clear existing items
    _validationList->clear();
    
    // Count issues by level
    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;
    
    // Add items to list
    for (const auto& issue : _validationIssues) {
        QListWidgetItem* item = new QListWidgetItem();
        
        QString prefix;
        QString color;
        switch (issue.level) {
            case ValidationLevel::ERROR:
                prefix = "🔴";
                color = "red";
                errorCount++;
                break;
            case ValidationLevel::WARNING:
                prefix = "🟠";
                color = "orange";
                warningCount++;
                break;
            case ValidationLevel::INFO:
                prefix = "🔵";
                color = "blue";
                infoCount++;
                break;
        }
        
        QString text = QString("%1 [%2] %3").arg(prefix).arg(issue.category).arg(issue.message);
        item->setText(text);
        item->setForeground(QColor(color));
        item->setData(Qt::UserRole, QVariant::fromValue(issue.field));
        
        _validationList->addItem(item);
    }
    
    // Update status label
    if (errorCount > 0) {
        _validationStatusLabel->setText(QString("%1 error%2, %3 warning%4")
            .arg(errorCount).arg(errorCount > 1 ? "s" : "")
            .arg(warningCount).arg(warningCount > 1 ? "s" : ""));
        _validationStatusLabel->setStyleSheet("color: red; font-weight: bold;");
    } else if (warningCount > 0) {
        _validationStatusLabel->setText(QString("%1 warning%2, %3 info")
            .arg(warningCount).arg(warningCount > 1 ? "s" : "")
            .arg(infoCount));
        _validationStatusLabel->setStyleSheet("color: orange; font-weight: bold;");
    } else if (infoCount > 0) {
        _validationStatusLabel->setText(QString("%1 suggestion%2")
            .arg(infoCount).arg(infoCount > 1 ? "s" : ""));
        _validationStatusLabel->setStyleSheet("color: blue; font-weight: bold;");
    } else {
        _validationStatusLabel->setText("No issues");
        _validationStatusLabel->setStyleSheet("color: green; font-weight: bold;");
    }
    
    // Update button text with count
    QString buttonText = _validationPanelVisible ? "▼" : "▶";
    if (errorCount + warningCount + infoCount > 0) {
        buttonText += QString(" Validation Issues (%1)").arg(errorCount + warningCount + infoCount);
    } else {
        buttonText += " Validation Issues";
    }
    _validationToggleButton->setText(buttonText);
}

int ProEditorDialog::calculateArmorAIPriority() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM || 
        static_cast<Pro::ITEM_TYPE>(_pro->objectSubtypeId()) != Pro::ITEM_TYPE::ARMOR) {
        return 0;
    }
    
    int priority = 0;
    const auto& armorData = _pro->armorData;
    
    // Add armor class
    priority += static_cast<int>(armorData.armorClass);
    
    // Add all damage threshold values (7 damage types)
    for (int i = 0; i < 7; ++i) {
        priority += static_cast<int>(armorData.damageThreshold[i]);
    }
    
    // Add all damage resistance values (7 damage types)
    for (int i = 0; i < 7; ++i) {
        priority += static_cast<int>(armorData.damageResist[i]);
    }
    
    return priority;
}

int ProEditorDialog::calculateWeaponAIPriority() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM || 
        static_cast<Pro::ITEM_TYPE>(_pro->objectSubtypeId()) != Pro::ITEM_TYPE::WEAPON) {
        return 0;
    }
    
    int priority = 0;
    const auto& weaponData = _pro->weaponData;
    
    // Add average damage
    int avgDamage = (static_cast<int>(weaponData.damageMin) + static_cast<int>(weaponData.damageMax)) / 2;
    priority += avgDamage * 2;  // Weight damage heavily
    
    // Add range factors
    priority += static_cast<int>(weaponData.rangePrimary) / 2;
    priority += static_cast<int>(weaponData.rangeSecondary) / 4;
    
    // Subtract action point costs (lower AP cost = higher priority)
    priority -= static_cast<int>(weaponData.actionCostPrimary);
    priority -= static_cast<int>(weaponData.actionCostSecondary) / 2;
    
    // Add burst rounds bonus
    if (weaponData.burstRounds > 1) {
        priority += static_cast<int>(weaponData.burstRounds) * 3;
    }
    
    return std::max(0, priority);  // Ensure non-negative
}

void ProEditorDialog::updateAIPriorityDisplays() {
    if (!_pro) return;
    
    // Update armor AI priority (only if label exists)
    if (_armorAIPriorityLabel && _pro->type() == Pro::OBJECT_TYPE::ITEM && 
        static_cast<Pro::ITEM_TYPE>(_pro->objectSubtypeId()) == Pro::ITEM_TYPE::ARMOR) {
        int armorPriority = calculateArmorAIPriority();
        _armorAIPriorityLabel->setText(QString::number(armorPriority));
    }
    
    // Update weapon AI priority (only if label exists)
    if (_weaponAIPriorityLabel && _pro->type() == Pro::OBJECT_TYPE::ITEM && 
        static_cast<Pro::ITEM_TYPE>(_pro->objectSubtypeId()) == Pro::ITEM_TYPE::WEAPON) {
        int weaponPriority = calculateWeaponAIPriority();
        _weaponAIPriorityLabel->setText(QString::number(weaponPriority));
    }
}

} // namespace geck