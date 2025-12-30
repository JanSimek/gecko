#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QListWidget>
#include <QRadioButton>
#include <QButtonGroup>
#include <memory>
#include <unordered_map>
#include <climits>

#include "../../format/pro/Pro.h"
#include "../../format/pro/ProDataModels.h"
#include "../../format/msg/Msg.h"
#include "../../util/ResourceManager.h"
#include "../widgets/ObjectPreviewWidget.h"
#include "../widgets/ProCommonFieldsWidget.h"
#include "../widgets/pro/ProWallWidget.h"
#include "../widgets/pro/ProTileWidget.h"
#include "../widgets/pro/ProArmorWidget.h"
#include "../widgets/pro/ProWeaponWidget.h"
#include "../widgets/pro/ProDrugWidget.h"
#include "../widgets/pro/ProContainerKeyWidget.h"

namespace geck {

/**
 * @brief PRO file editor dialog matching f2wedit functionality
 *
 * Provides comprehensive editing capabilities for PRO files with tabbed interface
 * supporting all item types: Armor, Container, Drug, Weapon, Ammo, Misc, Key
 */
class ProEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProEditorDialog(std::shared_ptr<Pro> pro, QWidget* parent = nullptr);
    ~ProEditorDialog() = default;

    std::shared_ptr<Pro> getModifiedPro() const { return _pro; }

    // UI Size Constants
    static constexpr int PREVIEW_COMPACT_WIDTH = 180;
    static constexpr int PREVIEW_COMPACT_HEIGHT = 150;
    static constexpr int PREVIEW_MIN_HEIGHT = 200;
    static constexpr int PREVIEW_ITEM_SIZE = 120;
    static constexpr int PREVIEW_FULL_MIN_HEIGHT = 150;
    static constexpr int PREVIEW_FULL_MAX_HEIGHT = 200;
    static constexpr int PREVIEW_FULL_MIN_WIDTH = 150;

    static constexpr int BUTTON_MAX_WIDTH = 24;
    static constexpr int BUTTON_MAX_HEIGHT = 20;
    static constexpr int DIRECTION_COMBO_MAX_WIDTH = 50;
    static constexpr int LAYOUT_SPACING = 10;

    // Game Constants (Documented Engine Limits Only)
    static constexpr int MAX_SPECIAL_STAT = 10; // Wiki: Primary/Secondary Stats "1-10"
    static constexpr int MIN_SPECIAL_STAT = 1;
    static constexpr int MAX_SKILL_PERCENT = 300; // Wiki: Skills "0-300"
    static constexpr int MAX_AGE = 99;            // Wiki: Age "1-99"
    static constexpr int MIN_AGE = 1;
    static constexpr int MAX_LIGHT_RADIUS = 8;        // Wiki: Light radius "0..8 (hexes)"
    static constexpr int MAX_LIGHT_INTENSITY = 65536; // Wiki: Light intensity "0..65536"
    static constexpr int MAX_SOUND_ID = 255;          // 8-bit field limit

    // Gender Constants (Wiki documented)
    static constexpr int GENDER_MALE = 0; // Wiki: "0-male, 1-female"
    static constexpr int GENDER_FEMALE = 1;
    static constexpr int BONUS_GENDER_MIN = -1; // For bonus calculations
    static constexpr int BONUS_GENDER_MAX = 1;

    // Array Size Constants
    static constexpr int NUM_SPECIAL_STATS = 7;
    static constexpr int NUM_DAMAGE_TYPES = 7;
    static constexpr int NUM_DAMAGE_TYPES_WITH_COMBAT = 8;
    static constexpr int NUM_DAMAGE_TYPES_WITH_SPECIAL = 8;
    static constexpr int NUM_DAMAGE_TYPES_WITH_RADIATION = 9;
    static constexpr int NUM_SKILLS = 18;
    static constexpr int NUM_DRUG_STATS = 3;
    static constexpr int NUM_COMMON_FLAGS = 13;
    static constexpr int DIRECTIONS_COUNT = 6;

    // Other Constants
    static constexpr int BURST_ROUND_PRIORITY_MULTIPLIER = 3;

private slots:
    void onAccept();
    void onFieldChanged();
    void onComboBoxChanged();
    void onCheckBoxChanged();
    void onFidSelectorClicked();
    void onEditMessageClicked();
    void onInventoryFidSelectorClicked();
    void onCritterHeadFidSelectorClicked();
    void onCritterFlagChanged();
    void onObjectFidChangeRequested();
    void onObjectFidChanged(int32_t newFid);
    void onPlayPauseClicked();
    void onFrameChanged(int frame);
    void onDirectionChanged(int direction);
    void onAnimationTick();
    void onObjectFlagChanged();
    void onTransparencyFlagChanged();

private:
    void setupUI();
    void setupTabs();
    void setupCompactPreview(QVBoxLayout* parentLayout);
    void setupCompactAnimationControls(QVBoxLayout* parentLayout);
    void setupCompactArmorAnimationControls(QVBoxLayout* parentLayout);
    void setupDualPreviewCompact(QVBoxLayout* parentLayout);
    void setupArmorPreviewCompact(QVBoxLayout* parentLayout);
    void setupItemFields();
    void setupArmorFields();
    void setupContainerFields();
    void setupDrugFields();
    void setupWeaponFields();
    void setupAmmoFields();
    void setupMiscItemFields();
    void setupKeyFields();
    void setupCritterFields();
    void setupSceneryFields();
    void setupMiscFields();
    void setupAnimationControls();
    void setupCommonTab();
    void setupTypeSpecificTabs();
    void setupItemTabs();
    void setupArmorTab();
    void setupDrugTab();
    void setupWeaponTab();
    void setupAmmoMiscTab();
    void setupContainerKeyTab();
    void setupCritterTab();
    void setupCritterStatsTab(QTabWidget* parentTabs);
    void setupCritterDefenceTab(QTabWidget* parentTabs);
    void setupCritterGeneralTab(QTabWidget* parentTabs);
    void setupSceneryTab();
    void setupWallTab();
    void setupTileTab();
    void setupMiscTab();
    void setupObjectFlagsGroup(QFormLayout* layout);
    void setupExtendedFlagsGroup(QFormLayout* layout);
    void setupWeaponExtendedFlags(QVBoxLayout* layout);
    void setupContainerExtendedFlags(QVBoxLayout* layout);
    void setupItemExtendedFlags(QVBoxLayout* layout);
    void setupOtherExtendedFlags(QVBoxLayout* layout);
    void addStandardItemFlags(QVBoxLayout* parentLayout);

    void loadProData();
    void loadArmorData();
    void loadContainerData();
    void loadDrugData();
    void loadWeaponData();
    void loadAmmoData();
    void loadMiscData();
    void loadKeyData();
    void loadCritterData();
    void loadSceneryData();
    void loadWallData();
    void loadTileData();
    void loadObjectFlags(uint32_t flags);

    void saveProData();
    void saveArmorData();
    void saveContainerData();
    void saveDrugData();
    void saveWeaponData();
    void saveAmmoData();
    void saveMiscData();
    void saveKeyData();
    void saveCritterData();
    void saveSceneryData();
    void saveWallData();
    void saveTileData();

    void updatePreview();
    void updateInventoryPreview();
    void updateGroundPreview();
    void updateArmorPreview();
    int32_t getPreviewFid();
    int32_t getInventoryFid();
    int32_t getGroundFid();
    void updateFilenameLabel();
    void openFrmSelector(QSpinBox* targetField, uint32_t objectType);
    void openFrmSelectorForLabel(QLabel* targetLabel, int32_t* fidStorage, uint32_t objectType);
    void loadAnimationFrames();
    void updateWindowTitle();

    // MSG file loading for name and description
    void loadNameAndDescription();

    // Helper methods to reduce code duplication
    QSpinBox* createSpinBox(int min, int max, const QString& tooltip = QString());
    QSpinBox* createHexSpinBox(int max, const QString& tooltip = QString());
    QComboBox* createComboBox(const QStringList& items, const QString& tooltip = QString());
    QComboBox* createMaterialComboBox(const QString& tooltip = QString());
    void connectSpinBox(QSpinBox* spinBox);
    void connectComboBox(QComboBox* comboBox);
    void connectCheckBox(QCheckBox* checkBox);

    // Layout helper methods (DRY principle)
    QFormLayout* createStandardFormLayout(QWidget* parent);
    QGroupBox* createStandardGroupBox(const QString& title);
    QHBoxLayout* createTwoColumnLayout(QWidget* parent);

    // Widget array helper methods (DRY principle)
    void loadIntArrayToWidgets(QSpinBox** widgets, const uint32_t* arrayValues, int count);
    void saveWidgetsToIntArray(QSpinBox** widgets, uint32_t* arrayValues, int count);
    QSpinBox** createConnectedSpinBoxArray(int count, int min, int max, const QStringList& tooltips);

    // FID to FRM filename conversion
    QString getFrmFilename(int32_t fid);

    // Note: CommonData is now handled by ProCommonFieldsWidget
    // Data structures have been moved to ProDataModels.h

    // UI Components
    QVBoxLayout* _mainLayout;
    QHBoxLayout* _contentLayout;
    QTabWidget* _tabWidget;
    QDialogButtonBox* _buttonBox;

    // Temporary layout pointers for tab content (used during setup)
    QVBoxLayout* _leftFieldsLayout;
    QVBoxLayout* _rightFieldsLayout;

    // Preview panel (for items only)
    QGroupBox* _previewGroup;
    QLabel* _previewLabel;

    // Object preview widget
    ObjectPreviewWidget* _objectPreviewWidget;

    // Dual preview system
    QWidget* _dualPreviewWidget;
    QHBoxLayout* _dualPreviewLayout;
    ObjectPreviewWidget* _inventoryPreviewWidget;
    ObjectPreviewWidget* _groundPreviewWidget;

    // Armor preview system
    QGroupBox* _armorPreviewGroup;
    ObjectPreviewWidget* _armorMalePreviewWidget;
    ObjectPreviewWidget* _armorFemalePreviewWidget;

    // Animation controls (non-critter types still use old system)
    QWidget* _animationControls;
    QHBoxLayout* _animationLayout;
    QPushButton* _playPauseButton;
    QSlider* _frameSlider;
    QLabel* _frameLabel;
    QComboBox* _directionCombo;
    QTimer* _animationTimer;

    // Animation state (non-critter types)
    int _currentFrame;
    int _currentDirection;
    int _totalFrames;
    int _totalDirections;
    bool _isAnimating;
    std::vector<QPixmap> _frameCache;

    // AI Priority calculations (f2wedit feature)
    int calculateArmorAIPriority();
    int calculateWeaponAIPriority();
    void updateAIPriorityDisplays();

    // Tabs
    QWidget* _commonTab;

    // Common fields widget (replaces individual common field controls)
    ProCommonFieldsWidget* _commonFieldsWidget;

    // Type-specific tab widgets (refactored)
    ProWallWidget* _wallWidget;
    ProTileWidget* _tileWidget;
    ProArmorWidget* _armorWidget;
    ProWeaponWidget* _weaponWidget;
    ProDrugWidget* _drugWidget;
    ProContainerKeyWidget* _containerKeyWidget;

    // Left panel widgets (name, preview, description, PID)
    QLabel* _nameLabel;
    QTextEdit* _descriptionEdit;
    QPushButton* _editMessageButton;
    QSpinBox* _pidEdit;
    QLineEdit* _filenameEdit;

    // Note: Common object flags, extended flags, and item-specific fields
    // are now handled by ProCommonFieldsWidget

    // Armor tab controls
    QSpinBox* _armorClassEdit;
    QSpinBox* _damageResistEdits[7];
    QSpinBox* _damageThresholdEdits[7];
    QComboBox* _armorPerkCombo;
    QLabel* _armorMaleFIDLabel;
    QPushButton* _armorMaleFIDSelectorButton;
    QLabel* _armorFemaleFIDLabel;
    QPushButton* _armorFemaleFIDSelectorButton;
    QLabel* _armorAIPriorityLabel; // Read-only AI priority calculation display

    // Container tab controls
    QSpinBox* _containerMaxSizeEdit;
    QCheckBox* _containerFlagChecks[5]; // Use, UseOn, Look, Talk, Pickup

    // Drug tab controls
    QComboBox* _drugStatCombos[3];
    QSpinBox* _drugStatAmountEdits[3];
    QSpinBox* _drugFirstDelayEdit;
    QSpinBox* _drugFirstStatAmountEdits[3];
    QSpinBox* _drugSecondDelayEdit;
    QSpinBox* _drugSecondStatAmountEdits[3];
    QSpinBox* _drugAddictionChanceEdit;
    QComboBox* _drugAddictionPerkCombo;
    QSpinBox* _drugAddictionDelayEdit;

    // Weapon tab controls
    QComboBox* _weaponAnimationCombo;
    QSpinBox* _weaponDamageMinEdit;
    QSpinBox* _weaponDamageMaxEdit;
    QComboBox* _weaponDamageTypeCombo;
    QSpinBox* _weaponRangePrimaryEdit;
    QSpinBox* _weaponRangeSecondaryEdit;
    QSpinBox* _weaponProjectilePIDEdit;
    QSpinBox* _weaponMinStrengthEdit;
    QSpinBox* _weaponAPPrimaryEdit;
    QSpinBox* _weaponAPSecondaryEdit;
    QSpinBox* _weaponCriticalFailEdit;
    QComboBox* _weaponPerkCombo;
    QSpinBox* _weaponBurstRoundsEdit;
    QComboBox* _weaponAmmoTypeCombo;
    QSpinBox* _weaponAmmoPIDEdit;
    QSpinBox* _weaponAmmoCapacityEdit;
    QSpinBox* _weaponSoundIdEdit;
    QCheckBox* _weaponEnergyWeaponCheck; // Energy weapon flag (sfall 4.2/3.8.20 feature)
    QLabel* _weaponAIPriorityLabel;      // Read-only AI priority calculation display

    // Ammo tab controls
    QComboBox* _ammoCaliberCombo;
    QSpinBox* _ammoQuantityEdit;
    QSpinBox* _ammoDamageModEdit;
    QSpinBox* _ammoDRModEdit;
    QSpinBox* _ammoDamageMultEdit;
    QComboBox* _ammoDamageTypeModCombo;

    // Misc tab controls
    QComboBox* _miscPowerTypeCombo;
    QSpinBox* _miscChargesEdit;

    // Key tab controls
    QSpinBox* _keyIdEdit;

    // Critter tab controls
    QLabel* _critterHeadFIDLabel;
    QPushButton* _critterHeadFIDSelectorButton;
    QSpinBox* _critterAIPacketEdit;
    QSpinBox* _critterTeamNumberEdit;
    QSpinBox* _critterFlagsEdit;

    // Critter flag checkboxes
    QCheckBox* _critterBarterCheck;
    QCheckBox* _critterNoStealCheck;
    QCheckBox* _critterNoDropCheck;
    QCheckBox* _critterNoLimbsCheck;
    QCheckBox* _critterNoAgeCheck;
    QCheckBox* _critterNoHealCheck;
    QCheckBox* _critterInvulnerableCheck;
    QCheckBox* _critterNoFlattenCheck;
    QCheckBox* _critterSpecialDeathCheck;
    QCheckBox* _critterLongLimbsCheck;
    QCheckBox* _critterNoKnockbackCheck;
    QSpinBox* _critterSpecialStatEdits[7]; // STR, PER, END, CHR, INT, AGL, LCK
    QSpinBox* _critterMaxHitPointsEdit;
    QSpinBox* _critterActionPointsEdit;
    QSpinBox* _critterArmorClassEdit;
    QSpinBox* _critterMeleeDamageEdit;
    QSpinBox* _critterCarryWeightMaxEdit;
    QSpinBox* _critterSequenceEdit;
    QSpinBox* _critterHealingRateEdit;
    QSpinBox* _critterCriticalChanceEdit;
    QSpinBox* _critterBetterCriticalsEdit;
    QSpinBox* _critterDamageThresholdEdits[7];
    QSpinBox* _critterDamageResistEdits[9];
    QSpinBox* _critterAgeEdit;

    // Bonus stat controls
    QSpinBox* _critterBonusSpecialStatEdits[7]; // Bonus STR, PER, END, CHR, INT, AGL, LCK
    QSpinBox* _critterBonusHealthPointsEdit;
    QSpinBox* _critterBonusActionPointsEdit;
    QSpinBox* _critterBonusArmorClassEdit;
    QSpinBox* _critterBonusMeleeDamageEdit;
    QSpinBox* _critterBonusCarryWeightEdit;
    QSpinBox* _critterBonusSequenceEdit;
    QSpinBox* _critterBonusHealingRateEdit;
    QSpinBox* _critterBonusCriticalChanceEdit;
    QSpinBox* _critterBonusBetterCriticalsEdit;
    QSpinBox* _critterBonusDamageThresholdEdits[8];
    QSpinBox* _critterBonusDamageResistanceEdits[8];
    QSpinBox* _critterBonusAgeEdit;
    QSpinBox* _critterBonusGenderEdit;
    QComboBox* _critterGenderCombo;
    QSpinBox* _critterSkillEdits[18];
    QComboBox* _critterBodyTypeCombo;
    QSpinBox* _critterExperienceEdit;
    QSpinBox* _critterKillTypeEdit;
    QSpinBox* _critterDamageTypeEdit;

    // Scenery tab controls
    QComboBox* _sceneryMaterialIdEdit;

    // Wall tab controls - moved to ProWallWidget

    // Tile tab controls - moved to ProTileWidget
    QSpinBox* _scenerySoundIdEdit;
    QComboBox* _sceneryTypeCombo;
    // Door controls
    QCheckBox* _doorWalkThroughCheck;
    QSpinBox* _doorUnknownEdit;
    // Stairs controls
    QSpinBox* _stairsDestTileEdit;
    QSpinBox* _stairsDestElevationEdit;
    // Elevator controls
    QSpinBox* _elevatorTypeEdit;
    QSpinBox* _elevatorLevelEdit;
    // Ladder controls
    QSpinBox* _ladderDestTileElevationEdit;
    // Generic controls
    QSpinBox* _genericUnknownEdit;

    // Data
    std::shared_ptr<Pro> _pro;
    // Note: Common data and main/inventory FIDs are now handled by ProCommonFieldsWidget

    // Internal FID storage for type-specific fields
    int32_t _armorMaleFID = 0;
    int32_t _armorFemaleFID = 0;
    int32_t _critterHeadFID = 0;
    ProArmorData _armorData;
    ProContainerData _containerData;
    ProDrugData _drugData;
    ProWeaponData _weaponData;
    ProAmmoData _ammoData;
    ProMiscData _miscData;
    ProKeyData _keyData;
    ProCritterData _critterData;
    ProSceneryData _sceneryData;

    // MSG files for stat and perk names
    Msg* _statMsg = nullptr;
    Msg* _perkMsg = nullptr;

    // Cached stat and perk names
    QStringList _statNames;
    QStringList _perkNames;

    // Helper methods for MSG loading
    void loadStatAndPerkNames();
    void loadStatNames();
    void loadPerkNames();
};

} // namespace geck
