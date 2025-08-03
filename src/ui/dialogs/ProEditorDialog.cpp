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
#include "../../util/ResourcePaths.h"
#include "FrmSelectorDialog.h"

#include <util/ProHelper.h>

namespace geck {

ProEditorDialog::ProEditorDialog(std::shared_ptr<Pro> pro, QWidget* parent)
    : QDialog(parent)
    , _pro(pro)
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
    , _armorPreviewGroup(nullptr)
    , _armorMalePreviewLabel(nullptr)
    , _armorFemalePreviewLabel(nullptr)
    , _armorAnimationControls(nullptr)
    , _armorAnimationLayout(nullptr)
    , _armorPlayPauseButton(nullptr)
    , _armorFrameSlider(nullptr)
    , _armorFrameLabel(nullptr)
    , _armorDirectionCombo(nullptr)
    , _armorAnimationTimer(nullptr)
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
    , _armorCurrentFrame(0)
    , _armorCurrentDirection(0)
    , _armorTotalFrames(0)
    , _armorTotalDirections(0)
    , _armorIsAnimating(false)
    , _nameLabel(nullptr)
    , _descriptionLabel(nullptr)
    , _fidSelectorButton(nullptr)
    , _inventoryFIDSelectorButton(nullptr)
    , _armorMaleFIDSelectorButton(nullptr)
    , _armorFemaleFIDSelectorButton(nullptr)
    , _inventoryFIDLabel(nullptr)
    , _armorMaleFIDLabel(nullptr)
    , _armorFemaleFIDLabel(nullptr)
    , _critterHeadFIDLabel(nullptr)
    , _critterHeadFIDSelectorButton(nullptr)
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
    , _hiddenCheck(nullptr)
    , _noSaveCheck(nullptr)
    , _lightingCheck(nullptr)
    , _noRemoveCheck(nullptr)
    , _queuedCheck(nullptr)
    , _leftHandCheck(nullptr)
    , _rightHandCheck(nullptr)
    , _wornCheck(nullptr)
    , _seenCheck(nullptr)
    , _sidEdit(nullptr)
    , _materialIdEdit(nullptr)
    , _containerSizeEdit(nullptr)
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
    resize(950, 650); // True 3-column layout with preview
    
    
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
    
    // Initialize critter UI element arrays to nullptr
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
    
    // Initialize individual critter UI pointers to nullptr
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
    _critterFlatCheck = nullptr;
    _critterSpecialDeathCheck = nullptr;
    _critterLongLimbsCheck = nullptr;
    _critterNoKnockbackCheck = nullptr;
    
    // Initialize all item UI elements to nullptr to prevent crashes
    // Armor elements
    _armorClassEdit = nullptr;
    for (int i = 0; i < NUM_DAMAGE_TYPES; ++i) {
        _damageResistEdits[i] = nullptr;
        _damageThresholdEdits[i] = nullptr;
    }
    _armorPerkCombo = nullptr;
    _armorMaleFIDLabel = nullptr;
    _armorFemaleFIDLabel = nullptr;
    _armorAIPriorityLabel = nullptr;
    
    // Container elements
    _containerMaxSizeEdit = nullptr;
    for (int i = 0; i < 5; ++i) {
        _containerFlagChecks[i] = nullptr;
    }
    
    // Drug elements
    for (int i = 0; i < NUM_DRUG_STATS; ++i) {
        _drugStatCombos[i] = nullptr;
        _drugStatAmountEdits[i] = nullptr;
        _drugFirstStatAmountEdits[i] = nullptr;
        _drugSecondStatAmountEdits[i] = nullptr;
    }
    _drugFirstDelayEdit = nullptr;
    _drugSecondDelayEdit = nullptr;
    _drugAddictionChanceEdit = nullptr;
    _drugAddictionPerkCombo = nullptr;
    _drugAddictionDelayEdit = nullptr;
    
    // Weapon elements
    _weaponAnimationCombo = nullptr;
    _weaponDamageMinEdit = nullptr;
    _weaponDamageMaxEdit = nullptr;
    _weaponDamageTypeCombo = nullptr;
    _weaponRangePrimaryEdit = nullptr;
    _weaponRangeSecondaryEdit = nullptr;
    _weaponProjectilePIDEdit = nullptr;
    _weaponMinStrengthEdit = nullptr;
    _weaponAPPrimaryEdit = nullptr;
    _weaponAPSecondaryEdit = nullptr;
    _weaponCriticalFailEdit = nullptr;
    _weaponPerkCombo = nullptr;
    _weaponBurstRoundsEdit = nullptr;
    _weaponAmmoTypeCombo = nullptr;
    _weaponAmmoPIDEdit = nullptr;
    _weaponAmmoCapacityEdit = nullptr;
    _weaponSoundIdEdit = nullptr;
    _weaponEnergyWeaponCheck = nullptr;
    _weaponAIPriorityLabel = nullptr;
    
    // Ammo elements
    _ammoCaliberCombo = nullptr;
    _ammoQuantityEdit = nullptr;
    _ammoDamageModEdit = nullptr;
    _ammoDRModEdit = nullptr;
    _ammoDamageMultEdit = nullptr;
    _ammoDamageTypeModCombo = nullptr;
    
    // Misc elements
    _miscPowerTypeCombo = nullptr;
    _miscChargesEdit = nullptr;
    
    // Key elements
    _keyIdEdit = nullptr;
    
    // Extended flags and common elements
    _extendedFlagsGroup = nullptr;
    _animationPrimaryEdit = nullptr;
    _animationSecondaryEdit = nullptr;
    _bigGunCheck = nullptr;
    _twoHandedCheck = nullptr;
    _canUseCheck = nullptr;
    _canUseOnCheck = nullptr;
    _generalFlagCheck = nullptr;
    _interactionFlagCheck = nullptr;
    _itemHiddenCheck = nullptr;
    _lightFlag1Check = nullptr;
    _lightFlag2Check = nullptr;
    _lightFlag3Check = nullptr;
    _lightFlag4Check = nullptr;
    _sidEdit = nullptr;
    _materialIdEdit = nullptr;
    _containerSizeEdit = nullptr;
    _weightEdit = nullptr;
    _basePriceEdit = nullptr;
    _inventoryFIDLabel = nullptr;
    _inventoryFIDSelectorButton = nullptr;
    _soundIdEdit = nullptr;
    
    // Load stat and perk names from MSG files BEFORE setting up UI (needed for drug fields)
    loadStatAndPerkNames();
    
    setupUI();
    
    loadProData();
    
    
    // Load name and description from MSG files
    loadNameAndDescription();
    
    // NOTE: updateTabVisibility() is already called from setupUI() -> setupTabContent()
    // Call updatePreview after a brief delay to ensure all widgets are fully initialized
    QTimer::singleShot(0, this, &ProEditorDialog::updatePreview);
    // Update AI priority displays
    updateAIPriorityDisplays();
}

void ProEditorDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    
    // Create horizontal layout: Left Info Panel | Right Type-Specific Fields
    _contentLayout = new QHBoxLayout();
    
    // === LEFT PANEL: Image + Name + Description + Common Fields ===
    QWidget* leftInfoPanel = new QWidget();
    leftInfoPanel->setFixedWidth(320);  // Fixed width for consistent layout
    QVBoxLayout* leftInfoLayout = new QVBoxLayout(leftInfoPanel);
    leftInfoLayout->setContentsMargins(8, 8, 8, 8);
    leftInfoLayout->setSpacing(6);
    
    // Setup compact preview at top
    setupCompactPreview(leftInfoLayout);
    
    // Name (bold)
    _nameLabel = new QLabel("Loading...");
    _nameLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 14px; color: #2c5282; padding: 4px 0; }");
    _nameLabel->setWordWrap(true);
    _nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    leftInfoLayout->addWidget(_nameLabel);
    
    // Description (compact)
    _descriptionLabel = new QTextEdit("Loading...");
    _descriptionLabel->setStyleSheet("QTextEdit { background-color: #f7fafc; border: 1px solid #e2e8f0; font-size: 12px; }");
    _descriptionLabel->setReadOnly(true);
    _descriptionLabel->setFixedHeight(60);
    _descriptionLabel->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _descriptionLabel->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    leftInfoLayout->addWidget(_descriptionLabel);
    
    // Common fields section
    setupLeftPanelCommonFields(leftInfoLayout);
    
    leftInfoLayout->addStretch(); // Push everything to top
    
    // === RIGHT PANEL: Two-Column Type-Specific Fields ===
    QWidget* rightFieldsPanel = new QWidget();
    QHBoxLayout* rightMainLayout = new QHBoxLayout(rightFieldsPanel);
    rightMainLayout->setContentsMargins(8, 8, 8, 8);
    rightMainLayout->setSpacing(12);
    
    // Column 1: Left side of type-specific fields
    QWidget* rightColumn1 = new QWidget();
    QVBoxLayout* rightColumn1Layout = new QVBoxLayout(rightColumn1);
    rightColumn1Layout->setContentsMargins(0, 0, 0, 0);
    rightColumn1Layout->setSpacing(8);
    
    // Column 2: Right side of type-specific fields  
    QWidget* rightColumn2 = new QWidget();
    QVBoxLayout* rightColumn2Layout = new QVBoxLayout(rightColumn2);
    rightColumn2Layout->setContentsMargins(0, 0, 0, 0);
    rightColumn2Layout->setSpacing(8);
    
    // Add both columns to right panel with equal width
    rightMainLayout->addWidget(rightColumn1, 1);
    rightMainLayout->addWidget(rightColumn2, 1);
    
    // Store references for type-specific field setup
    _leftFieldsLayout = rightColumn1Layout;
    _rightFieldsLayout = rightColumn2Layout;
    
    // Add main panels to content layout
    _contentLayout->addWidget(leftInfoPanel, 0); // Fixed width
    _contentLayout->addWidget(rightFieldsPanel, 1); // Flexible width
    
    // Setup type-specific content in the right columns
    setupTabContent();
    
    // Button box
    _buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(_buttonBox, &QDialogButtonBox::accepted, this, &ProEditorDialog::onAccept);
    connect(_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    _mainLayout->addLayout(_contentLayout);
    
    
    _mainLayout->addWidget(_buttonBox);
}

void ProEditorDialog::setupCompactPreview(QVBoxLayout* parentLayout) {
    // Create compact preview group
    QWidget* previewGroup = new QWidget();

    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(4);

    // Check if we need specialized previews for items
    bool hasSpecializedPreview = (_pro && _pro->type() == Pro::OBJECT_TYPE::ITEM);
    
    // Only show main preview image if no specialized previews
    if (!hasSpecializedPreview) {
        // Use ObjectPreviewWidget for non-item objects (all with full animation controls)
        _objectPreviewWidget = new ObjectPreviewWidget();
        
        // Connect signals
        connect(_objectPreviewWidget, &ObjectPreviewWidget::fidChangeRequested, 
                this, &ProEditorDialog::onObjectFidChangeRequested);
        connect(_objectPreviewWidget, &ObjectPreviewWidget::fidChanged, 
                this, &ProEditorDialog::onObjectFidChanged);
        
        previewLayout->addWidget(_objectPreviewWidget);
    }
    
    // Handle dual preview for items (inventory/ground) if needed
    if (hasSpecializedPreview) {
        setupDualPreviewCompact(previewLayout);
    }

    parentLayout->addWidget(previewGroup);
}

void ProEditorDialog::setupCompactAnimationControls(QVBoxLayout* parentLayout) {
    _animationControls = new QWidget();
    QHBoxLayout* animLayout = new QHBoxLayout(_animationControls);
    animLayout->setContentsMargins(0, 0, 0, 0);
    animLayout->setSpacing(4);
    
    // Play/Pause button (small)
    _playPauseButton = new QPushButton("▶");
    _playPauseButton->setMaximumSize(BUTTON_MAX_WIDTH, BUTTON_MAX_HEIGHT);
    _playPauseButton->setToolTip("Play/Pause animation");
    connect(_playPauseButton, &QPushButton::clicked, this, &ProEditorDialog::onPlayPauseClicked);
    
    // Frame slider (compact)
    _frameSlider = new QSlider(Qt::Horizontal);
    _frameSlider->setMinimum(0);
    _frameSlider->setMaximum(0);
    _frameSlider->setValue(0);
    _frameSlider->setToolTip("Animation frame");
    connect(_frameSlider, &QSlider::valueChanged, this, &ProEditorDialog::onFrameChanged);
    
    // Frame label (small)
    _frameLabel = new QLabel("0/0");
    _frameLabel->setFixedWidth(30);
    _frameLabel->setStyleSheet("QLabel { font-size: 10px; }");
    
    // Direction combo (compact)
    _directionCombo = new QComboBox();
    _directionCombo->setMaximumWidth(DIRECTION_COMBO_MAX_WIDTH); // Slightly wider for compass strings
    _directionCombo->setToolTip("Animation direction");
    _directionCombo->addItems({"NE", "E", "SE", "SW", "W", "NW"}); // Fallout 2 uses 6 directions
    connect(_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onDirectionChanged);
    
    animLayout->addWidget(_playPauseButton);
    animLayout->addWidget(_frameSlider, 1);
    animLayout->addWidget(_frameLabel);
    animLayout->addWidget(_directionCombo);
    
    // Animation timer
    _animationTimer = new QTimer(this);
    _animationTimer->setInterval(ANIMATION_TIMER_INTERVAL); // 5 FPS
    connect(_animationTimer, &QTimer::timeout, this, &ProEditorDialog::onAnimationTick);
    
    _animationControls->setVisible(false); // Hidden by default, shown when FRM is loaded
    parentLayout->addWidget(_animationControls);
}

void ProEditorDialog::setupCompactArmorAnimationControls(QVBoxLayout* parentLayout) {
    // Check if armor animation controls are already initialized (from full preview setup)
    if (!_armorAnimationControls) {
        // Create armor animation controls with compact styling
        _armorAnimationControls = new QWidget();
        QHBoxLayout* animLayout = new QHBoxLayout(_armorAnimationControls);
        animLayout->setContentsMargins(0, 0, 0, 0);
        animLayout->setSpacing(4);
        
        // Play/Pause button (small)
        _armorPlayPauseButton = new QPushButton("▶");
        _armorPlayPauseButton->setMaximumSize(BUTTON_MAX_WIDTH, BUTTON_MAX_HEIGHT);
        _armorPlayPauseButton->setToolTip("Play/Pause armor animation");
        
        // Frame slider (compact)
        _armorFrameSlider = new QSlider(Qt::Horizontal);
        _armorFrameSlider->setMinimum(0);
        _armorFrameSlider->setMaximum(0);
        _armorFrameSlider->setValue(0);
        _armorFrameSlider->setToolTip("Armor animation frame");
        
        // Frame label (small)
        _armorFrameLabel = new QLabel("0/0");
        _armorFrameLabel->setFixedWidth(30);
        _armorFrameLabel->setStyleSheet("QLabel { font-size: 10px; }");
        
        // Direction combo (compact)
        _armorDirectionCombo = new QComboBox();
        _armorDirectionCombo->setMaximumWidth(DIRECTION_COMBO_MAX_WIDTH);
        _armorDirectionCombo->setToolTip("Armor animation direction");
        _armorDirectionCombo->addItems({"NE", "E", "SE", "SW", "W", "NW"}); // Fallout 2 uses 6 directions
        
        animLayout->addWidget(_armorPlayPauseButton);
        animLayout->addWidget(_armorFrameSlider, 1);
        animLayout->addWidget(_armorFrameLabel);
        animLayout->addWidget(_armorDirectionCombo);
        
        // Animation timer
        if (!_armorAnimationTimer) {
            _armorAnimationTimer = new QTimer(this);
            _armorAnimationTimer->setInterval(ANIMATION_TIMER_INTERVAL); // 5 FPS
        }
        
        // Connect signals
        connect(_armorPlayPauseButton, &QPushButton::clicked, this, &ProEditorDialog::onArmorPlayPauseClicked);
        connect(_armorFrameSlider, &QSlider::valueChanged, this, &ProEditorDialog::onArmorFrameChanged);
        connect(_armorDirectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onArmorDirectionChanged);
        connect(_armorAnimationTimer, &QTimer::timeout, this, &ProEditorDialog::onArmorAnimationTick);
    }
    
    _armorAnimationControls->setEnabled(false); // Disabled by default, enabled when FRM is loaded
    parentLayout->addWidget(_armorAnimationControls);
}

void ProEditorDialog::setupDualPreviewCompact(QVBoxLayout* parentLayout) {
    // Compact dual preview for inventory/ground using ObjectPreviewWidget
    QWidget* dualWidget = new QWidget();
    QHBoxLayout* dualLayout = new QHBoxLayout(dualWidget);
    dualLayout->setContentsMargins(0, 0, 0, 0);
    dualLayout->setSpacing(4);
    dualLayout->setAlignment(Qt::AlignCenter);
    
    // Inventory preview - no animation controls, just title below image
    _inventoryPreviewWidget = new ObjectPreviewWidget(this, 
        ObjectPreviewWidget::PreviewOptions(), 
        QSize(PREVIEW_ITEM_SIZE, PREVIEW_ITEM_SIZE));
    _inventoryPreviewWidget->setTitle("Inventory");
    
    // Ground preview - no animation controls, just title below image  
    _groundPreviewWidget = new ObjectPreviewWidget(this,
        ObjectPreviewWidget::PreviewOptions(),
        QSize(PREVIEW_ITEM_SIZE, PREVIEW_ITEM_SIZE));
    _groundPreviewWidget->setTitle("Ground");
    
    dualLayout->addWidget(_inventoryPreviewWidget);
    dualLayout->addWidget(_groundPreviewWidget);

    parentLayout->addWidget(dualWidget);
}

void ProEditorDialog::setupArmorPreviewCompact(QVBoxLayout* parentLayout) {
    // Compact armor preview for male/female with animation controls
    QGroupBox* armorGroup = new QGroupBox("Armor Views");
    QVBoxLayout* armorGroupLayout = new QVBoxLayout(armorGroup);
    armorGroupLayout->setContentsMargins(4, 4, 4, 4);
    armorGroupLayout->setSpacing(4);
    
    // Horizontal layout for male/female previews
    QWidget* previewsWidget = new QWidget();
    QHBoxLayout* previewsLayout = new QHBoxLayout(previewsWidget);
    previewsLayout->setContentsMargins(0, 0, 0, 0);
    previewsLayout->setSpacing(4);
    
    // Male preview (small)
    QWidget* maleWidget = new QWidget();
    QVBoxLayout* maleLayout = new QVBoxLayout(maleWidget);
    maleLayout->setContentsMargins(0, 0, 0, 0);
    maleLayout->setSpacing(2);
    
    _armorMalePreviewLabel = new QLabel();
    _armorMalePreviewLabel->setFixedSize(PREVIEW_ITEM_SIZE, PREVIEW_ITEM_SIZE);
    _armorMalePreviewLabel->setAlignment(Qt::AlignCenter);
    _armorMalePreviewLabel->setStyleSheet("QLabel { border: 1px solid #cbd5e0; background-color: #f7fafc; }");
    _armorMalePreviewLabel->setText("Male");
    _armorMalePreviewLabel->setScaledContents(false); // Preserve aspect ratio
    
    QLabel* maleLabel = new QLabel("Male");
    maleLabel->setAlignment(Qt::AlignCenter);
    maleLabel->setStyleSheet("QLabel { font-size: 10px; }");
    
    maleLayout->addWidget(_armorMalePreviewLabel);
    maleLayout->addWidget(maleLabel);
    
    // Female preview (small)
    QWidget* femaleWidget = new QWidget();
    QVBoxLayout* femaleLayout = new QVBoxLayout(femaleWidget);
    femaleLayout->setContentsMargins(0, 0, 0, 0);
    femaleLayout->setSpacing(2);
    
    _armorFemalePreviewLabel = new QLabel();
    _armorFemalePreviewLabel->setFixedSize(PREVIEW_ITEM_SIZE, PREVIEW_ITEM_SIZE);
    _armorFemalePreviewLabel->setAlignment(Qt::AlignCenter);
    _armorFemalePreviewLabel->setStyleSheet("QLabel { border: 1px solid #cbd5e0; background-color: #f7fafc; }");
    _armorFemalePreviewLabel->setText("Female");
    _armorFemalePreviewLabel->setScaledContents(false); // Preserve aspect ratio
    
    QLabel* femaleLabel = new QLabel("Female");
    femaleLabel->setAlignment(Qt::AlignCenter);
    femaleLabel->setStyleSheet("QLabel { font-size: 10px; }");
    
    femaleLayout->addWidget(_armorFemalePreviewLabel);
    femaleLayout->addWidget(femaleLabel);
    
    previewsLayout->addWidget(maleWidget);
    previewsLayout->addWidget(femaleWidget);
    
    // Add previews to main layout
    armorGroupLayout->addWidget(previewsWidget);
    
    // Add compact armor animation controls
    setupCompactArmorAnimationControls(armorGroupLayout);
    
    parentLayout->addWidget(armorGroup);
}

void ProEditorDialog::setupLeftPanelCommonFields(QVBoxLayout* parentLayout) {
    // === COMMON FIELDS IN LEFT PANEL ===
    QGroupBox* commonGroup = new QGroupBox("Properties");
    QFormLayout* commonLayout = new QFormLayout(commonGroup);
    commonLayout->setContentsMargins(8, 8, 8, 8);
    commonLayout->setSpacing(4);
    
    // PID (read-only display)
    QLabel* pidLabel = new QLabel(QString::number(_pro->header.PID));
    pidLabel->setStyleSheet("QLabel { background-color: #f7fafc; padding: 2px 4px; border: 1px solid #e2e8f0; font-family: monospace; }");
    pidLabel->setToolTip("Prototype ID (read-only)");
    commonLayout->addRow("PID:", pidLabel);
    
    // Message ID
    _messageIdEdit = createSpinBox(0, 999999, "Message ID for displaying object name and description");
    _messageIdEdit->setValue(_pro->header.message_id);
    commonLayout->addRow("Message ID:", _messageIdEdit);
    
    // Light properties (compact)
    _lightDistanceEdit = createSpinBox(0, MAX_LIGHT_DISTANCE, "Distance that light from this object reaches (0 = no light)");
    _lightDistanceEdit->setValue(_pro->header.light_distance);
    commonLayout->addRow("Light Dist:", _lightDistanceEdit);
    
    _lightIntensityEdit = createSpinBox(0, MAX_LIGHT_INTENSITY, "Brightness of the light (higher values = brighter)");
    _lightIntensityEdit->setValue(_pro->header.light_intensity);
    commonLayout->addRow("Light Int:", _lightIntensityEdit);
    
    parentLayout->addWidget(commonGroup);
    
    // === OBJECT FLAGS (COMPACT) ===
    QGroupBox* flagsGroup = new QGroupBox("Object Flags");
    QVBoxLayout* flagsLayout = new QVBoxLayout(flagsGroup);
    flagsLayout->setContentsMargins(8, 8, 8, 8);
    flagsLayout->setSpacing(2);
    
    // NOTE: Hex flags field removed - using user-friendly checkboxes only
    
    // Complete flags as checkboxes (organized in compact layout)
    uint32_t flags = _pro->header.flags;
    
    // Basic rendering flags
    _flatCheck = new QCheckBox("Flat");
    _flatCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_FLAT));
    _flatCheck->setToolTip("Rendered first, just after tiles");
    connect(_flatCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    
    _noBlockCheck = new QCheckBox("No Block");
    _noBlockCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_BLOCK));
    _noBlockCheck->setToolTip("Doesn't block the tile");
    connect(_noBlockCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    
    _multiHexCheck = new QCheckBox("Multi-Hex");
    _multiHexCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_MULTIHEX));
    _multiHexCheck->setToolTip("Object spans multiple hexes");
    connect(_multiHexCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    
    _noHighlightCheck = new QCheckBox("No Highlight");
    _noHighlightCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_HIGHLIGHT));
    _noHighlightCheck->setToolTip("Doesn't highlight border; used for containers");
    connect(_noHighlightCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    
    // Transparency flags
    _transRedCheck = new QCheckBox("Trans Red");
    _transRedCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_RED));
    _transRedCheck->setToolTip("Red transparency effect");
    connect(_transRedCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    
    _transNoneCheck = new QCheckBox("Opaque");
    _transNoneCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_NONE));
    _transNoneCheck->setToolTip("Opaque - not the default value");
    connect(_transNoneCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    
    _transWallCheck = new QCheckBox("Trans Wall");
    _transWallCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_WALL));
    _transWallCheck->setToolTip("Wall transparency effect");
    connect(_transWallCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    
    _transGlassCheck = new QCheckBox("Trans Glass");
    _transGlassCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_GLASS));
    _transGlassCheck->setToolTip("Glass transparency effect");
    connect(_transGlassCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    
    _transSteamCheck = new QCheckBox("Trans Steam");
    _transSteamCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_STEAM));
    _transSteamCheck->setToolTip("Steam transparency effect");
    connect(_transSteamCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    
    _transEnergyCheck = new QCheckBox("Trans Energy");
    _transEnergyCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_ENERGY));
    _transEnergyCheck->setToolTip("Energy transparency effect");
    connect(_transEnergyCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    
    // Advanced flags
    _wallTransEndCheck = new QCheckBox("Wall Trans End");
    _wallTransEndCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_WALL_TRANS_END));
    _wallTransEndCheck->setToolTip("Changes transparency egg logic");
    connect(_wallTransEndCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    
    _lightThruCheck = new QCheckBox("Light Thru");
    _lightThruCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_LIGHT_THRU));
    _lightThruCheck->setToolTip("Light passes through this object");
    connect(_lightThruCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    
    _shootThruCheck = new QCheckBox("Shoot Thru");
    _shootThruCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_SHOOT_THRU));
    _shootThruCheck->setToolTip("Projectiles pass through this object");
    connect(_shootThruCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    
    // Compact checkbox layout (3 columns for better organization)
    QWidget* checkboxWidget = new QWidget();
    QGridLayout* checkboxLayout = new QGridLayout(checkboxWidget);
    checkboxLayout->setContentsMargins(0, 0, 0, 0);
    checkboxLayout->setSpacing(2);
    
    // Column 1: Basic rendering
    checkboxLayout->addWidget(_flatCheck, 0, 0);
    checkboxLayout->addWidget(_noBlockCheck, 1, 0);
    checkboxLayout->addWidget(_multiHexCheck, 2, 0);
    checkboxLayout->addWidget(_noHighlightCheck, 3, 0);
    
    // Column 2: Transparency
    checkboxLayout->addWidget(_transRedCheck, 0, 1);
    checkboxLayout->addWidget(_transNoneCheck, 1, 1);
    checkboxLayout->addWidget(_transWallCheck, 2, 1);
    checkboxLayout->addWidget(_transGlassCheck, 3, 1);
    
    // Column 3: Advanced
    checkboxLayout->addWidget(_transSteamCheck, 0, 2);
    checkboxLayout->addWidget(_transEnergyCheck, 1, 2);
    checkboxLayout->addWidget(_wallTransEndCheck, 2, 2);
    checkboxLayout->addWidget(_lightThruCheck, 3, 2);
    
    // Shoot thru spans all columns at bottom
    checkboxLayout->addWidget(_shootThruCheck, 4, 0, 1, 3);
    
    flagsLayout->addWidget(checkboxWidget);
    parentLayout->addWidget(flagsGroup);
}

void ProEditorDialog::setupTabContent() {
    // No longer setup common fields here - they're in left panel
    // Only setup type-specific fields in right columns
    updateTabVisibility();
}

void ProEditorDialog::setupCommonFields() {
    // This method is now obsolete - common fields are handled in setupLeftPanelCommonFields()
    // Left for compatibility with existing code structure
}

void ProEditorDialog::setupTabs() {
    _tabWidget = new QTabWidget(this);
    
    setupCommonTab();
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
    _descriptionLabel->setStyleSheet("QTextEdit { background-color: #f0f8ff; padding: 4px; border: 1px solid #add8e6; font-size: 12px; }");
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
    
    // Light Distance
    _lightDistanceEdit = createSpinBox(0, MAX_LIGHT_DISTANCE, "Distance that light from this object reaches (0 = no light)");
    _lightDistanceEdit->setValue(_pro->header.light_distance);
    layout->addRow("Light Distance:", _lightDistanceEdit);
    
    // Light Intensity
    _lightIntensityEdit = createSpinBox(0, MAX_LIGHT_INTENSITY, "Brightness of the light (higher values = brighter)");
    _lightIntensityEdit->setValue(_pro->header.light_intensity);
    layout->addRow("Light Intensity:", _lightIntensityEdit);
    
    // Object Flags (organized checkboxes instead of hex)
    setupObjectFlagsGroup(layout);
    
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
        _materialIdEdit = createMaterialComboBox("Material type - determines material properties");
        _materialIdEdit->setCurrentIndex(_pro->commonItemData.materialId);
        connect(_materialIdEdit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
        layout->addRow("Material:", _materialIdEdit);
        
        // Container Size
        _containerSizeEdit = new QSpinBox();
        _containerSizeEdit->setRange(0, MAX_CONTAINER_SIZE);
        _containerSizeEdit->setValue(_pro->commonItemData.containerSize);
        _containerSizeEdit->setToolTip("Container size - volume capacity for containers");
        connect(_containerSizeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        layout->addRow("Container Size:", _containerSizeEdit);
        // Weight
        _weightEdit = new QSpinBox();
        _weightEdit->setRange(0, MAX_WEIGHT);
        _weightEdit->setValue(_pro->commonItemData.weight);
        _weightEdit->setToolTip("Weight in pounds - affects carry capacity");
        connect(_weightEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        layout->addRow("Weight:", _weightEdit);
        
        // Base Price
        _basePriceEdit = new QSpinBox();
        _basePriceEdit->setRange(0, MAX_PRICE);
        _basePriceEdit->setValue(_pro->commonItemData.basePrice);
        _basePriceEdit->setToolTip("Base price in caps for trading");
        connect(_basePriceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        layout->addRow("Base Price:", _basePriceEdit);
        
        // Sound ID
        _soundIdEdit = new QSpinBox();
        _soundIdEdit->setRange(0, MAX_SOUND_ID);
        _soundIdEdit->setValue(_pro->commonItemData.soundId);
        _soundIdEdit->setToolTip("Sound effect ID when using/manipulating the item");
        connect(_soundIdEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        layout->addRow("Sound ID:", _soundIdEdit);
    }
    
    // Add wall-specific fields (only for walls)
    if (_pro->type() == Pro::OBJECT_TYPE::WALL) {
        _wallMaterialIdEdit = createMaterialComboBox("Material type - determines wall material properties and interactions");
        _wallMaterialIdEdit->setCurrentIndex(_pro->wallData.materialId);
        connect(_wallMaterialIdEdit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
        layout->addRow("Material:", _wallMaterialIdEdit);
    }
    
    // Add tile-specific fields (only for tiles)
    if (_pro->type() == Pro::OBJECT_TYPE::TILE) {
        _tileMaterialIdEdit = createMaterialComboBox("Material type - determines tile material properties and interactions");
        _tileMaterialIdEdit->setCurrentIndex(_pro->tileData.materialId);
        connect(_tileMaterialIdEdit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
        layout->addRow("Material:", _tileMaterialIdEdit);
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
    
    layout->addRow("Extended Flags:", _extendedFlagsGroup);
}

void ProEditorDialog::setupWeaponExtendedFlags(QVBoxLayout* layout) {
    // Animation flags group
    QGroupBox* animGroup = new QGroupBox("Animation");
    QFormLayout* animLayout = new QFormLayout(animGroup);
    
    _animationPrimaryEdit = new QSpinBox();
    _animationPrimaryEdit->setRange(0, MAX_ANIMATION_CODE);
    _animationPrimaryEdit->setValue(Pro::getAnimationPrimary(_pro->commonItemData.flagsExt));
    _animationPrimaryEdit->setToolTip("Primary attack animation index (0-15)");
    connect(_animationPrimaryEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onExtendedFlagChanged);
    animLayout->addRow("Primary Anim:", _animationPrimaryEdit);
    
    _animationSecondaryEdit = new QSpinBox();
    _animationSecondaryEdit->setRange(0, MAX_ANIMATION_CODE);
    _animationSecondaryEdit->setValue(Pro::getAnimationSecondary(_pro->commonItemData.flagsExt));
    _animationSecondaryEdit->setToolTip("Secondary attack animation index (0-15)");
    connect(_animationSecondaryEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onExtendedFlagChanged);
    animLayout->addRow("Secondary Anim:", _animationSecondaryEdit);
    
    layout->addWidget(animGroup);
    
    // Weapon behavior flags
    QGroupBox* behaviorGroup = new QGroupBox("Weapon Behavior");
    QVBoxLayout* behaviorLayout = new QVBoxLayout(behaviorGroup);
    
    _bigGunCheck = new QCheckBox("Big Gun");
    _bigGunCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::BIG_GUN));
    _bigGunCheck->setToolTip("Forces weapon to use Big Guns skill instead of Small Guns");
    connect(_bigGunCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    behaviorLayout->addWidget(_bigGunCheck);
    
    _twoHandedCheck = new QCheckBox("Two-Handed");
    _twoHandedCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::TWO_HANDED));
    _twoHandedCheck->setToolTip("Weapon requires both hands, prevents dual-wielding");
    connect(_twoHandedCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    behaviorLayout->addWidget(_twoHandedCheck);
    
    _itemHiddenCheck = new QCheckBox("Hidden Item");
    _itemHiddenCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::ITEM_HIDDEN));
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
    _canUseCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::CAN_USE));
    _canUseCheck->setToolTip("Container can be 'used' (automatically set for containers)");
    connect(_canUseCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    actionLayout->addWidget(_canUseCheck);
    
    _canUseOnCheck = new QCheckBox("Can Use On");
    _canUseOnCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::CAN_USE_ON));
    _canUseOnCheck->setToolTip("Container can be 'used on' target");
    connect(_canUseOnCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    actionLayout->addWidget(_canUseOnCheck);
    
    _interactionFlagCheck = new QCheckBox("Interaction Flag");
    _interactionFlagCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::INTERACTION_FLAG));
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
    _canUseCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::CAN_USE));
    _canUseCheck->setToolTip("Item can be 'used'");
    connect(_canUseCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    generalLayout->addWidget(_canUseCheck);
    
    _canUseOnCheck = new QCheckBox("Can Use On");
    _canUseOnCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::CAN_USE_ON));
    _canUseOnCheck->setToolTip("Item can be 'used on' target (automatically set for drugs)");
    connect(_canUseOnCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    generalLayout->addWidget(_canUseOnCheck);
    
    _generalFlagCheck = new QCheckBox("General Flag");
    _generalFlagCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::GENERAL_FLAG));
    _generalFlagCheck->setToolTip("General purpose flag");
    connect(_generalFlagCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    generalLayout->addWidget(_generalFlagCheck);
    
    layout->addWidget(generalGroup);
}

void ProEditorDialog::setupOtherExtendedFlags(QVBoxLayout* layout) {
    // For critters, scenery, walls - show general flags
    _generalFlagCheck = new QCheckBox("General Flag");
    _generalFlagCheck->setChecked(_pro->type() == Pro::OBJECT_TYPE::ITEM ? 
        Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::GENERAL_FLAG) :
        Pro::hasFlag(_pro->header.flags, Pro::ExtendedItemFlags::GENERAL_FLAG));
    _generalFlagCheck->setToolTip("General purpose flag (scenery/walls/tiles)");
    connect(_generalFlagCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    layout->addWidget(_generalFlagCheck);
    
    // Light flags for objects that can have light
    if (_pro->type() != Pro::OBJECT_TYPE::TILE) {
        QGroupBox* lightGroup = new QGroupBox("Light Flags");
        QVBoxLayout* lightLayout = new QVBoxLayout(lightGroup);
        
        uint32_t flagsToCheck = _pro->type() == Pro::OBJECT_TYPE::ITEM ? 
            _pro->commonItemData.flagsExt : _pro->header.flags;
        
        _lightFlag1Check = new QCheckBox("OBJECT_WALL_TRANS_END");
        _lightFlag1Check->setChecked(Pro::hasFlag(flagsToCheck, Pro::ObjectFlags::OBJECT_WALL_TRANS_END));
        _lightFlag1Check->setToolTip("OBJECT_WALL_TRANS_END");
        connect(_lightFlag1Check, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
        lightLayout->addWidget(_lightFlag1Check);
        
        _lightFlag2Check = new QCheckBox("OBJECT_LIGHT_THRU");
        _lightFlag2Check->setChecked(Pro::hasFlag(flagsToCheck, Pro::ObjectFlags::OBJECT_LIGHT_THRU));
        _lightFlag2Check->setToolTip("OBJECT_LIGHT_THRU");
        connect(_lightFlag2Check, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
        lightLayout->addWidget(_lightFlag2Check);
        
        _lightFlag3Check = new QCheckBox("OBJECT_SEEN");
        _lightFlag3Check->setChecked(Pro::hasFlag(flagsToCheck, Pro::ObjectFlags::OBJECT_SEEN));
        _lightFlag3Check->setToolTip("OBJECT_SEEN");
        connect(_lightFlag3Check, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
        lightLayout->addWidget(_lightFlag3Check);
        
        _lightFlag4Check = new QCheckBox("OBJECT_SHOOT_THRU");
        _lightFlag4Check->setChecked(Pro::hasFlag(flagsToCheck, Pro::ObjectFlags::OBJECT_SHOOT_THRU));
        _lightFlag4Check->setToolTip("OBJECT_SHOOT_THRU");
        connect(_lightFlag4Check, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
        lightLayout->addWidget(_lightFlag4Check);
        
        layout->addWidget(lightGroup);
    }
}

void ProEditorDialog::addStandardItemFlags(QVBoxLayout* parentLayout) {
    // Standard ExtendedItemFlags group for all item types
    QGroupBox* itemFlagsGroup = new QGroupBox("Item Flags");
    QVBoxLayout* itemFlagsLayout = new QVBoxLayout(itemFlagsGroup);
    
    _canUseCheck = new QCheckBox("Can Use");
    _canUseCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::CAN_USE));
    _canUseCheck->setToolTip("Item can be 'used' (right-click action)");
    connect(_canUseCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    itemFlagsLayout->addWidget(_canUseCheck);
    
    _canUseOnCheck = new QCheckBox("Can Use On");
    _canUseOnCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::CAN_USE_ON));
    _canUseOnCheck->setToolTip("Item can be 'used on' target (drag and drop on target)");
    connect(_canUseOnCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    itemFlagsLayout->addWidget(_canUseOnCheck);
    
    _generalFlagCheck = new QCheckBox("General Flag");
    _generalFlagCheck->setChecked(Pro::hasFlag(_pro->commonItemData.flagsExt, Pro::ExtendedItemFlags::GENERAL_FLAG));
    _generalFlagCheck->setToolTip("General purpose flag for special behaviors");
    connect(_generalFlagCheck, &QCheckBox::toggled, this, &ProEditorDialog::onExtendedFlagChanged);
    itemFlagsLayout->addWidget(_generalFlagCheck);
    
    parentLayout->addWidget(itemFlagsGroup);
}

void ProEditorDialog::setupObjectFlagsGroup(QFormLayout* layout) {
    QGroupBox* objectFlagsGroup = new QGroupBox("Object Flags");
    QVBoxLayout* flagsLayout = new QVBoxLayout(objectFlagsGroup);
    
    uint32_t flags = _pro->header.flags;
    
    // Rendering flags group
    QGroupBox* renderingGroup = new QGroupBox("Rendering");
    QVBoxLayout* renderingLayout = new QVBoxLayout(renderingGroup);
    
    _flatCheck = new QCheckBox("Flat");
    _flatCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_FLAT));
    _flatCheck->setToolTip("Rendered first, just after tiles");
    connect(_flatCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    renderingLayout->addWidget(_flatCheck);
    
    _noBlockCheck = new QCheckBox("No Block");
    _noBlockCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_BLOCK));
    _noBlockCheck->setToolTip("Doesn't block the tile");
    connect(_noBlockCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    renderingLayout->addWidget(_noBlockCheck);
    
    _multiHexCheck = new QCheckBox("Multi-Hex");
    _multiHexCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_MULTIHEX));
    _multiHexCheck->setToolTip("Object occupies multiple hexes");
    connect(_multiHexCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    renderingLayout->addWidget(_multiHexCheck);
    
    _noHighlightCheck = new QCheckBox("No Highlight");
    _noHighlightCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_HIGHLIGHT));
    _noHighlightCheck->setToolTip("Doesn't highlight border; used for containers");
    connect(_noHighlightCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    renderingLayout->addWidget(_noHighlightCheck);
    
    flagsLayout->addWidget(renderingGroup);
    
    // Transparency flags group (mutually exclusive)
    QGroupBox* transparencyGroup = new QGroupBox("Transparency (Mutually Exclusive)");
    QVBoxLayout* transparencyLayout = new QVBoxLayout(transparencyGroup);
    
    _transNoneCheck = new QCheckBox("Opaque (TransNone)");
    _transNoneCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_NONE));
    _transNoneCheck->setToolTip("Opaque - not the default value");
    connect(_transNoneCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    transparencyLayout->addWidget(_transNoneCheck);
    
    _transRedCheck = new QCheckBox("Red Transparency");
    _transRedCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_RED));
    _transRedCheck->setToolTip("Red transparency effect");
    connect(_transRedCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    transparencyLayout->addWidget(_transRedCheck);
    
    _transWallCheck = new QCheckBox("Wall Transparency");
    _transWallCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_WALL));
    _transWallCheck->setToolTip("Wall transparency effect");
    connect(_transWallCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    transparencyLayout->addWidget(_transWallCheck);
    
    _transGlassCheck = new QCheckBox("Glass Transparency");
    _transGlassCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_GLASS));
    _transGlassCheck->setToolTip("Glass transparency effect");
    connect(_transGlassCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    transparencyLayout->addWidget(_transGlassCheck);
    
    _transSteamCheck = new QCheckBox("Steam Transparency");
    _transSteamCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_STEAM));
    _transSteamCheck->setToolTip("Steam transparency effect");
    connect(_transSteamCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    transparencyLayout->addWidget(_transSteamCheck);
    
    _transEnergyCheck = new QCheckBox("Energy Transparency");
    _transEnergyCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_ENERGY));
    _transEnergyCheck->setToolTip("Energy transparency effect");
    connect(_transEnergyCheck, &QCheckBox::toggled, this, &ProEditorDialog::onTransparencyFlagChanged);
    transparencyLayout->addWidget(_transEnergyCheck);
    
    flagsLayout->addWidget(transparencyGroup);
    
    // Interaction flags group
    QGroupBox* interactionGroup = new QGroupBox("Interaction");
    QVBoxLayout* interactionLayout = new QVBoxLayout(interactionGroup);
    
    _wallTransEndCheck = new QCheckBox("Wall Trans End");
    _wallTransEndCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_WALL_TRANS_END));
    _wallTransEndCheck->setToolTip("Changes transparency egg logic");
    connect(_wallTransEndCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    interactionLayout->addWidget(_wallTransEndCheck);
    
    _lightThruCheck = new QCheckBox("Light Thru");
    _lightThruCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_LIGHT_THRU));
    _lightThruCheck->setToolTip("Light passes through");
    connect(_lightThruCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    interactionLayout->addWidget(_lightThruCheck);
    
    _shootThruCheck = new QCheckBox("Shoot Thru");
    _shootThruCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_SHOOT_THRU));
    _shootThruCheck->setToolTip("Projectiles pass through");
    connect(_shootThruCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    interactionLayout->addWidget(_shootThruCheck);
    
    flagsLayout->addWidget(interactionGroup);
    
    // System flags group
    QGroupBox* systemGroup = new QGroupBox("System Flags");
    QVBoxLayout* systemLayout = new QVBoxLayout(systemGroup);
    
    _hiddenCheck = new QCheckBox("Hidden");
    _hiddenCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_HIDDEN));
    _hiddenCheck->setToolTip("Object is hidden from view");
    connect(_hiddenCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    systemLayout->addWidget(_hiddenCheck);
    
    _noSaveCheck = new QCheckBox("No Save");
    _noSaveCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_SAVE));
    _noSaveCheck->setToolTip("Should not be saved to savegame file");
    connect(_noSaveCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    systemLayout->addWidget(_noSaveCheck);
    
    _noRemoveCheck = new QCheckBox("No Remove");
    _noRemoveCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_REMOVE));
    _noRemoveCheck->setToolTip("Should not be removed from game world");
    connect(_noRemoveCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    systemLayout->addWidget(_noRemoveCheck);
    
    _queuedCheck = new QCheckBox("Queued");
    _queuedCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_QUEUED));
    _queuedCheck->setToolTip("Set if there was/is any event for the object");
    connect(_queuedCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    systemLayout->addWidget(_queuedCheck);
    
    _seenCheck = new QCheckBox("Seen");
    _seenCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_SEEN));
    _seenCheck->setToolTip("Has been seen by the player");
    connect(_seenCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    systemLayout->addWidget(_seenCheck);
    
    flagsLayout->addWidget(systemGroup);
    
    // Lighting group
    QGroupBox* lightingGroup = new QGroupBox("Lighting");
    QVBoxLayout* lightingLayout = new QVBoxLayout(lightingGroup);
    
    _lightingCheck = new QCheckBox("Has Lighting");
    _lightingCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_LIGHTING));
    _lightingCheck->setToolTip("Object has lighting properties");
    connect(_lightingCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    lightingLayout->addWidget(_lightingCheck);
    
    flagsLayout->addWidget(lightingGroup);
    
    // Equipment flags group
    QGroupBox* equipmentGroup = new QGroupBox("Equipment State");
    QVBoxLayout* equipmentLayout = new QVBoxLayout(equipmentGroup);
    
    _leftHandCheck = new QCheckBox("In Left Hand");
    _leftHandCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_IN_LEFT_HAND));
    _leftHandCheck->setToolTip("Object is in left hand");
    connect(_leftHandCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    equipmentLayout->addWidget(_leftHandCheck);
    
    _rightHandCheck = new QCheckBox("In Right Hand");
    _rightHandCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_IN_RIGHT_HAND));
    _rightHandCheck->setToolTip("Object is in right hand");
    connect(_rightHandCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    equipmentLayout->addWidget(_rightHandCheck);
    
    _wornCheck = new QCheckBox("Worn");
    _wornCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_WORN));
    _wornCheck->setToolTip("Object is being worn");
    connect(_wornCheck, &QCheckBox::toggled, this, &ProEditorDialog::onObjectFlagChanged);
    equipmentLayout->addWidget(_wornCheck);
    
    flagsLayout->addWidget(equipmentGroup);
    
    layout->addRow("Object Flags:", objectFlagsGroup);
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
    
    _animationLayout->addSpacing(LAYOUT_SPACING);
    
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
    _animationTimer->setInterval(ANIMATION_TIMER_INTERVAL); // 5 FPS default
    
    // Connect signals
    connect(_playPauseButton, &QPushButton::clicked, this, &ProEditorDialog::onPlayPauseClicked);
    connect(_frameSlider, &QSlider::valueChanged, this, &ProEditorDialog::onFrameChanged);
    connect(_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onDirectionChanged);
    connect(_animationTimer, &QTimer::timeout, this, &ProEditorDialog::onAnimationTick);
    
    // Initially disable controls
    _animationControls->setEnabled(false);
}

void ProEditorDialog::setupArmorAnimationControls() {
    // Armor animation controls
    _armorAnimationControls = new QWidget();
    _armorAnimationLayout = new QHBoxLayout(_armorAnimationControls);
    _armorAnimationLayout->setContentsMargins(0, 0, 0, 0);
    
    // Direction selection
    _armorDirectionCombo = new QComboBox();
    _armorDirectionCombo->addItems({"NE", "E", "SE", "SW", "W", "NW"});
    _armorDirectionCombo->setToolTip("Select armor animation direction");
    _armorAnimationLayout->addWidget(new QLabel("Direction:"));
    _armorAnimationLayout->addWidget(_armorDirectionCombo);
    
    _armorAnimationLayout->addSpacing(LAYOUT_SPACING);
    
    // Play/pause button
    _armorPlayPauseButton = new QPushButton("▶");
    _armorPlayPauseButton->setMaximumWidth(30);
    _armorPlayPauseButton->setToolTip("Play/Pause armor animation");
    _armorAnimationLayout->addWidget(_armorPlayPauseButton);
    
    // Frame slider
    _armorFrameSlider = new QSlider(Qt::Horizontal);
    _armorFrameSlider->setMinimum(0);
    _armorFrameSlider->setMaximum(0);
    _armorFrameSlider->setToolTip("Select armor animation frame");
    _armorAnimationLayout->addWidget(_armorFrameSlider);
    
    // Frame label
    _armorFrameLabel = new QLabel("0/0");
    _armorFrameLabel->setMinimumWidth(40);
    _armorAnimationLayout->addWidget(_armorFrameLabel);
    
    // Setup armor animation timer
    _armorAnimationTimer = new QTimer(this);
    _armorAnimationTimer->setSingleShot(false);
    _armorAnimationTimer->setInterval(200); // 5 FPS default
    
    // Connect signals
    connect(_armorPlayPauseButton, &QPushButton::clicked, this, &ProEditorDialog::onArmorPlayPauseClicked);
    connect(_armorFrameSlider, &QSlider::valueChanged, this, &ProEditorDialog::onArmorFrameChanged);
    connect(_armorDirectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onArmorDirectionChanged);
    connect(_armorAnimationTimer, &QTimer::timeout, this, &ProEditorDialog::onArmorAnimationTick);
    
    // Initially disable controls
    _armorAnimationControls->setEnabled(false);
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
        _commonData.weight = _pro->commonItemData.weight;
        _commonData.basePrice = _pro->commonItemData.basePrice;
        _commonData.inventoryFID = _pro->commonItemData.inventoryFID;
        _commonData.soundId = _pro->commonItemData.soundId;
        
    } catch (const std::exception& e) {
        spdlog::error("ProEditorDialog::loadProData() - exception loading header/extended data: {}", e.what());
        throw;
    }
    
    try {
        // Update common UI controls
        
        if (_messageIdEdit) {
            _messageIdEdit->setValue(_commonData.message_id);
        }
        
        // Always set _mainFID from PRO data (needed for compact preview)
        _mainFID = _commonData.FID;
        if (_fidLabel) {
            _fidLabel->setText(getFrmFilename(_mainFID));
        }
        
        if (_lightDistanceEdit) {
            _lightDistanceEdit->setValue(_commonData.light_distance);
        }
        
        if (_lightIntensityEdit) {
            _lightIntensityEdit->setValue(_commonData.light_intensity);
        }
        
        // Always set _inventoryFID from PRO data (needed for compact preview)
        _inventoryFID = _commonData.inventoryFID;
        if (_inventoryFIDLabel) {
            _inventoryFIDLabel->setText(getFrmFilename(_inventoryFID));
        }
        
        // Update flag checkboxes based on loaded flags
        loadObjectFlags(_commonData.flags);
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
            loadWallData();
            break;
        case Pro::OBJECT_TYPE::TILE:
            loadTileData();
            break;
        case Pro::OBJECT_TYPE::MISC:
            // This type only has common data
            break;
    }
    
    // Update dual previews for items now that all data is loaded
    if (_pro && _pro->type() == Pro::OBJECT_TYPE::ITEM && _inventoryPreviewWidget && _groundPreviewWidget) {
        updateInventoryPreview();
        updateGroundPreview();
    }
}

void ProEditorDialog::loadArmorData() {
    if (_armorClassEdit) {
        _armorClassEdit->setValue(_pro->armorData.armorClass);
    }
    
    for (int i = 0; i < 7; ++i) {
        if (_damageResistEdits[i]) {
            _damageResistEdits[i]->setValue(_pro->armorData.damageResist[i]);
        }
        if (_damageThresholdEdits[i]) {
            _damageThresholdEdits[i]->setValue(_pro->armorData.damageThreshold[i]);
        }
    }
    
    if (_armorPerkCombo) {
        _armorPerkCombo->setCurrentIndex(_pro->armorData.perk);
    }
    
    _armorMaleFID = _pro->armorData.armorMaleFID;
    if (_armorMaleFIDLabel) {
        _armorMaleFIDLabel->setText(getFrmFilename(_armorMaleFID));
    }
    
    _armorFemaleFID = _pro->armorData.armorFemaleFID;
    if (_armorFemaleFIDLabel) {
        _armorFemaleFIDLabel->setText(getFrmFilename(_armorFemaleFID));
    }
    
    // Load armor animation frames and update preview
    loadArmorAnimationFrames();
}

void ProEditorDialog::loadContainerData() {
    if (_containerMaxSizeEdit) {
        _containerMaxSizeEdit->setValue(_pro->containerData.maxSize);
    }
    
    // Load container flags (bit flags)
    uint32_t flags = _pro->containerData.flags;
    for (int i = 0; i < 5; ++i) {
        if (_containerFlagChecks[i]) {
            _containerFlagChecks[i]->setChecked(flags & (1 << i));
        }
    }
}

void ProEditorDialog::loadDrugData() {
    // Load stat selections (0xFFFF means "None", map to index 0)
    if (_drugStatCombos[0]) {
        uint32_t stat = _pro->drugData.stat0;
        if (stat == 0xFFFF || stat == 0xFFFFFFFF) {
            _drugStatCombos[0]->setCurrentIndex(0);  // "None"
        } else {
            _drugStatCombos[0]->setCurrentIndex(stat + 1);  // Offset by 1 to account for "None" at index 0
        }
    }
    if (_drugStatCombos[1]) {
        uint32_t stat = _pro->drugData.stat1;
        if (stat == 0xFFFF || stat == 0xFFFFFFFF) {
            _drugStatCombos[1]->setCurrentIndex(0);  // "None"
        } else {
            _drugStatCombos[1]->setCurrentIndex(stat + 1);  // Offset by 1 to account for "None" at index 0
        }
    }
    if (_drugStatCombos[2]) {
        uint32_t stat = _pro->drugData.stat2;
        if (stat == 0xFFFF || stat == 0xFFFFFFFF) {
            _drugStatCombos[2]->setCurrentIndex(0);  // "None"
        } else {
            _drugStatCombos[2]->setCurrentIndex(stat + 1);  // Offset by 1 to account for "None" at index 0
        }
    }
    
    // Load immediate effect amounts
    if (_drugStatAmountEdits[0]) {
        _drugStatAmountEdits[0]->setValue(_pro->drugData.amount0);
    }
    if (_drugStatAmountEdits[1]) {
        _drugStatAmountEdits[1]->setValue(_pro->drugData.amount1);
    }
    if (_drugStatAmountEdits[2]) {
        _drugStatAmountEdits[2]->setValue(_pro->drugData.amount2);
    }
    
    // Load first delayed effect
    if (_drugFirstDelayEdit) {
        _drugFirstDelayEdit->setValue(_pro->drugData.duration1);
    }
    if (_drugFirstStatAmountEdits[0]) {
        _drugFirstStatAmountEdits[0]->setValue(_pro->drugData.amount0_1);
    }
    if (_drugFirstStatAmountEdits[1]) {
        _drugFirstStatAmountEdits[1]->setValue(_pro->drugData.amount1_1);
    }
    if (_drugFirstStatAmountEdits[2]) {
        _drugFirstStatAmountEdits[2]->setValue(_pro->drugData.amount2_1);
    }
    
    // Load second delayed effect
    if (_drugSecondDelayEdit) {
        _drugSecondDelayEdit->setValue(_pro->drugData.duration2);
    }
    if (_drugSecondStatAmountEdits[0]) {
        _drugSecondStatAmountEdits[0]->setValue(_pro->drugData.amount0_2);
    }
    if (_drugSecondStatAmountEdits[1]) {
        _drugSecondStatAmountEdits[1]->setValue(_pro->drugData.amount1_2);
    }
    if (_drugSecondStatAmountEdits[2]) {
        _drugSecondStatAmountEdits[2]->setValue(_pro->drugData.amount2_2);
    }
    
    // Load addiction data
    if (_drugAddictionChanceEdit) {
        _drugAddictionChanceEdit->setValue(_pro->drugData.addictionRate);
    }
    if (_drugAddictionPerkCombo) {
        _drugAddictionPerkCombo->setCurrentIndex(_pro->drugData.addictionEffect);
    }
    if (_drugAddictionDelayEdit) {
        _drugAddictionDelayEdit->setValue(_pro->drugData.addictionOnset);
    }
}

void ProEditorDialog::loadWeaponData() {
    if (_weaponAnimationCombo) {
        _weaponAnimationCombo->setCurrentIndex(_pro->weaponData.animationCode);
    }
    if (_weaponDamageMinEdit) {
        _weaponDamageMinEdit->setValue(_pro->weaponData.damageMin);
    }
    if (_weaponDamageMaxEdit) {
        _weaponDamageMaxEdit->setValue(_pro->weaponData.damageMax);
    }
    if (_weaponDamageTypeCombo) {
        _weaponDamageTypeCombo->setCurrentIndex(_pro->weaponData.damageType);
    }
    if (_weaponRangePrimaryEdit) {
        _weaponRangePrimaryEdit->setValue(_pro->weaponData.rangePrimary);
    }
    if (_weaponRangeSecondaryEdit) {
        _weaponRangeSecondaryEdit->setValue(_pro->weaponData.rangeSecondary);
    }
    if (_weaponProjectilePIDEdit) {
        _weaponProjectilePIDEdit->setValue(_pro->weaponData.projectilePID);
    }
    if (_weaponMinStrengthEdit) {
        _weaponMinStrengthEdit->setValue(_pro->weaponData.minimumStrength);
    }
    if (_weaponAPPrimaryEdit) {
        _weaponAPPrimaryEdit->setValue(_pro->weaponData.actionCostPrimary);
    }
    if (_weaponAPSecondaryEdit) {
        _weaponAPSecondaryEdit->setValue(_pro->weaponData.actionCostSecondary);
    }
    if (_weaponCriticalFailEdit) {
        _weaponCriticalFailEdit->setValue(_pro->weaponData.criticalFail);
    }
    if (_weaponPerkCombo) {
        _weaponPerkCombo->setCurrentIndex(_pro->weaponData.perk);
    }
    if (_weaponBurstRoundsEdit) {
        _weaponBurstRoundsEdit->setValue(_pro->weaponData.burstRounds);
    }
    if (_weaponAmmoTypeCombo) {
        _weaponAmmoTypeCombo->setCurrentIndex(_pro->weaponData.ammoType);
    }
    if (_weaponAmmoPIDEdit) {
        _weaponAmmoPIDEdit->setValue(_pro->weaponData.ammoPID);
    }
    if (_weaponAmmoCapacityEdit) {
        _weaponAmmoCapacityEdit->setValue(_pro->weaponData.ammoCapacity);
    }
    if (_weaponSoundIdEdit) {
        _weaponSoundIdEdit->setValue(_pro->weaponData.soundId);
    }
    
    // Load weapon flags
    if (_weaponEnergyWeaponCheck) {
        bool isEnergyWeapon = Pro::hasFlag(_pro->weaponData.weaponFlags, Pro::WEAPON_FLAGS::ENERGY_WEAPON);
        _weaponEnergyWeaponCheck->setChecked(isEnergyWeapon);
    }
    
}

void ProEditorDialog::loadAmmoData() {
    if (_ammoCaliberCombo) {
        _ammoCaliberCombo->setCurrentIndex(_pro->ammoData.caliber);
    }
    if (_ammoQuantityEdit) {
        _ammoQuantityEdit->setValue(_pro->ammoData.quantity);
    }
    if (_ammoDamageModEdit) {
        _ammoDamageModEdit->setValue(_pro->ammoData.damageModifier);
    }
    if (_ammoDRModEdit) {
        _ammoDRModEdit->setValue(_pro->ammoData.damageResistModifier);
    }
    if (_ammoDamageMultEdit) {
        _ammoDamageMultEdit->setValue(_pro->ammoData.damageMultiplier);
    }
    if (_ammoDamageTypeModCombo) {
        _ammoDamageTypeModCombo->setCurrentIndex(_pro->ammoData.damageTypeModifier);
    }
}

void ProEditorDialog::loadMiscData() {
    if (_miscPowerTypeCombo) {
        _miscPowerTypeCombo->setCurrentIndex(_pro->miscData.powerType);
    }
    if (_miscChargesEdit) {
        _miscChargesEdit->setValue(_pro->miscData.charges);
    }
}

void ProEditorDialog::loadKeyData() {
    if (_keyIdEdit) {
        _keyIdEdit->setValue(_pro->keyData.keyId);
    }
}

void ProEditorDialog::saveProData() {
    // Save common data back to PRO header
    _pro->header.message_id = _messageIdEdit->value();
    _pro->header.FID = _mainFID;
    _pro->header.light_distance = _lightDistanceEdit->value();
    _pro->header.light_intensity = _lightIntensityEdit->value();
    // Flags are updated real-time via checkbox signal handlers
    
    // Extended flags are updated real-time via signal handlers
    if (_sidEdit) {
        _pro->commonItemData.SID = _sidEdit->value();
    }
    if (_materialIdEdit) {
        _pro->commonItemData.materialId = _materialIdEdit->currentIndex();
    }
    if (_containerSizeEdit) {
        _pro->commonItemData.containerSize = _containerSizeEdit->value();
    }
    if (_weightEdit) {
        _pro->commonItemData.weight = _weightEdit->value();
    }
    if (_basePriceEdit) {
        _pro->commonItemData.basePrice = _basePriceEdit->value();
    }
    if (_inventoryFIDLabel) {
        _pro->commonItemData.inventoryFID = _inventoryFID;
    }
    if (_soundIdEdit) {
        _pro->commonItemData.soundId = _soundIdEdit->value();
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
            saveWallData();
            break;
        case Pro::OBJECT_TYPE::TILE:
            saveTileData();
            break;
        case Pro::OBJECT_TYPE::MISC:
            // This type only has common data
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
    _pro->armorData.armorMaleFID = _armorMaleFID;
    _pro->armorData.armorFemaleFID = _armorFemaleFID;
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
    // Save stat selections (index 0 = "None" maps to 0xFFFF)
    _pro->drugData.stat0 = _drugStatCombos[0]->currentIndex() == 0 ? 0xFFFF : _drugStatCombos[0]->currentIndex() - 1;
    _pro->drugData.stat1 = _drugStatCombos[1]->currentIndex() == 0 ? 0xFFFF : _drugStatCombos[1]->currentIndex() - 1;
    _pro->drugData.stat2 = _drugStatCombos[2]->currentIndex() == 0 ? 0xFFFF : _drugStatCombos[2]->currentIndex() - 1;
    
    // Save immediate effect amounts
    _pro->drugData.amount0 = _drugStatAmountEdits[0]->value();
    _pro->drugData.amount1 = _drugStatAmountEdits[1]->value();
    _pro->drugData.amount2 = _drugStatAmountEdits[2]->value();
    
    // Save first delayed effect
    _pro->drugData.duration1 = _drugFirstDelayEdit->value();
    _pro->drugData.amount0_1 = _drugFirstStatAmountEdits[0]->value();
    _pro->drugData.amount1_1 = _drugFirstStatAmountEdits[1]->value();
    _pro->drugData.amount2_1 = _drugFirstStatAmountEdits[2]->value();
    
    // Save second delayed effect
    _pro->drugData.duration2 = _drugSecondDelayEdit->value();
    _pro->drugData.amount0_2 = _drugSecondStatAmountEdits[0]->value();
    _pro->drugData.amount1_2 = _drugSecondStatAmountEdits[1]->value();
    _pro->drugData.amount2_2 = _drugSecondStatAmountEdits[2]->value();
    
    // Save addiction data
    _pro->drugData.addictionRate = _drugAddictionChanceEdit->value();
    _pro->drugData.addictionEffect = _drugAddictionPerkCombo->currentIndex();
    _pro->drugData.addictionOnset = _drugAddictionDelayEdit->value();
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
        _pro->weaponData.weaponFlags = Pro::setFlag(_pro->weaponData.weaponFlags, Pro::WEAPON_FLAGS::ENERGY_WEAPON);
    } else {
        _pro->weaponData.weaponFlags = Pro::clearFlag(_pro->weaponData.weaponFlags, Pro::WEAPON_FLAGS::ENERGY_WEAPON);
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
    
    // Add type-specific fields to the two columns
    Pro::OBJECT_TYPE type = _pro->type();
    
    // Add type-specific fields based on PRO type
    switch (type) {
        case Pro::OBJECT_TYPE::ITEM:
            setupItemFields();
            break;
        case Pro::OBJECT_TYPE::CRITTER:
            setupCritterFields();
            break;
        case Pro::OBJECT_TYPE::SCENERY:
            setupSceneryFields();
            break;
        case Pro::OBJECT_TYPE::WALL:
            setupWallFields();
            break;
        case Pro::OBJECT_TYPE::TILE:
            setupTileFields();
            break;
        case Pro::OBJECT_TYPE::MISC:
            setupMiscFields();
            break;
    }
}

void ProEditorDialog::setupItemFields() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM) return;
    
    Pro::ITEM_TYPE itemType = _pro->itemType();
    
    // Clear any existing widgets in the right panels
    while (QLayoutItem* item = _leftFieldsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    while (QLayoutItem* item = _rightFieldsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    
    // Setup type-specific fields based on item subtype
    switch (itemType) {
        case Pro::ITEM_TYPE::ARMOR:
            setupArmorFields();
            break;
        case Pro::ITEM_TYPE::CONTAINER:
            setupContainerFields();
            break;
        case Pro::ITEM_TYPE::DRUG:
            setupDrugFields();
            break;
        case Pro::ITEM_TYPE::WEAPON:
            setupWeaponFields();
            break;
        case Pro::ITEM_TYPE::AMMO:
            setupAmmoFields();
            break;
        case Pro::ITEM_TYPE::MISC:
            setupMiscItemFields();
            break;
        case Pro::ITEM_TYPE::KEY:
            setupKeyFields();
            break;
    }
}

void ProEditorDialog::setupCritterFields() {
    // Show the right column for critter tab (ensure both columns are visible)
    QWidget* rightColumn2 = _rightFieldsLayout->parentWidget();
    if (rightColumn2) {
        rightColumn2->show();
    }
    
    // Clear any existing widgets in the right panels
    while (QLayoutItem* item = _leftFieldsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    while (QLayoutItem* item = _rightFieldsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    
    // === COLUMN 1: Critter-Specific Properties and SPECIAL Stats ===
    
    // Critter-Specific Properties (not shown in left panel)
    QGroupBox* critterGroup = new QGroupBox("Critter Properties");
    QFormLayout* critterLayout = new QFormLayout(critterGroup);
    critterLayout->setContentsMargins(8, 8, 8, 8);
    critterLayout->setSpacing(4);
    
    // Head FID with selector button
    QWidget* headFidWidget = new QWidget();
    QHBoxLayout* headFidLayout = new QHBoxLayout(headFidWidget);
    headFidLayout->setContentsMargins(0, 0, 0, 0);
    headFidLayout->setSpacing(4);
    
    _critterHeadFIDLabel = new QLabel("No FRM");
    _critterHeadFIDLabel->setToolTip("FRM filename for critter head appearance");
    _critterHeadFIDLabel->setStyleSheet("QLabel { border: 1px solid #e2e8f0; padding: 2px 4px; background-color: white; }");
    
    _critterHeadFIDSelectorButton = new QPushButton("...");
    _critterHeadFIDSelectorButton->setMaximumWidth(24);
    _critterHeadFIDSelectorButton->setMaximumHeight(22);
    _critterHeadFIDSelectorButton->setToolTip("Browse FRM files for critter head");
    connect(_critterHeadFIDSelectorButton, &QPushButton::clicked, this, &ProEditorDialog::onCritterHeadFidSelectorClicked);
    
    headFidLayout->addWidget(_critterHeadFIDLabel, 1);
    headFidLayout->addWidget(_critterHeadFIDSelectorButton);
    critterLayout->addRow("Head FID:", headFidWidget);
    
    _critterAIPacketEdit = createSpinBox(0, MAX_AI_PACKET, "AI packet number for critter behavior");
    connectSpinBox(_critterAIPacketEdit);
    critterLayout->addRow("AI Packet:", _critterAIPacketEdit);
    
    _critterTeamNumberEdit = createSpinBox(0, MAX_TEAM_NUMBER, "Team number for faction identification");
    connectSpinBox(_critterTeamNumberEdit);
    critterLayout->addRow("Team Number:", _critterTeamNumberEdit);
    
    _leftFieldsLayout->addWidget(critterGroup);
    
    // Critter Flags Group (two-column layout)
    QGroupBox* critterFlagsGroup = new QGroupBox("Critter Flags");
    QGridLayout* critterFlagsLayout = new QGridLayout(critterFlagsGroup);
    critterFlagsLayout->setContentsMargins(8, 8, 8, 8);
    critterFlagsLayout->setSpacing(4);
    
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
    
    _critterFlatCheck = new QCheckBox("Flat");
    _critterFlatCheck->setToolTip("Flat critter");
    connect(_critterFlatCheck, &QCheckBox::toggled, this, &ProEditorDialog::onCritterFlagChanged);
    critterFlagsLayout->addWidget(_critterFlatCheck, 1, 1);
    
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
    specialLayout->setContentsMargins(8, 8, 8, 8);
    specialLayout->setSpacing(4);
    
    const char* specialNames[] = {"STR", "PER", "END", "CHR", "INT", "AGL", "LCK"};
    for (int i = 0; i < 7; ++i) {
        _critterSpecialStatEdits[i] = new QSpinBox();
        _critterSpecialStatEdits[i]->setRange(MIN_SPECIAL_STAT, MAX_SPECIAL_STAT);
        _critterSpecialStatEdits[i]->setToolTip(QString("Base %1 stat").arg(specialNames[i]));
        connect(_critterSpecialStatEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        
        QLabel* label = new QLabel(specialNames[i]);
        label->setFixedWidth(30);
        specialLayout->addWidget(label, i / 4, (i % 4) * 2);
        specialLayout->addWidget(_critterSpecialStatEdits[i], i / 4, (i % 4) * 2 + 1);
    }
    _leftFieldsLayout->addWidget(specialGroup);
    
    // Primary Combat Stats
    QGroupBox* combatGroup = new QGroupBox("Combat Stats");
    QFormLayout* combatLayout = new QFormLayout(combatGroup);
    combatLayout->setContentsMargins(8, 8, 8, 8);
    combatLayout->setSpacing(4);
    
    _critterMaxHitPointsEdit = new QSpinBox();
    _critterMaxHitPointsEdit->setRange(1, MAX_CRITTER_HIT_POINTS);
    _critterMaxHitPointsEdit->setToolTip("Maximum hit points");
    connect(_critterMaxHitPointsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Max HP:", _critterMaxHitPointsEdit);
    
    _critterActionPointsEdit = new QSpinBox();
    _critterActionPointsEdit->setRange(1, MAX_ACTION_POINTS);
    _critterActionPointsEdit->setToolTip("Action points per turn");
    connect(_critterActionPointsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Action Points:", _critterActionPointsEdit);
    
    _critterArmorClassEdit = new QSpinBox();
    _critterArmorClassEdit->setRange(0, MAX_ARMOR_CLASS);
    _critterArmorClassEdit->setToolTip("Base armor class");
    connect(_critterArmorClassEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Armor Class:", _critterArmorClassEdit);
    
    _critterMeleeDamageEdit = new QSpinBox();
    _critterMeleeDamageEdit->setRange(0, MAX_MELEE_DAMAGE);
    _critterMeleeDamageEdit->setToolTip("Melee damage bonus");
    connect(_critterMeleeDamageEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    combatLayout->addRow("Melee Damage:", _critterMeleeDamageEdit);
    
    _leftFieldsLayout->addWidget(combatGroup);
    
    // === COLUMN 2: Advanced Stats, Damage Resistance/Threshold, Skills ===
    
    // Advanced Stats & Characteristics
    QGroupBox* advancedGroup = new QGroupBox("Advanced Stats");
    QFormLayout* advancedLayout = new QFormLayout(advancedGroup);
    advancedLayout->setContentsMargins(8, 8, 8, 8);
    advancedLayout->setSpacing(4);
    
    _critterCarryWeightMaxEdit = new QSpinBox();
    _critterCarryWeightMaxEdit->setRange(0, MAX_CARRY_WEIGHT);
    _critterCarryWeightMaxEdit->setToolTip("Maximum carry weight");
    connect(_critterCarryWeightMaxEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Carry Weight:", _critterCarryWeightMaxEdit);
    
    _critterSequenceEdit = new QSpinBox();
    _critterSequenceEdit->setRange(0, MAX_SEQUENCE);
    _critterSequenceEdit->setToolTip("Initiative sequence bonus");
    connect(_critterSequenceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Sequence:", _critterSequenceEdit);
    
    _critterHealingRateEdit = new QSpinBox();
    _critterHealingRateEdit->setRange(0, MAX_HEALING_RATE);
    _critterHealingRateEdit->setToolTip("Healing rate bonus");
    connect(_critterHealingRateEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Healing Rate:", _critterHealingRateEdit);
    
    _critterCriticalChanceEdit = new QSpinBox();
    _critterCriticalChanceEdit->setRange(0, MAX_CRITICAL_CHANCE);
    _critterCriticalChanceEdit->setToolTip("Critical hit chance bonus");
    connect(_critterCriticalChanceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Critical Chance:", _critterCriticalChanceEdit);
    
    _critterBetterCriticalsEdit = new QSpinBox();
    _critterBetterCriticalsEdit->setRange(0, MAX_BETTER_CRITICALS);
    _critterBetterCriticalsEdit->setToolTip("Better criticals bonus");
    connect(_critterBetterCriticalsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Better Criticals:", _critterBetterCriticalsEdit);
    
    _critterAgeEdit = new QSpinBox();
    _critterAgeEdit->setRange(0, MAX_AGE);
    _critterAgeEdit->setToolTip("Critter age");
    connect(_critterAgeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Age:", _critterAgeEdit);
    
    _critterGenderCombo = new QComboBox();
    _critterGenderCombo->addItems({"Male", "Female"});
    _critterGenderCombo->setToolTip("Critter gender");
    connect(_critterGenderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    advancedLayout->addRow("Gender:", _critterGenderCombo);
    
    _critterBodyTypeCombo = new QComboBox();
    _critterBodyTypeCombo->addItems({"Biped", "Quadruped", "Robotic"});
    _critterBodyTypeCombo->setToolTip("Body type for animations");
    connect(_critterBodyTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    advancedLayout->addRow("Body Type:", _critterBodyTypeCombo);
    
    _critterExperienceEdit = new QSpinBox();
    _critterExperienceEdit->setRange(0, MAX_EXPERIENCE);
    _critterExperienceEdit->setToolTip("Experience points for killing this critter");
    connect(_critterExperienceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Experience:", _critterExperienceEdit);
    
    _critterKillTypeEdit = new QSpinBox();
    _critterKillTypeEdit->setRange(0, MAX_KILL_TYPE);
    _critterKillTypeEdit->setToolTip("Kill type for karma tracking");
    connect(_critterKillTypeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Kill Type:", _critterKillTypeEdit);
    
    _critterDamageTypeEdit = new QSpinBox();
    _critterDamageTypeEdit->setRange(0, MAX_DAMAGE_TYPE);
    _critterDamageTypeEdit->setToolTip("Default damage type");
    connect(_critterDamageTypeEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    advancedLayout->addRow("Damage Type:", _critterDamageTypeEdit);
    
    _rightFieldsLayout->addWidget(advancedGroup);
    
    // Damage Protection (unified resistance and threshold)
    QGroupBox* damageProtectionGroup = new QGroupBox("Damage Protection");
    QGridLayout* damageProtectionLayout = new QGridLayout(damageProtectionGroup);
    damageProtectionLayout->setContentsMargins(8, 8, 8, 8);
    damageProtectionLayout->setSpacing(2);
    
    // Headers
    QLabel* thresholdHeader = new QLabel("Threshold");
    thresholdHeader->setStyleSheet("font-weight: bold;");
    thresholdHeader->setAlignment(Qt::AlignCenter);
    damageProtectionLayout->addWidget(thresholdHeader, 0, 1);
    
    QLabel* resistanceHeader = new QLabel("Resistance");
    resistanceHeader->setStyleSheet("font-weight: bold;");
    resistanceHeader->setAlignment(Qt::AlignCenter);
    damageProtectionLayout->addWidget(resistanceHeader, 0, 2);
    
    const char* damageTypes[] = {"Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion", "Radiation", "Poison"};
    
    // First 7 damage types (threshold + resistance)
    for (int i = 0; i < 7; ++i) {
        // Damage type label
        QLabel* typeLabel = new QLabel(QString(damageTypes[i]) + ":");
        typeLabel->setFixedWidth(60);
        damageProtectionLayout->addWidget(typeLabel, i + 1, 0);
        
        // Damage threshold
        _critterDamageThresholdEdits[i] = new QSpinBox();
        _critterDamageThresholdEdits[i]->setRange(0, MAX_DAMAGE_THRESHOLD);
        _critterDamageThresholdEdits[i]->setToolTip(QString("%1 damage threshold").arg(damageTypes[i]));
        _critterDamageThresholdEdits[i]->setFixedWidth(60);
        connect(_critterDamageThresholdEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        damageProtectionLayout->addWidget(_critterDamageThresholdEdits[i], i + 1, 1);
        
        // Damage resistance
        _critterDamageResistEdits[i] = new QSpinBox();
        _critterDamageResistEdits[i]->setRange(0, MAX_DAMAGE_RESISTANCE);
        _critterDamageResistEdits[i]->setToolTip(QString("%1 damage resistance").arg(damageTypes[i]));
        _critterDamageResistEdits[i]->setFixedWidth(60);
        connect(_critterDamageResistEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        damageProtectionLayout->addWidget(_critterDamageResistEdits[i], i + 1, 2);
    }
    
    // Last 2 damage types (Radiation and Poison - resistance only)
    for (int i = 7; i < 9; ++i) {
        QLabel* typeLabel = new QLabel(QString(damageTypes[i]) + ":");
        typeLabel->setFixedWidth(60);
        damageProtectionLayout->addWidget(typeLabel, i + 1, 0);
        
        // No threshold for Radiation and Poison, add placeholder
        QLabel* placeholder = new QLabel("—");
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: gray;");
        placeholder->setFixedWidth(60);
        damageProtectionLayout->addWidget(placeholder, i + 1, 1);
        
        // Damage resistance
        _critterDamageResistEdits[i] = new QSpinBox();
        _critterDamageResistEdits[i]->setRange(0, MAX_DAMAGE_RESISTANCE);
        _critterDamageResistEdits[i]->setToolTip(QString("%1 damage resistance").arg(damageTypes[i]));
        _critterDamageResistEdits[i]->setFixedWidth(60);
        connect(_critterDamageResistEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        damageProtectionLayout->addWidget(_critterDamageResistEdits[i], i + 1, 2);
    }
    
    _rightFieldsLayout->addWidget(damageProtectionGroup);
    
    // Add stretch to push everything to top
    _leftFieldsLayout->addStretch();
    _rightFieldsLayout->addStretch();
}

void ProEditorDialog::setupSceneryFields() {
    // Clear any existing widgets in the right panels
    while (QLayoutItem* item = _leftFieldsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    while (QLayoutItem* item = _rightFieldsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    
    // === COLUMN 1: Basic Scenery Properties ===
    
    QGroupBox* basicGroup = new QGroupBox("Basic Properties");
    QFormLayout* basicLayout = new QFormLayout(basicGroup);
    basicLayout->setContentsMargins(8, 8, 8, 8);
    basicLayout->setSpacing(4);
    
    _sceneryMaterialIdEdit = createMaterialComboBox("Material type for scenery");
    connectComboBox(_sceneryMaterialIdEdit);
    basicLayout->addRow("Material:", _sceneryMaterialIdEdit);
    
    _scenerySoundIdEdit = createSpinBox(0, 255, "Sound ID for interactions");
    connectSpinBox(_scenerySoundIdEdit);
    basicLayout->addRow("Sound ID:", _scenerySoundIdEdit);
    
    _sceneryTypeCombo = createComboBox({"Door", "Stairs", "Elevator", "Ladder Bottom", "Ladder Top", "Generic"}, "Scenery subtype");
    connectComboBox(_sceneryTypeCombo);
    basicLayout->addRow("Type:", _sceneryTypeCombo);
    
    _leftFieldsLayout->addWidget(basicGroup);
    
    // === COLUMN 2: Type-Specific Properties ===
    
    // Door Properties
    QGroupBox* doorGroup = new QGroupBox("Door Properties");
    QFormLayout* doorLayout = new QFormLayout(doorGroup);
    doorLayout->setContentsMargins(8, 8, 8, 8);
    doorLayout->setSpacing(4);
    
    _doorWalkThroughCheck = new QCheckBox("Walk Through");
    _doorWalkThroughCheck->setToolTip("Allow walking through the door");
    connectCheckBox(_doorWalkThroughCheck);
    doorLayout->addRow("", _doorWalkThroughCheck);
    
    _doorUnknownEdit = createSpinBox(0, 0xFFFFFFFF, "Door unknown field");
    connectSpinBox(_doorUnknownEdit);
    doorLayout->addRow("Unknown Field:", _doorUnknownEdit);
    
    _rightFieldsLayout->addWidget(doorGroup);
    
    // Stairs Properties
    QGroupBox* stairsGroup = new QGroupBox("Stairs Properties");
    QFormLayout* stairsLayout = new QFormLayout(stairsGroup);
    stairsLayout->setContentsMargins(8, 8, 8, 8);
    stairsLayout->setSpacing(4);
    
    _stairsDestTileEdit = createSpinBox(0, 39999, "Destination tile number");
    connectSpinBox(_stairsDestTileEdit);
    stairsLayout->addRow("Dest Tile:", _stairsDestTileEdit);
    
    _stairsDestElevationEdit = createSpinBox(0, 3, "Destination elevation");
    connectSpinBox(_stairsDestElevationEdit);
    stairsLayout->addRow("Dest Elevation:", _stairsDestElevationEdit);
    
    _rightFieldsLayout->addWidget(stairsGroup);
    
    // Elevator Properties (these will be shown/hidden based on scenery type)
    QGroupBox* elevatorGroup = new QGroupBox("Elevator Properties");
    QFormLayout* elevatorLayout = new QFormLayout(elevatorGroup);
    elevatorLayout->setContentsMargins(8, 8, 8, 8);
    elevatorLayout->setSpacing(4);
    
    _elevatorTypeEdit = createSpinBox(0, MAX_ELEVATOR_VALUE, "Elevator type");
    connectSpinBox(_elevatorTypeEdit);
    elevatorLayout->addRow("Type:", _elevatorTypeEdit);
    
    _elevatorLevelEdit = createSpinBox(0, MAX_ELEVATOR_VALUE, "Elevator level");
    connectSpinBox(_elevatorLevelEdit);
    elevatorLayout->addRow("Level:", _elevatorLevelEdit);
    
    _rightFieldsLayout->addWidget(elevatorGroup);
    
    // Ladder Properties
    QGroupBox* ladderGroup = new QGroupBox("Ladder Properties");
    QFormLayout* ladderLayout = new QFormLayout(ladderGroup);
    ladderLayout->setContentsMargins(8, 8, 8, 8);
    ladderLayout->setSpacing(4);
    
    _ladderDestTileElevationEdit = createSpinBox(0, 0xFFFFFFFF, "Destination tile and elevation combined");
    connectSpinBox(_ladderDestTileElevationEdit);
    ladderLayout->addRow("Dest Tile+Elev:", _ladderDestTileElevationEdit);
    
    _rightFieldsLayout->addWidget(ladderGroup);
    
    // Generic Properties
    QGroupBox* genericGroup = new QGroupBox("Generic Properties");
    QFormLayout* genericLayout = new QFormLayout(genericGroup);
    genericLayout->setContentsMargins(8, 8, 8, 8);
    genericLayout->setSpacing(4);
    
    _genericUnknownEdit = createSpinBox(0, 0xFFFFFFFF, "Generic unknown field");
    connectSpinBox(_genericUnknownEdit);
    genericLayout->addRow("Unknown Field:", _genericUnknownEdit);
    
    _rightFieldsLayout->addWidget(genericGroup);
    
    // Add stretch to push content to top
    _leftFieldsLayout->addStretch();
    _rightFieldsLayout->addStretch();
}

void ProEditorDialog::setupWallFields() {
    // TODO: Implement wall-specific fields  
}

void ProEditorDialog::setupTileFields() {
    // TODO: Implement tile-specific fields
}

void ProEditorDialog::setupMiscFields() {
    // TODO: Implement misc-specific fields
}



void ProEditorDialog::updatePreview() {
    
    // Stop current animations
    if (_animationTimer && _animationTimer->isActive()) {
        _animationTimer->stop();
        _isAnimating = false;
        if (_playPauseButton) {
            _playPauseButton->setText("▶");
        }
    }
    
    // Stop armor animation
    if (_armorAnimationTimer && _armorAnimationTimer->isActive()) {
        _armorAnimationTimer->stop();
        _armorIsAnimating = false;
        if (_armorPlayPauseButton) {
            _armorPlayPauseButton->setText("▶");
        }
    }
    
    // Check if we're using dual preview system (for items)
    if (_pro && _pro->type() == Pro::OBJECT_TYPE::ITEM && _inventoryPreviewWidget && _groundPreviewWidget) {
        // Update both dual previews (items use static thumbnails)
        updateInventoryPreview();
        updateGroundPreview();
        
        // Update armor preview if this is an armor item
        if (_pro->itemType() == Pro::ITEM_TYPE::ARMOR && _armorMalePreviewLabel && _armorFemalePreviewLabel) {
            updateArmorPreview();
        }
        
        // Items don't animate, so disable animation controls and return
        if (_animationControls) {
            _animationControls->setEnabled(false);
        }
        return;
    }
    
    // Check if we're using object preview widget (all non-item objects)
    if (_pro && _pro->type() != Pro::OBJECT_TYPE::ITEM && _objectPreviewWidget) {
        std::string frmPath = ResourceManager::getInstance().FIDtoFrmName(static_cast<unsigned int>(_pro->header.FID));
        spdlog::debug("ObjectPreviewWidget: Setting FRM path: {}", frmPath);
        _objectPreviewWidget->setFrmPath(QString::fromStdString(frmPath));
        _objectPreviewWidget->setFid(_pro->header.FID);
        return;
    }
    
    // No specific action needed - ObjectPreviewWidget handles everything for non-item objects
}

int32_t ProEditorDialog::getPreviewFid() {
    
    // Check if basic widgets are initialized
    if (!_fidLabel) {
        return 0;
    }
    
    // Check if PRO is valid
    if (!_pro) {
        return 0;
    }
    
    // Items use dual preview system, not single preview, so they don't need getPreviewFid()
    
    // For items without dual preview, prefer inventory FID over world FID for preview
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM && _inventoryFIDLabel) {
        int32_t inventoryFid = _inventoryFID;
        if (inventoryFid > 0) {
            return inventoryFid;
        }
        
        // For armor, check for male/female specific FIDs
        if (_pro->itemType() == Pro::ITEM_TYPE::ARMOR && _armorMaleFIDLabel && _armorFemaleFIDLabel) {
            int32_t maleFid = _armorMaleFID;
            if (maleFid > 0) {
                return maleFid; // Default to male version
            }
            
            int32_t femaleFid = _armorFemaleFID;
            if (femaleFid > 0) {
                return femaleFid;
            }
        }
    }
    
    // Fall back to main FID
    return _mainFID;
}

int32_t ProEditorDialog::getInventoryFid() {
    
    // Check if pro is loaded
    if (!_pro) {
        return 0;
    }
    
    // For items, return inventory FID only if set, don't fallback to main FID
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        int32_t inventoryFid = _inventoryFID;
        // Return the inventory FID as-is, even if 0 or -1 (let caller handle "no image" case)
        return inventoryFid;
    }
    
    return 0;
}

int32_t ProEditorDialog::getGroundFid() {
    
    // Check if pro is loaded
    if (!_pro) {
        return 0;
    }
    
    // For items, return main FID (ground view)
    if (_pro->type() == Pro::OBJECT_TYPE::ITEM) {
        int32_t groundFid = _mainFID;
        return groundFid;
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
    
    // Set FRM path for inventory view
    try {
        auto& resourceManager = ResourceManager::getInstance();
        std::string frmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(inventoryFid));
        
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
    
    // Set FRM path for ground view
    try {
        auto& resourceManager = ResourceManager::getInstance();
        std::string frmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(groundFid));
        
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

void ProEditorDialog::updateArmorPreview() {
    if (!_armorMalePreviewLabel || !_armorFemalePreviewLabel) {
        return;
    }
    
    // Update male armor preview
    if (_armorMaleFID <= 0) {
        _armorMalePreviewLabel->clear();
        _armorMalePreviewLabel->setText("No male armor FRM");
    } else {
        // Use cached frame if available, otherwise fall back to static thumbnail
        if (_armorCurrentFrame < static_cast<int>(_armorMaleFrameCache.size()) && 
            !_armorMaleFrameCache[_armorCurrentFrame].isNull()) {
            QPixmap scaledPixmap = _armorMaleFrameCache[_armorCurrentFrame].scaled(_armorMalePreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            _armorMalePreviewLabel->setPixmap(scaledPixmap);
        } else {
            // Fall back to static thumbnail generation
            try {
                auto& resourceManager = ResourceManager::getInstance();
                std::string maleFrmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(_armorMaleFID));
                
                if (maleFrmPath.empty()) {
                    _armorMalePreviewLabel->clear();
                    _armorMalePreviewLabel->setText("Invalid male armor FID");
                } else {
                    QPixmap maleThumbnail = createFrmThumbnail(maleFrmPath, QSize(120, 120));
                    if (!maleThumbnail.isNull()) {
                        QPixmap scaledPixmap = maleThumbnail.scaled(_armorMalePreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        _armorMalePreviewLabel->setPixmap(scaledPixmap);
                    } else {
                        _armorMalePreviewLabel->clear();
                        _armorMalePreviewLabel->setText("Failed to load male armor FRM");
                    }
                }
            } catch (const std::exception& e) {
                spdlog::error("ProEditorDialog::updateArmorPreview() - male armor exception: {}", e.what());
                _armorMalePreviewLabel->clear();
                _armorMalePreviewLabel->setText("Error loading male armor FRM");
            }
        }
    }
    
    // Update female armor preview
    if (_armorFemaleFID <= 0) {
        _armorFemalePreviewLabel->clear();
        _armorFemalePreviewLabel->setText("No female armor FRM");
    } else {
        // Use cached frame if available, otherwise fall back to static thumbnail
        if (_armorCurrentFrame < static_cast<int>(_armorFemaleFrameCache.size()) && 
            !_armorFemaleFrameCache[_armorCurrentFrame].isNull()) {
            QPixmap scaledPixmap = _armorFemaleFrameCache[_armorCurrentFrame].scaled(_armorFemalePreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            _armorFemalePreviewLabel->setPixmap(scaledPixmap);
        } else {
            // Fall back to static thumbnail generation
            try {
                auto& resourceManager = ResourceManager::getInstance();
                std::string femaleFrmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(_armorFemaleFID));
                
                if (femaleFrmPath.empty()) {
                    _armorFemalePreviewLabel->clear();
                    _armorFemalePreviewLabel->setText("Invalid female armor FID");
                } else {
                    QPixmap femaleThumbnail = createFrmThumbnail(femaleFrmPath, QSize(120, 120));
                    if (!femaleThumbnail.isNull()) {
                        QPixmap scaledPixmap = femaleThumbnail.scaled(_armorFemalePreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        _armorFemalePreviewLabel->setPixmap(scaledPixmap);
                    } else {
                        _armorFemalePreviewLabel->clear();
                        _armorFemalePreviewLabel->setText("Failed to load female armor FRM");
                    }
                }
            } catch (const std::exception& e) {
                spdlog::error("ProEditorDialog::updateArmorPreview() - female armor exception: {}", e.what());
                _armorFemalePreviewLabel->clear();
                _armorFemalePreviewLabel->setText("Error loading female armor FRM");
            }
        }
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
        // Update preview if ground FID field changed
        if (sender == _fidLabel) {
            updatePreview();
        }
        
        // Update AI priority displays when relevant fields change
        updateAIPriorityDisplays();
    }
}

void ProEditorDialog::onComboBoxChanged() {
    QComboBox* sender = qobject_cast<QComboBox*>(QObject::sender());
    if (sender) {
        // Update AI priority displays when relevant fields change
        updateAIPriorityDisplays();
    }
}

void ProEditorDialog::onCheckBoxChanged() {
    QCheckBox* sender = qobject_cast<QCheckBox*>(QObject::sender());
    if (sender) {
        // CheckBox changed - no additional action needed
    }
}

void ProEditorDialog::onFidSelectorClicked() {
    openFrmSelectorForLabel(_fidLabel, &_mainFID, 0); // Items type
}

void ProEditorDialog::onInventoryFidSelectorClicked() {
    openFrmSelectorForLabel(_inventoryFIDLabel, &_inventoryFID, 7); // Inventory type
}

void ProEditorDialog::onArmorMaleFidSelectorClicked() {
    openFrmSelectorForLabel(_armorMaleFIDLabel, &_armorMaleFID, 7); // Inventory type for armor
    loadArmorAnimationFrames(); // Reload animation frames after FID selection
}

void ProEditorDialog::onArmorFemaleFidSelectorClicked() {
    openFrmSelectorForLabel(_armorFemaleFIDLabel, &_armorFemaleFID, 7); // Inventory type for armor
    loadArmorAnimationFrames(); // Reload animation frames after FID selection
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

void ProEditorDialog::openFrmSelectorForLabel(QLabel* targetLabel, int32_t* fidStorage, uint32_t objectType) {
    if (!targetLabel || !fidStorage) return;
    
    FrmSelectorDialog dialog(this);
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
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);
    return layout;
}

QGroupBox* ProEditorDialog::createStandardGroupBox(const QString& title) {
    QGroupBox* groupBox = new QGroupBox(title);
    createStandardFormLayout(groupBox);
    return groupBox;
}

QHBoxLayout* ProEditorDialog::createTwoColumnLayout(QWidget* parent) {
    QHBoxLayout* layout = new QHBoxLayout(parent);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);
    return layout;
}

// Widget array helper methods (DRY principle)
void ProEditorDialog::loadIntArrayToWidgets(QSpinBox** widgets, const uint32_t* data, int count) {
    for (int i = 0; i < count; ++i) {
        if (widgets[i]) {
            widgets[i]->setValue(static_cast<int>(data[i]));
        }
    }
}

void ProEditorDialog::saveWidgetsToIntArray(QSpinBox** widgets, uint32_t* data, int count) {
    for (int i = 0; i < count; ++i) {
        if (widgets[i]) {
            data[i] = static_cast<uint32_t>(widgets[i]->value());
        }
    }
}

QSpinBox** ProEditorDialog::createConnectedSpinBoxArray(int count, int min, int max, const QStringList& tooltips) {
    QSpinBox** widgets = new QSpinBox*[count];
    for (int i = 0; i < count; ++i) {
        widgets[i] = createSpinBox(min, max, i < tooltips.size() ? tooltips[i] : QString());
    }
    return widgets;
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
    if (_critterAIPacketEdit) _critterAIPacketEdit->setValue(static_cast<int>(critterData.aiPacket));
    if (_critterTeamNumberEdit) _critterTeamNumberEdit->setValue(static_cast<int>(critterData.teamNumber));
    if (_critterFlagsEdit) _critterFlagsEdit->setValue(static_cast<int>(critterData.flags));
    
    // Load critter flag checkboxes
    if (_critterBarterCheck) _critterBarterCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_BARTER));
    if (_critterNoStealCheck) _critterNoStealCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_STEAL));
    if (_critterNoDropCheck) _critterNoDropCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_DROP));
    if (_critterNoLimbsCheck) _critterNoLimbsCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_LIMBS));
    if (_critterNoAgeCheck) _critterNoAgeCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_AGE));
    if (_critterNoHealCheck) _critterNoHealCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_HEAL));
    if (_critterInvulnerableCheck) _critterInvulnerableCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_INVULNERABLE));
    if (_critterFlatCheck) _critterFlatCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_FLAT));
    if (_critterSpecialDeathCheck) _critterSpecialDeathCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_SPECIAL_DEATH));
    if (_critterLongLimbsCheck) _critterLongLimbsCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_LONG_LIMBS));
    if (_critterNoKnockbackCheck) _critterNoKnockbackCheck->setChecked(Pro::hasFlag(critterData.flags, Pro::CritterFlags::CRITTER_NO_KNOCKBACK));
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
    if (_sceneryMaterialIdEdit) _sceneryMaterialIdEdit->setCurrentIndex(static_cast<int>(sceneryData.materialId));
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
    if (_critterHeadFIDLabel) critterData.headFID = static_cast<uint32_t>(_critterHeadFID);
    if (_critterAIPacketEdit) critterData.aiPacket = static_cast<uint32_t>(_critterAIPacketEdit->value());
    if (_critterTeamNumberEdit) critterData.teamNumber = static_cast<uint32_t>(_critterTeamNumberEdit->value());
    
    // Save critter flags from checkboxes (preferred) or numeric field (fallback)
    if (_critterBarterCheck) {
        uint32_t flags = 0;
        if (_critterBarterCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_BARTER);
        if (_critterNoStealCheck && _critterNoStealCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_STEAL);
        if (_critterNoDropCheck && _critterNoDropCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_DROP);
        if (_critterNoLimbsCheck && _critterNoLimbsCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_LIMBS);
        if (_critterNoAgeCheck && _critterNoAgeCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_AGE);
        if (_critterNoHealCheck && _critterNoHealCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_HEAL);
        if (_critterInvulnerableCheck && _critterInvulnerableCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_INVULNERABLE);
        if (_critterFlatCheck && _critterFlatCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_FLAT);
        if (_critterSpecialDeathCheck && _critterSpecialDeathCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_SPECIAL_DEATH);
        if (_critterLongLimbsCheck && _critterLongLimbsCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_LONG_LIMBS);
        if (_critterNoKnockbackCheck && _critterNoKnockbackCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_KNOCKBACK);
        critterData.flags = flags;
    } else if (_critterFlagsEdit) {
        critterData.flags = static_cast<uint32_t>(_critterFlagsEdit->value());
    }
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
    if (_sceneryMaterialIdEdit) sceneryData.materialId = static_cast<uint32_t>(_sceneryMaterialIdEdit->currentIndex());
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

void ProEditorDialog::loadWallData() {
    // Wall data is simple - just load the materialId value that's already been read from the file
    // The UI field is set up in setupCommonTab when _pro->type() == Pro::OBJECT_TYPE::WALL
}

void ProEditorDialog::loadTileData() {
    // Tile data is simple - just load the materialId value that's already been read from the file  
    // The UI field is set up in setupCommonTab when _pro->type() == Pro::OBJECT_TYPE::TILE
}

void ProEditorDialog::saveWallData() {
    if (_wallMaterialIdEdit) {
        _pro->wallData.materialId = static_cast<uint32_t>(_wallMaterialIdEdit->currentIndex());
    }
}

void ProEditorDialog::saveTileData() {
    if (_tileMaterialIdEdit) {
        _pro->tileData.materialId = static_cast<uint32_t>(_tileMaterialIdEdit->currentIndex());
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
        const auto* msgFile = ProHelper::msgFile(_pro->type());
        if (!msgFile) {
            _nameLabel->setText("MSG file not found");
            _descriptionLabel->setPlainText(QString("Could not load MSG file for %1").arg(QString::fromStdString(_pro->typeToString())));
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

void ProEditorDialog::onArmorPlayPauseClicked() {
    if (_armorTotalFrames <= 1) {
        return; // Nothing to animate
    }
    
    if (_armorIsAnimating) {
        // Pause
        _armorAnimationTimer->stop();
        _armorIsAnimating = false;
        _armorPlayPauseButton->setText("▶");
    } else {
        // Play
        _armorAnimationTimer->start();
        _armorIsAnimating = true;
        _armorPlayPauseButton->setText("⏸");
    }
}

void ProEditorDialog::onArmorFrameChanged(int frame) {
    if (frame != _armorCurrentFrame) {
        _armorCurrentFrame = frame;
        _armorFrameLabel->setText(QString("%1/%2").arg(frame + 1).arg(_armorTotalFrames));
        
        // Update both armor previews with new frame
        updateArmorPreview();
    }
}

void ProEditorDialog::onArmorDirectionChanged(int direction) {
    if (direction != _armorCurrentDirection) {
        _armorCurrentDirection = direction;
        
        // Reload frames for new direction
        loadArmorAnimationFrames();
    }
}

void ProEditorDialog::onArmorAnimationTick() {
    if (_armorTotalFrames > 1) {
        int nextFrame = (_armorCurrentFrame + 1) % _armorTotalFrames;
        _armorFrameSlider->setValue(nextFrame);
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
        _animationControls->setVisible(shouldEnableAnimation); // Show controls when animation is available
        
 
        
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

void ProEditorDialog::loadArmorAnimationFrames() {
    _armorMaleFrameCache.clear();
    _armorFemaleFrameCache.clear();
    
    // Check if armor animation controls are initialized - return early if not
    if (!_armorFrameSlider || !_armorFrameLabel || !_armorDirectionCombo || !_armorAnimationControls) {
        // Armor animation controls not initialized yet, skip loading frames
        return;
    }
    
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::ITEM || _pro->itemType() != Pro::ITEM_TYPE::ARMOR) {
        _armorAnimationControls->setEnabled(false);
        return;
    }
    
    try {
        auto& resourceManager = ResourceManager::getInstance();
        
        // Load palette
        const Pal* palette = resourceManager.loadResource<Pal>("color.pal");
        if (!palette) {
            _armorAnimationControls->setEnabled(false);
            return;
        }
        
        // Load male armor frames
        std::vector<QPixmap> maleFrames;
        int maleTotalFrames = 0;
        int maleTotalDirections = 0;
        
        if (_armorMaleFID > 0) {
            std::string maleFrmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(_armorMaleFID));
            if (!maleFrmPath.empty()) {
                const auto* maleFrm = resourceManager.loadResource<Frm>(maleFrmPath);
                if (maleFrm) {
                    const auto& directions = maleFrm->directions();
                    maleTotalDirections = static_cast<int>(directions.size());
                    
                    if (_armorCurrentDirection < maleTotalDirections) {
                        const auto& direction = directions[_armorCurrentDirection];
                        const auto& frames = direction.frames();
                        maleTotalFrames = static_cast<int>(frames.size());
                        
                        for (const auto& frame : frames) {
                            QPixmap framePixmap = createFrameThumbnail(frame, palette, QSize(150, 150));
                            maleFrames.push_back(framePixmap);
                        }
                    }
                }
            }
        }
        
        // Load female armor frames
        std::vector<QPixmap> femaleFrames;
        int femaleTotalFrames = 0;
        int femaleTotalDirections = 0;
        
        if (_armorFemaleFID > 0) {
            std::string femaleFrmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(_armorFemaleFID));
            if (!femaleFrmPath.empty()) {
                const auto* femaleFrm = resourceManager.loadResource<Frm>(femaleFrmPath);
                if (femaleFrm) {
                    const auto& directions = femaleFrm->directions();
                    femaleTotalDirections = static_cast<int>(directions.size());
                    
                    if (_armorCurrentDirection < femaleTotalDirections) {
                        const auto& direction = directions[_armorCurrentDirection];
                        const auto& frames = direction.frames();
                        femaleTotalFrames = static_cast<int>(frames.size());
                        
                        for (const auto& frame : frames) {
                            QPixmap framePixmap = createFrameThumbnail(frame, palette, QSize(150, 150));
                            femaleFrames.push_back(framePixmap);
                        }
                    }
                }
            }
        }
        
        // Use the maximum frame count and direction count from both armors
        _armorTotalFrames = std::max(maleTotalFrames, femaleTotalFrames);
        _armorTotalDirections = std::max(maleTotalDirections, femaleTotalDirections);
        
        // Store frame caches
        _armorMaleFrameCache = std::move(maleFrames);
        _armorFemaleFrameCache = std::move(femaleFrames);
        
        // Update UI controls
        _armorFrameSlider->setMaximum(std::max(0, _armorTotalFrames - 1));
        _armorFrameSlider->setValue(0);
        _armorFrameLabel->setText(QString("1/%1").arg(_armorTotalFrames));
        
        // Enable/disable direction combo based on available directions
        _armorDirectionCombo->setEnabled(_armorTotalDirections > 1);
        
        // Enable animation controls if there are multiple frames or directions
        bool shouldEnableAnimation = (_armorTotalFrames > 1 || _armorTotalDirections > 1);
        _armorAnimationControls->setEnabled(shouldEnableAnimation);
        
        // Reset frame counter and update preview
        _armorCurrentFrame = 0;
        updateArmorPreview();
        
    } catch (const std::exception& e) {
        spdlog::warn("ProEditorDialog: Exception loading armor animation frames: {}", e.what());
        if (_armorAnimationControls) {
            _armorAnimationControls->setEnabled(false);
        }
    }
}


void ProEditorDialog::onExtendedFlagChanged() {
    uint32_t flags = 0;
    
    // Rebuild flags from individual controls
    flags |= Pro::setAnimationPrimary(flags, _animationPrimaryEdit ? _animationPrimaryEdit->value() : 0);
    flags |= Pro::setAnimationSecondary(flags, _animationSecondaryEdit ? _animationSecondaryEdit->value() : 0);
    
    if (_bigGunCheck && _bigGunCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::ExtendedItemFlags::BIG_GUN);
    }
    if (_twoHandedCheck && _twoHandedCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::ExtendedItemFlags::TWO_HANDED);
    }
    if (_canUseCheck && _canUseCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::ExtendedItemFlags::CAN_USE);
    }
    if (_canUseOnCheck && _canUseOnCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::ExtendedItemFlags::CAN_USE_ON);
    }
    if (_generalFlagCheck && _generalFlagCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::ExtendedItemFlags::GENERAL_FLAG);
    }
    if (_interactionFlagCheck && _interactionFlagCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::ExtendedItemFlags::INTERACTION_FLAG);
    }
    if (_itemHiddenCheck && _itemHiddenCheck->isChecked()) {
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_HIDDEN);
    }
    if (_lightFlag1Check && _lightFlag1Check->isChecked()) {
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_WALL_TRANS_END);
    }
    if (_lightFlag2Check && _lightFlag2Check->isChecked()) {
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_LIGHT_THRU);
    }
    if (_lightFlag3Check && _lightFlag3Check->isChecked()) {
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_SEEN);
    }
    if (_lightFlag4Check && _lightFlag4Check->isChecked()) {
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_SHOOT_THRU);
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
        priority += static_cast<int>(weaponData.burstRounds) * BURST_ROUND_PRIORITY_MULTIPLIER;
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

void ProEditorDialog::onObjectFlagChanged() {
    // Calculate combined flags value from all checkboxes
    uint32_t flags = 0;
    
    // System flags
    if (_hiddenCheck && _hiddenCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_HIDDEN);
    if (_noSaveCheck && _noSaveCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_NO_SAVE);
    if (_noRemoveCheck && _noRemoveCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_NO_REMOVE);
    if (_queuedCheck && _queuedCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_QUEUED);
    if (_seenCheck && _seenCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_SEEN);
    
    // Rendering flags
    if (_flatCheck && _flatCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_FLAT);
    if (_noBlockCheck && _noBlockCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_NO_BLOCK);
    if (_multiHexCheck && _multiHexCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_MULTIHEX);
    if (_noHighlightCheck && _noHighlightCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_NO_HIGHLIGHT);
    
    // Lighting flags
    if (_lightingCheck && _lightingCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_LIGHTING);
    
    // Transparency flags (keep existing transparency values)
    flags |= (_pro->header.flags & 0x000FC000); // Preserve transparency bits
    
    // Interaction flags
    if (_wallTransEndCheck && _wallTransEndCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_WALL_TRANS_END);
    if (_lightThruCheck && _lightThruCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_LIGHT_THRU);
    if (_shootThruCheck && _shootThruCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_SHOOT_THRU);
    
    // Equipment state flags
    if (_leftHandCheck && _leftHandCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_IN_LEFT_HAND);
    if (_rightHandCheck && _rightHandCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_IN_RIGHT_HAND);
    if (_wornCheck && _wornCheck->isChecked()) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_WORN);
    
    // Update the PRO data
    _pro->header.flags = flags;
}

void ProEditorDialog::onTransparencyFlagChanged() {
    // Handle mutually exclusive transparency flags
    QCheckBox* sender = qobject_cast<QCheckBox*>(QObject::sender());
    if (!sender || !sender->isChecked()) return;
    
    // Uncheck all other transparency flags when one is selected
    if (sender != _transNoneCheck && _transNoneCheck) _transNoneCheck->setChecked(false);
    if (sender != _transRedCheck && _transRedCheck) _transRedCheck->setChecked(false);
    if (sender != _transWallCheck && _transWallCheck) _transWallCheck->setChecked(false);
    if (sender != _transGlassCheck && _transGlassCheck) _transGlassCheck->setChecked(false);
    if (sender != _transSteamCheck && _transSteamCheck) _transSteamCheck->setChecked(false);
    if (sender != _transEnergyCheck && _transEnergyCheck) _transEnergyCheck->setChecked(false);
    
    // Calculate combined flags value
    uint32_t flags = _pro->header.flags & ~0x000FC000; // Clear transparency bits
    
    // Add the selected transparency flag
    if (sender == _transNoneCheck) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_NONE);
    else if (sender == _transRedCheck) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_RED);
    else if (sender == _transWallCheck) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_WALL);
    else if (sender == _transGlassCheck) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_GLASS);
    else if (sender == _transSteamCheck) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_STEAM);
    else if (sender == _transEnergyCheck) flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_ENERGY);
    
    // Update the PRO data
    _pro->header.flags = flags;
}

void ProEditorDialog::loadObjectFlags(uint32_t flags) {
    // Load system flags
    if (_hiddenCheck) _hiddenCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_HIDDEN));
    if (_noSaveCheck) _noSaveCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_SAVE));
    if (_noRemoveCheck) _noRemoveCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_REMOVE));
    if (_queuedCheck) _queuedCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_QUEUED));
    if (_seenCheck) _seenCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_SEEN));
    
    // Load rendering flags
    if (_flatCheck) _flatCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_FLAT));
    if (_noBlockCheck) _noBlockCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_BLOCK));
    if (_multiHexCheck) _multiHexCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_MULTIHEX));
    if (_noHighlightCheck) _noHighlightCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_HIGHLIGHT));
    
    // Load lighting flags
    if (_lightingCheck) _lightingCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_LIGHTING));
    
    // Load transparency flags (mutually exclusive)
    if (_transNoneCheck) _transNoneCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_NONE));
    if (_transRedCheck) _transRedCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_RED));
    if (_transWallCheck) _transWallCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_WALL));
    if (_transGlassCheck) _transGlassCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_GLASS));
    if (_transSteamCheck) _transSteamCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_STEAM));
    if (_transEnergyCheck) _transEnergyCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_ENERGY));
    
    // Load interaction flags
    if (_wallTransEndCheck) _wallTransEndCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_WALL_TRANS_END));
    if (_lightThruCheck) _lightThruCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_LIGHT_THRU));
    if (_shootThruCheck) _shootThruCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_SHOOT_THRU));
    
    // Load equipment state flags
    if (_leftHandCheck) _leftHandCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_IN_LEFT_HAND));
    if (_rightHandCheck) _rightHandCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_IN_RIGHT_HAND));
    if (_wornCheck) _wornCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_WORN));
}

const QStringList ProEditorDialog::getMaterialNames() {
    return QStringList{
        "Glass",     // 0
        "Metal",     // 1
        "Plastic",   // 2
        "Wood",      // 3
        "Dirt",      // 4
        "Stone",     // 5
        "Cement",    // 6
        "Leather"    // 7
    };
}

QComboBox* ProEditorDialog::createMaterialComboBox(const QString& tooltip) {
    QComboBox* comboBox = new QComboBox();
    comboBox->addItems(getMaterialNames());
    if (!tooltip.isEmpty()) {
        comboBox->setToolTip(tooltip);
    }
    return comboBox;
}

QString ProEditorDialog::getFrmFilename(int32_t fid) {
    if (fid <= 0) {
        return "No FRM";
    }
    std::string frmPath = ResourceManager::getInstance().FIDtoFrmName(static_cast<unsigned int>(fid));
    if (frmPath.empty()) {
        return QString("Invalid FID (%1)").arg(fid);
    }
    // Extract just the filename from the full path
    return QString::fromStdString(std::filesystem::path(frmPath).filename().string());
}

void ProEditorDialog::onCritterHeadFidSelectorClicked() {
    openFrmSelectorForLabel(_critterHeadFIDLabel, &_critterHeadFID, 1); // Object type 1 for critters
}

void ProEditorDialog::onObjectFidChangeRequested() {
    if (_objectPreviewWidget && _pro && _pro->type() != Pro::OBJECT_TYPE::ITEM) {
        FrmSelectorDialog dialog(this);
        
        // Set appropriate object type filter based on PRO type
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
                objectTypeFilter = 0; // No filter
                break;
        }
        
        dialog.setObjectTypeFilter(objectTypeFilter);
        dialog.setInitialFrmPid(static_cast<uint32_t>(_pro->header.FID));
        
        if (dialog.exec() == QDialog::Accepted) {
            uint32_t selectedFrmPid = dialog.getSelectedFrmPid();
            if (selectedFrmPid > 0) {
                // Update the PRO data
                _pro->header.FID = static_cast<int32_t>(selectedFrmPid);
                
                // Update the preview widget
                std::string frmPath = ResourceManager::getInstance().FIDtoFrmName(selectedFrmPid);
                _objectPreviewWidget->setFrmPath(QString::fromStdString(frmPath));
                _objectPreviewWidget->setFid(static_cast<int32_t>(selectedFrmPid));
            }
        }
    }
}

void ProEditorDialog::onObjectFidChanged(int32_t newFid) {
    if (_pro && _pro->type() != Pro::OBJECT_TYPE::ITEM) {
        _pro->header.FID = newFid;
        
        // Update the preview
        std::string frmPath = ResourceManager::getInstance().FIDtoFrmName(static_cast<unsigned int>(newFid));
        if (_objectPreviewWidget) {
            _objectPreviewWidget->setFrmPath(QString::fromStdString(frmPath));
            _objectPreviewWidget->setFid(newFid);
        }
    }
}

void ProEditorDialog::setupArmorFields() {
    // Show the right column for armor tab (ensure both columns are visible)
    QWidget* rightColumn2 = _rightFieldsLayout->parentWidget();
    if (rightColumn2) {
        rightColumn2->show();
    }
    
    // === COLUMN 1: Armor Class and Damage Resistance ===
    
    // Armor Class
    QGroupBox* acGroup = createStandardGroupBox("Armor Class");
    QFormLayout* acLayout = static_cast<QFormLayout*>(acGroup->layout());
    
    _armorClassEdit = createSpinBox(0, 999, "Armor Class - higher values provide better protection");
    acLayout->addRow("AC:", _armorClassEdit);
    
    _leftFieldsLayout->addWidget(acGroup);
    
    // Damage Resistance
    QGroupBox* resistGroup = new QGroupBox("Damage Resistance");
    QGridLayout* resistLayout = new QGridLayout(resistGroup);
    resistLayout->setContentsMargins(8, 8, 8, 8);
    resistLayout->setSpacing(4);
    
    const QStringList damageTypes = {"Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion"};
    
    resistLayout->addWidget(new QLabel("Type"), 0, 0);
    resistLayout->addWidget(new QLabel("Resist %"), 0, 1);
    
    for (int i = 0; i < 7; ++i) {
        resistLayout->addWidget(new QLabel(damageTypes[i]), i + 1, 0);
        
        _damageResistEdits[i] = createSpinBox(0, 100, QString("Damage resistance against %1 damage").arg(damageTypes[i]));
        _damageResistEdits[i]->setSuffix("%");
        connectSpinBox(_damageResistEdits[i]);
        resistLayout->addWidget(_damageResistEdits[i], i + 1, 1);
    }
    
    _leftFieldsLayout->addWidget(resistGroup);
    
    // Armor Views (Male/Female armor preview with animation controls)
    setupArmorPreviewCompact(_leftFieldsLayout);
    
    // === COLUMN 2: Damage Threshold and Misc Properties ===
    
    // Damage Threshold
    QGroupBox* thresholdGroup = new QGroupBox("Damage Threshold");
    QGridLayout* thresholdLayout = new QGridLayout(thresholdGroup);
    thresholdLayout->setContentsMargins(8, 8, 8, 8);
    thresholdLayout->setSpacing(4);
    
    thresholdLayout->addWidget(new QLabel("Type"), 0, 0);
    thresholdLayout->addWidget(new QLabel("Threshold"), 0, 1);
    
    for (int i = 0; i < 7; ++i) {
        thresholdLayout->addWidget(new QLabel(damageTypes[i]), i + 1, 0);
        
        _damageThresholdEdits[i] = createSpinBox(0, 999, QString("Damage threshold against %1 damage").arg(damageTypes[i]));
        connectSpinBox(_damageThresholdEdits[i]);
        thresholdLayout->addWidget(_damageThresholdEdits[i], i + 1, 1);
    }
    
    _rightFieldsLayout->addWidget(thresholdGroup);
    
    // Misc Properties
    QGroupBox* miscGroup = createStandardGroupBox("Misc Properties");
    QFormLayout* miscLayout = static_cast<QFormLayout*>(miscGroup->layout());
    
    _armorPerkCombo = createComboBox({"None", "PowerArmor", "CombatArmor", "Other"}, "Special perk associated with this armor");
    miscLayout->addRow("Perk:", _armorPerkCombo);
    
    // AI Priority display
    _armorAIPriorityLabel = new QLabel("0");
    _armorAIPriorityLabel->setStyleSheet("font-weight: bold; color: #0066CC;");
    _armorAIPriorityLabel->setToolTip("AI Priority = AC + all DT values + all DR values (used by AI to select best armor)");
    miscLayout->addRow("AI Priority:", _armorAIPriorityLabel);
    
    _rightFieldsLayout->addWidget(miscGroup);
    
    // Add standard item flags
    addStandardItemFlags(_rightFieldsLayout);
    
    // Add stretch to push content to top
    _leftFieldsLayout->addStretch();
    _rightFieldsLayout->addStretch();
}

void ProEditorDialog::setupContainerFields() {
    // Show the right column for container tab (ensure both columns are visible)
    QWidget* rightColumn2 = _rightFieldsLayout->parentWidget();
    if (rightColumn2) {
        rightColumn2->show();
    }
    
    // === COLUMN 1: Container Properties ===
    
    QGroupBox* containerGroup = createStandardGroupBox("Container Properties");
    QFormLayout* containerLayout = static_cast<QFormLayout*>(containerGroup->layout());
    
    _containerMaxSizeEdit = createSpinBox(0, 999999, "Maximum size in volume units that this container can hold");
    containerLayout->addRow("Max Size:", _containerMaxSizeEdit);
    
    _leftFieldsLayout->addWidget(containerGroup);
    
    // === COLUMN 2: Container Flags ===
    
    QGroupBox* flagsGroup = new QGroupBox("Container Flags");
    QVBoxLayout* flagsLayout = new QVBoxLayout(flagsGroup);
    flagsLayout->setContentsMargins(8, 8, 8, 8);
    flagsLayout->setSpacing(4);
    
    const QStringList flagNames = {"Use", "Use On", "Look", "Talk", "Pickup"};
    for (int i = 0; i < 5; ++i) {
        _containerFlagChecks[i] = new QCheckBox(flagNames[i]);
        connectCheckBox(_containerFlagChecks[i]);
        flagsLayout->addWidget(_containerFlagChecks[i]);
    }
    
    _rightFieldsLayout->addWidget(flagsGroup);
    
    // Add stretch to push content to top
    _leftFieldsLayout->addStretch();
    _rightFieldsLayout->addStretch();
}

void ProEditorDialog::setupDrugFields() {
    // Hide the right column for drug tab to extend left column to full width
    QWidget* rightColumn2 = _rightFieldsLayout->parentWidget();
    if (rightColumn2) {
        rightColumn2->hide();
    }
    
    
    // Use loaded stat names from MSG file and add special values
    QStringList baseStatNames = _statNames;
    if (baseStatNames.isEmpty()) {
        spdlog::error("ProEditorDialog::setupDrugFields() - No stat names loaded from MSG file!");
        baseStatNames = {"Unknown"};  // Fallback if MSG loading fails
    }
    
    // === LEFT PANEL: Stat Effects ===
    
    // Stat Effects with column headers
    QGroupBox* effectsGroup = new QGroupBox("Stat Effects");
    QGridLayout* effectsGridLayout = new QGridLayout(effectsGroup);
    effectsGridLayout->setContentsMargins(8, 8, 8, 8);
    effectsGridLayout->setSpacing(6);
    
    // Header row (row 0)
    effectsGridLayout->addWidget(new QLabel(""), 0, 0);  // Empty space for stat labels
    
    QLabel* statHeader = new QLabel("Stat");
    statHeader->setAlignment(Qt::AlignCenter);
    statHeader->setStyleSheet("font-weight: bold;");
    effectsGridLayout->addWidget(statHeader, 0, 1);
    
    QLabel* immediateHeader = new QLabel("Immediate");
    immediateHeader->setAlignment(Qt::AlignCenter);
    immediateHeader->setStyleSheet("font-weight: bold;");
    effectsGridLayout->addWidget(immediateHeader, 0, 2);
    
    QLabel* midTimeHeader = new QLabel("Mid-time");
    midTimeHeader->setAlignment(Qt::AlignCenter);
    midTimeHeader->setStyleSheet("font-weight: bold;");
    effectsGridLayout->addWidget(midTimeHeader, 0, 3);
    
    QLabel* longTimeHeader = new QLabel("Long-time");
    longTimeHeader->setAlignment(Qt::AlignCenter);
    longTimeHeader->setStyleSheet("font-weight: bold;");
    effectsGridLayout->addWidget(longTimeHeader, 0, 4);
    
    // Set column stretches for proper sizing
    effectsGridLayout->setColumnStretch(0, 0);  // Fixed width for labels
    effectsGridLayout->setColumnStretch(1, 2);  // Stretch for combo boxes
    effectsGridLayout->setColumnStretch(2, 1);  // Stretch for spin boxes
    effectsGridLayout->setColumnStretch(3, 1);  // Stretch for spin boxes
    effectsGridLayout->setColumnStretch(4, 1);  // Stretch for spin boxes
    
    // Create rows for each stat (rows 1-3)
    for (int i = 0; i < NUM_DRUG_STATS; ++i) {
        int row = i + 1;
        
        // Stat label (column 0)
        QLabel* statLabel = new QLabel(QString("Stat %1:").arg(i + 1));
        effectsGridLayout->addWidget(statLabel, row, 0);
        
        // Stat dropdown (column 1)
        _drugStatCombos[i] = new QComboBox();
        QStringList statNames;
        statNames << "None";  // -1/0xFFFF
        statNames << baseStatNames;  // 0, 1, 2, ...
        _drugStatCombos[i]->addItems(statNames);
        _drugStatCombos[i]->setToolTip(QString("Stat %1 to modify (None=no effect)").arg(i + 1));
        connect(_drugStatCombos[i], QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
        effectsGridLayout->addWidget(_drugStatCombos[i], row, 1);
        
        // Immediate effect value (column 2)
        _drugStatAmountEdits[i] = new QSpinBox();
        _drugStatAmountEdits[i]->setRange(-MAX_DRUG_STAT_MODIFIER, MAX_DRUG_STAT_MODIFIER);
        _drugStatAmountEdits[i]->setToolTip(QString("Immediate effect amount for stat %1").arg(i + 1));
        connect(_drugStatAmountEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        effectsGridLayout->addWidget(_drugStatAmountEdits[i], row, 2);
        
        // Mid-time effect value (column 3)
        _drugFirstStatAmountEdits[i] = new QSpinBox();
        _drugFirstStatAmountEdits[i]->setRange(-MAX_DRUG_STAT_MODIFIER, MAX_DRUG_STAT_MODIFIER);
        _drugFirstStatAmountEdits[i]->setToolTip(QString("Mid-time effect amount for stat %1").arg(i + 1));
        connect(_drugFirstStatAmountEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        effectsGridLayout->addWidget(_drugFirstStatAmountEdits[i], row, 3);
        
        // Long-time effect value (column 4)
        _drugSecondStatAmountEdits[i] = new QSpinBox();
        _drugSecondStatAmountEdits[i]->setRange(-MAX_DRUG_STAT_MODIFIER, MAX_DRUG_STAT_MODIFIER);
        _drugSecondStatAmountEdits[i]->setToolTip(QString("Long-time effect amount for stat %1").arg(i + 1));
        connect(_drugSecondStatAmountEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
        effectsGridLayout->addWidget(_drugSecondStatAmountEdits[i], row, 4);
    }
    
    _leftFieldsLayout->addWidget(effectsGroup);
    
    // Add some spacing between stat effects and timing
    _leftFieldsLayout->addSpacing(10);
    
    // Effect Timing (moved from right panel to under Stat Effects)
    QGroupBox* timingGroup = new QGroupBox("Effect Timing");
    QFormLayout* timingLayout = new QFormLayout(timingGroup);
    timingLayout->setContentsMargins(8, 8, 8, 8);
    timingLayout->setSpacing(4);
    
    // Mid-time delay
    _drugFirstDelayEdit = new QSpinBox();
    _drugFirstDelayEdit->setRange(0, MAX_DRUG_DELAY);
    _drugFirstDelayEdit->setSuffix(" min");
    _drugFirstDelayEdit->setToolTip("Delay in game minutes before mid-time effect");
    connect(_drugFirstDelayEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    timingLayout->addRow("Mid-time Delay:", _drugFirstDelayEdit);
    
    // Long-time delay
    _drugSecondDelayEdit = new QSpinBox();
    _drugSecondDelayEdit->setRange(0, MAX_DRUG_DELAY);
    _drugSecondDelayEdit->setSuffix(" min");
    _drugSecondDelayEdit->setToolTip("Delay in game minutes before long-time effect");
    connect(_drugSecondDelayEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    timingLayout->addRow("Long-time Delay:", _drugSecondDelayEdit);
    
    _leftFieldsLayout->addWidget(timingGroup);
    
    // Add some spacing between timing and addiction
    _leftFieldsLayout->addSpacing(10);
    
    // Addiction Settings
    QGroupBox* addictionGroup = new QGroupBox("Addiction");
    QFormLayout* addictionLayout = new QFormLayout(addictionGroup);
    addictionLayout->setContentsMargins(8, 8, 8, 8);
    addictionLayout->setSpacing(4);
    
    _drugAddictionChanceEdit = new QSpinBox();
    _drugAddictionChanceEdit->setRange(0, MAX_ADDICTION_CHANCE);
    _drugAddictionChanceEdit->setSuffix("%");
    _drugAddictionChanceEdit->setToolTip("Percentage chance of addiction");
    connect(_drugAddictionChanceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    addictionLayout->addRow("Rate:", _drugAddictionChanceEdit);
    
    _drugAddictionPerkCombo = new QComboBox();
    // Use loaded perk names from MSG file directly
    if (_perkNames.isEmpty()) {
        spdlog::error("ProEditorDialog::setupDrugFields() - No perk names loaded from MSG file!");
        _drugAddictionPerkCombo->addItems({"No perk"});
    } else {
        _drugAddictionPerkCombo->addItems(_perkNames);
    }
    _drugAddictionPerkCombo->setToolTip("Perk applied when addicted");
    connect(_drugAddictionPerkCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProEditorDialog::onComboBoxChanged);
    addictionLayout->addRow("Effect:", _drugAddictionPerkCombo);
    
    // Addiction onset
    _drugAddictionDelayEdit = new QSpinBox();
    _drugAddictionDelayEdit->setRange(0, MAX_DRUG_DELAY);
    _drugAddictionDelayEdit->setSuffix(" min");
    _drugAddictionDelayEdit->setToolTip("Delay in game minutes before addiction effect is applied");
    connect(_drugAddictionDelayEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProEditorDialog::onFieldChanged);
    addictionLayout->addRow("Onset:", _drugAddictionDelayEdit);
    
    _leftFieldsLayout->addWidget(addictionGroup);
    
    // Add standard item flags
    addStandardItemFlags(_leftFieldsLayout);
    
    // Add stretch to push content to top (only left panel used for drugs)
    _leftFieldsLayout->addStretch();
}

void ProEditorDialog::setupWeaponFields() {
    // Show the right column for weapon tab (ensure both columns are visible)
    QWidget* rightColumn2 = _rightFieldsLayout->parentWidget();
    if (rightColumn2) {
        rightColumn2->show();
    }
    
    // === COLUMN 1: Basic Weapon Properties ===
    
    // Basic Properties
    QGroupBox* basicGroup = new QGroupBox("Basic Properties");
    QFormLayout* basicLayout = new QFormLayout(basicGroup);
    basicLayout->setContentsMargins(8, 8, 8, 8);
    basicLayout->setSpacing(4);
    
    _weaponAnimationCombo = createComboBox({"None", "Knife", "Club", "Hammer", "Spear", "Pistol", "SMG", "Rifle", "Big Gun", "Minigun", "Rocket Launcher"}, "Weapon animation type");
    connectComboBox(_weaponAnimationCombo);
    basicLayout->addRow("Animation:", _weaponAnimationCombo);
    
    _weaponDamageMinEdit = createSpinBox(0, 999, "Minimum damage");
    connectSpinBox(_weaponDamageMinEdit);
    basicLayout->addRow("Min Damage:", _weaponDamageMinEdit);
    
    _weaponDamageMaxEdit = createSpinBox(0, 999, "Maximum damage");
    connectSpinBox(_weaponDamageMaxEdit);
    basicLayout->addRow("Max Damage:", _weaponDamageMaxEdit);
    
    _weaponDamageTypeCombo = createComboBox({"Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion"}, "Damage type");
    connectComboBox(_weaponDamageTypeCombo);
    basicLayout->addRow("Damage Type:", _weaponDamageTypeCombo);
    
    _leftFieldsLayout->addWidget(basicGroup);
    
    // Range and Action Points
    QGroupBox* rangeGroup = new QGroupBox("Range & Action Points");
    QFormLayout* rangeLayout = new QFormLayout(rangeGroup);
    rangeLayout->setContentsMargins(8, 8, 8, 8);
    rangeLayout->setSpacing(4);
    
    _weaponRangePrimaryEdit = createSpinBox(0, 999, "Primary attack range");
    connectSpinBox(_weaponRangePrimaryEdit);
    rangeLayout->addRow("Range Primary:", _weaponRangePrimaryEdit);
    
    _weaponRangeSecondaryEdit = createSpinBox(0, 999, "Secondary attack range");
    connectSpinBox(_weaponRangeSecondaryEdit);
    rangeLayout->addRow("Range Secondary:", _weaponRangeSecondaryEdit);
    
    _weaponAPPrimaryEdit = createSpinBox(0, 99, "Action points for primary attack");
    connectSpinBox(_weaponAPPrimaryEdit);
    rangeLayout->addRow("AP Primary:", _weaponAPPrimaryEdit);
    
    _weaponAPSecondaryEdit = createSpinBox(0, 99, "Action points for secondary attack");
    connectSpinBox(_weaponAPSecondaryEdit);
    rangeLayout->addRow("AP Secondary:", _weaponAPSecondaryEdit);
    
    _leftFieldsLayout->addWidget(rangeGroup);
    
    // === COLUMN 2: Advanced Properties ===
    
    // Requirements and Projectile
    QGroupBox* reqGroup = new QGroupBox("Requirements & Projectile");
    QFormLayout* reqLayout = new QFormLayout(reqGroup);
    reqLayout->setContentsMargins(8, 8, 8, 8);
    reqLayout->setSpacing(4);
    
    _weaponMinStrengthEdit = createSpinBox(0, 10, "Minimum strength required");
    connectSpinBox(_weaponMinStrengthEdit);
    reqLayout->addRow("Min Strength:", _weaponMinStrengthEdit);
    
    _weaponProjectilePIDEdit = createSpinBox(-1, 0x0FFFFFFF, "Projectile PID (or -1 for none)");
    connectSpinBox(_weaponProjectilePIDEdit);
    reqLayout->addRow("Projectile PID:", _weaponProjectilePIDEdit);
    
    _weaponCriticalFailEdit = createSpinBox(0, 100, "Critical failure chance");
    connectSpinBox(_weaponCriticalFailEdit);
    reqLayout->addRow("Critical Fail:", _weaponCriticalFailEdit);
    
    _weaponPerkCombo = createComboBox({"None", "Fast Shot", "Long Range", "Accurate", "Penetrate", "Knockback", "Knockdown", "Flame", "Other"}, "Weapon perk");
    connectComboBox(_weaponPerkCombo);
    reqLayout->addRow("Perk:", _weaponPerkCombo);
    
    _rightFieldsLayout->addWidget(reqGroup);
    
    // Ammo and Special
    QGroupBox* ammoGroup = new QGroupBox("Ammo & Special");
    QFormLayout* ammoLayout = new QFormLayout(ammoGroup);
    ammoLayout->setContentsMargins(8, 8, 8, 8);
    ammoLayout->setSpacing(4);
    
    _weaponAmmoTypeCombo = createComboBox({"None", "Small Energy Cell", "Micro Fusion Cell", "2mm EC", ".223 FMJ", "5mm JHP", "5mm AP", ".45 Caliber", "10mm JHP", "10mm AP", "14mm AP", "Flame Fuel", "Flamethrower Fuel Mk. II", "Rocket", "Explosive Rocket", "BB's", "Small Energy Cell", "Micro Fusion Cell"}, "Ammo type");
    connectComboBox(_weaponAmmoTypeCombo);
    ammoLayout->addRow("Ammo Type:", _weaponAmmoTypeCombo);
    
    _weaponAmmoPIDEdit = createSpinBox(-1, 0x0FFFFFFF, "Ammo PID (or -1 for none)");
    connectSpinBox(_weaponAmmoPIDEdit);
    ammoLayout->addRow("Ammo PID:", _weaponAmmoPIDEdit);
    
    _weaponAmmoCapacityEdit = createSpinBox(0, 999, "Maximum ammo capacity");
    connectSpinBox(_weaponAmmoCapacityEdit);
    ammoLayout->addRow("Ammo Capacity:", _weaponAmmoCapacityEdit);
    
    _weaponBurstRoundsEdit = createSpinBox(0, 99, "Rounds per burst");
    connectSpinBox(_weaponBurstRoundsEdit);
    ammoLayout->addRow("Burst Rounds:", _weaponBurstRoundsEdit);
    
    _weaponSoundIdEdit = createSpinBox(0, 255, "Sound effect ID");
    connectSpinBox(_weaponSoundIdEdit);
    ammoLayout->addRow("Sound ID:", _weaponSoundIdEdit);
    
    _rightFieldsLayout->addWidget(ammoGroup);
    
    // Weapon Flags
    QGroupBox* flagsGroup = new QGroupBox("Weapon Flags");
    QVBoxLayout* flagsLayout = new QVBoxLayout(flagsGroup);
    flagsLayout->setContentsMargins(8, 8, 8, 8);
    flagsLayout->setSpacing(4);
    
    _weaponEnergyWeaponCheck = new QCheckBox("Energy Weapon");
    _weaponEnergyWeaponCheck->setToolTip("Mark as energy weapon (sfall extension)");
    connectCheckBox(_weaponEnergyWeaponCheck);
    flagsLayout->addWidget(_weaponEnergyWeaponCheck);
    
    _rightFieldsLayout->addWidget(flagsGroup);
    
    // Add stretch to push content to top
    _leftFieldsLayout->addStretch();
    _rightFieldsLayout->addStretch();
}

void ProEditorDialog::setupAmmoFields() {
    // TODO: Implement ammo fields - placeholder for now
    QLabel* placeholder = new QLabel("Ammo fields - coming soon");
    _leftFieldsLayout->addWidget(placeholder);
    
    // Add standard item flags
    addStandardItemFlags(_leftFieldsLayout);
    
    _leftFieldsLayout->addStretch();
    _rightFieldsLayout->addStretch();
}

void ProEditorDialog::setupMiscItemFields() {
    // TODO: Implement misc item fields - placeholder for now
    QLabel* placeholder = new QLabel("Misc item fields - coming soon");
    _leftFieldsLayout->addWidget(placeholder);
    
    // Add standard item flags
    addStandardItemFlags(_leftFieldsLayout);
    
    _leftFieldsLayout->addStretch();
    _rightFieldsLayout->addStretch();
}

void ProEditorDialog::setupKeyFields() {
    // === COLUMN 1: Key Properties ===
    
    QGroupBox* keyGroup = new QGroupBox("Key Properties");
    QFormLayout* keyLayout = new QFormLayout(keyGroup);
    keyLayout->setContentsMargins(8, 8, 8, 8);
    keyLayout->setSpacing(4);
    
    _keyIdEdit = createSpinBox(0, 999999, "Unique key identifier");
    connectSpinBox(_keyIdEdit);
    keyLayout->addRow("Key ID:", _keyIdEdit);
    
    _leftFieldsLayout->addWidget(keyGroup);
    
    // Add standard item flags
    addStandardItemFlags(_leftFieldsLayout);
    
    // Add stretch to push content to top
    _leftFieldsLayout->addStretch();
    _rightFieldsLayout->addStretch();
}

void ProEditorDialog::loadStatAndPerkNames() {
    try {
        // Load stat and perk MSG files using ResourceManager
        _statMsg = ResourceManager::getInstance().loadResource<Msg>(std::string(ResourcePaths::Msg::STAT));
        _perkMsg = ResourceManager::getInstance().loadResource<Msg>(std::string(ResourcePaths::Msg::PERK));
        
        // Load names into cached lists
        loadStatNames();
        loadPerkNames();
        
    } catch (const std::exception& e) {
        spdlog::warn("ProEditorDialog: Failed to load MSG files: {}", e.what());
        
        // Provide fallback generic names
        _statNames.clear();
        for (int i = 0; i < 38; ++i) {
            _statNames.append(QString("Stat %1").arg(i));
        }
        
        _perkNames.clear();
        _perkNames.append("No perk");
        for (int i = 1; i <= 119; ++i) {
            _perkNames.append(QString("Perk %1").arg(i));
        }
    }
}

void ProEditorDialog::loadStatNames() {
    _statNames.clear();
    
    if (!_statMsg) {
        spdlog::warn("ProEditorDialog: Stat MSG file not loaded");
        return;
    }
    
    // Load 38 stat names from indices 100-137
    for (int i = 0; i < 38; ++i) {
        try {
            const auto& message = _statMsg->message(100 + i);
            _statNames.append(QString::fromStdString(message.text));
        } catch (const std::exception& e) {
            spdlog::warn("ProEditorDialog: Failed to load stat name at index {}: {}", 100 + i, e.what());
            _statNames.append(QString("Stat %1").arg(i));
        }
    }
}

void ProEditorDialog::loadPerkNames() {
    _perkNames.clear();
    
    if (!_perkMsg) {
        spdlog::warn("ProEditorDialog: Perk MSG file not loaded");
        return;
    }
    
    try {
        // Index 100 = "No perk"
        const auto& noPerkMessage = _perkMsg->message(100);
        _perkNames.append(QString::fromStdString(noPerkMessage.text));
        
        // Indices 101-219 = actual perks (119 perks)
        for (int i = 101; i <= 219; ++i) {
            try {
                const auto& message = _perkMsg->message(i);
                _perkNames.append(QString::fromStdString(message.text));
            } catch (const std::exception& e) {
                spdlog::warn("ProEditorDialog: Failed to load perk name at index {}: {}", i, e.what());
                _perkNames.append(QString("Perk %1").arg(i - 100));
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("ProEditorDialog: Failed to load 'No perk' message: {}", e.what());
        _perkNames.append("No perk");
    }
}

void ProEditorDialog::onCritterFlagChanged() {
    if (!_pro || _pro->type() != Pro::OBJECT_TYPE::CRITTER) {
        return;
    }
    
    // Calculate combined critter flags value from all checkboxes
    uint32_t flags = 0;
    
    if (_critterBarterCheck && _critterBarterCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_BARTER);
    if (_critterNoStealCheck && _critterNoStealCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_STEAL);
    if (_critterNoDropCheck && _critterNoDropCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_DROP);
    if (_critterNoLimbsCheck && _critterNoLimbsCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_LIMBS);
    if (_critterNoAgeCheck && _critterNoAgeCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_AGE);
    if (_critterNoHealCheck && _critterNoHealCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_HEAL);
    if (_critterInvulnerableCheck && _critterInvulnerableCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_INVULNERABLE);
    if (_critterFlatCheck && _critterFlatCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_FLAT);
    if (_critterSpecialDeathCheck && _critterSpecialDeathCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_SPECIAL_DEATH);
    if (_critterLongLimbsCheck && _critterLongLimbsCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_LONG_LIMBS);
    if (_critterNoKnockbackCheck && _critterNoKnockbackCheck->isChecked()) flags = Pro::setFlag(flags, Pro::CritterFlags::CRITTER_NO_KNOCKBACK);
    
    // Update the PRO data
    _pro->critterData.flags = flags;
    
    // Also update the numeric display if it exists
    if (_critterFlagsEdit) {
        _critterFlagsEdit->setValue(static_cast<int>(flags));
    }
}

} // namespace geck