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

#include "../../format/pro/Pro.h"
#include "../../util/ResourceManager.h"

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

private slots:
    void onAccept();
    void onFieldChanged();
    void onComboBoxChanged();
    void onCheckBoxChanged();
    void onFidSelectorClicked();
    void onInventoryFidSelectorClicked();
    void onArmorMaleFidSelectorClicked();
    void onArmorFemaleFidSelectorClicked();
    void onCritterHeadFidSelectorClicked();
    void onPlayPauseClicked();
    void onFrameChanged(int frame);
    void onDirectionChanged(int direction);
    void onAnimationTick();
    void onValidationToggleClicked();
    void onValidationItemDoubleClicked(QListWidgetItem* item);
    void onExtendedFlagChanged();
    void onObjectFlagChanged();
    void onTransparencyFlagChanged();
    void onPreviewViewChanged();

private:
    void setupUI();
    void setupTabs();
    void setupPreview();
    void setupDualPreview();
    void setupAnimationControls();
    void setupValidationPanel();
    void setupCommonTab();
    void setupObjectFlagsGroup(QFormLayout* layout);
    void setupExtendedFlagsGroup(QFormLayout* layout);
    void setupWeaponExtendedFlags(QVBoxLayout* layout);
    void setupContainerExtendedFlags(QVBoxLayout* layout);
    void setupItemExtendedFlags(QVBoxLayout* layout);
    void setupOtherExtendedFlags(QVBoxLayout* layout);
    void setupArmorTab();
    void setupContainerTab();
    void setupDrugTab();
    void setupWeaponTab();
    void setupAmmoTab();
    void setupMiscTab();
    void setupKeyTab();
    void setupCritterTab();
    void setupSceneryTab();
    void setupWallTab();
    void setupTileTab();
    
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
    
    void updateTabVisibility();
    void validateField(QWidget* field);
    void updatePreview();
    void updateInventoryPreview();
    void updateGroundPreview();  
    int32_t getPreviewFid();
    int32_t getInventoryFid();
    int32_t getGroundFid();
    void openFrmSelector(QSpinBox* targetField, uint32_t objectType);
    void openFrmSelectorForLabel(QLabel* targetLabel, int32_t* fidStorage, uint32_t objectType);
    void loadAnimationFrames();
    
    // FRM thumbnail generation (based on ObjectPalettePanel approach)
    QPixmap createFrmThumbnail(const std::string& frmPath, const QSize& targetSize = QSize(250, 250));
    QPixmap createFrameThumbnail(const class Frame& frame, const class Pal* palette, const QSize& targetSize = QSize(250, 250));
    
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
    
    // Material names mapping
    static const QStringList getMaterialNames();
    
    // FID to FRM filename conversion
    QString getFrmFilename(int32_t fid);
    
    // Extended PRO data structures
    struct CommonData {
        int32_t PID;
        uint32_t message_id;
        int32_t FID;
        uint32_t light_distance;
        uint32_t light_intensity;
        uint32_t flags;
        uint32_t flagsExt;
        uint32_t SID;
        uint32_t materialId;
        uint32_t containerSize;
        uint32_t weight;
        uint32_t basePrice;
        int32_t inventoryFID;
        uint8_t soundId;
    };
    
    struct ArmorData {
        uint32_t armorClass;
        uint32_t damageResist[7];     // Normal, Laser, Fire, Plasma, Electrical, EMP, Explosion
        uint32_t damageThreshold[7];
        uint32_t perk;
        int32_t armorMaleFID;
        int32_t armorFemaleFID;
    };
    
    struct ContainerData {
        uint32_t maxSize;
        uint32_t flags;  // Use, UseOn, Look, Talk, Pickup flags
    };
    
    struct DrugData {
        uint32_t stat0Base, stat1Base, stat2Base;
        int32_t stat0Amount, stat1Amount, stat2Amount;
        uint32_t firstDelayMinutes;
        int32_t firstStat0Amount, firstStat1Amount, firstStat2Amount;
        uint32_t secondDelayMinutes;
        int32_t secondStat0Amount, secondStat1Amount, secondStat2Amount;
        uint32_t addictionChance;
        uint32_t addictionPerk;
        uint32_t addictionDelay;
    };
    
    struct WeaponData {
        uint32_t animationCode;
        uint32_t damageMin, damageMax;
        uint32_t damageType;
        uint32_t rangePrimary, rangeSecondary;
        int32_t projectilePID;
        uint32_t minimumStrength;
        uint32_t actionCostPrimary, actionCostSecondary;
        uint32_t criticalFail;
        uint32_t perk;
        uint32_t burstRounds;
        uint32_t ammoType;
        int32_t ammoPID;
        uint32_t ammoCapacity;
        uint8_t soundId;
    };
    
    struct AmmoData {
        uint32_t caliber;
        uint32_t quantity;
        int32_t damageModifier;
        int32_t damageResistModifier;
        int32_t damageMultiplier;
        int32_t damageTypeModifier;
    };
    
    struct MiscData {
        uint32_t powerType;
        uint32_t charges;
    };
    
    struct KeyData {
        uint32_t keyId;
    };
    
    struct CritterData {
        uint32_t headFID;
        uint32_t aiPacket;
        uint32_t teamNumber;
        uint32_t flags;
        // SPECIAL stats (7 stats: STR, PER, END, CHR, INT, AGL, LCK)
        uint32_t specialStats[7];
        uint32_t maxHitPoints;
        uint32_t actionPoints;
        uint32_t armorClass;
        uint32_t unused;
        uint32_t meleeDamage;
        uint32_t carryWeightMax;
        uint32_t sequence;
        uint32_t healingRate;
        uint32_t criticalChance;
        uint32_t betterCriticals;
        // Damage threshold (7 damage types)
        uint32_t damageThreshold[7];
        // Damage resist (9 damage types)
        uint32_t damageResist[9];
        uint32_t age;
        uint32_t gender;
        // Bonus SPECIAL stats (7 stats)
        uint32_t bonusSpecialStats[7];
        uint32_t bonusHealthPoints;
        uint32_t bonusActionPoints;
        uint32_t bonusArmorClass;
        uint32_t bonusUnused;
        uint32_t bonusMeleeDamage;
        uint32_t bonusCarryWeight;
        uint32_t bonusSequence;
        uint32_t bonusHealingRate;
        uint32_t bonusCriticalChance;
        uint32_t bonusBetterCriticals;
        // Bonus damage threshold (8 values)
        uint32_t bonusDamageThreshold[8];
        // Bonus damage resistance (8 values)
        uint32_t bonusDamageResistance[8];
        uint32_t bonusAge;
        uint32_t bonusGender;
        // Skills (18 different skills)
        uint32_t skills[18];
        uint32_t bodyType;
        uint32_t experienceForKill;
        uint32_t killType;
        uint32_t damageType; // Optional field
    };
    
    struct SceneryData {
        uint32_t materialId;
        uint8_t soundId;
        // Door-specific data
        struct {
            uint32_t walkThroughFlag;
            uint32_t unknownField;
        } doorData;
        // Stairs-specific data
        struct {
            uint32_t destTile;
            uint32_t destElevation;
        } stairsData;
        // Elevator-specific data
        struct {
            uint32_t elevatorType;
            uint32_t elevatorLevel;
        } elevatorData;
        // Ladder-specific data
        struct {
            uint32_t destTileAndElevation;
        } ladderData;
        // Generic-specific data
        struct {
            uint32_t unknownField;
        } genericData;
    };

    // UI Components
    QVBoxLayout* _mainLayout;
    QHBoxLayout* _contentLayout;
    QTabWidget* _tabWidget;
    QDialogButtonBox* _buttonBox;
    
    // Validation summary panel
    QWidget* _validationPanel;
    QVBoxLayout* _validationLayout;
    QGroupBox* _validationGroup;
    QListWidget* _validationList;
    QPushButton* _validationToggleButton;
    QLabel* _validationStatusLabel;
    bool _validationPanelVisible;
    
    // Preview panel
    QWidget* _previewPanel;
    QVBoxLayout* _previewLayout;
    QGroupBox* _previewGroup;
    QLabel* _previewLabel;
    
    // Dual preview system
    QWidget* _dualPreviewWidget;
    QHBoxLayout* _dualPreviewLayout;
    QGroupBox* _inventoryPreviewGroup;
    QGroupBox* _groundPreviewGroup;
    QLabel* _inventoryPreviewLabel;
    QLabel* _groundPreviewLabel;
    
    // Animation controls
    QWidget* _animationControls;
    QHBoxLayout* _animationLayout;
    QPushButton* _playPauseButton;
    QSlider* _frameSlider;
    QLabel* _frameLabel;
    QComboBox* _directionCombo;
    QTimer* _animationTimer;
    
    // Animation state
    int _currentFrame;
    int _currentDirection;
    int _totalFrames;
    int _totalDirections;
    bool _isAnimating;
    std::vector<QPixmap> _frameCache;
    
    // Validation system
    enum class ValidationLevel {
        INFO,
        WARNING,
        ERROR
    };
    
    struct ValidationIssue {
        QWidget* field;
        QString message;
        ValidationLevel level;
        QString category;
    };
    
    std::vector<ValidationIssue> _validationIssues;
    void addValidationIssue(QWidget* field, const QString& message, ValidationLevel level, const QString& category = "General");
    void clearValidationIssues(QWidget* field = nullptr);
    bool validateFIDReference(int32_t fid, const QString& context);
    bool validateStatValue(int value, int min, int max, const QString& statName);
    void updateValidationStatus();
    void updateValidationPanel();
    
    // AI Priority calculations (f2wedit feature)
    int calculateArmorAIPriority();
    int calculateWeaponAIPriority();
    void updateAIPriorityDisplays();
    
    // Tabs
    QWidget* _commonTab;
    QWidget* _armorTab;
    QWidget* _containerTab;
    QWidget* _drugTab;
    QWidget* _weaponTab;
    QWidget* _ammoTab;
    QWidget* _miscTab;
    QWidget* _keyTab;
    QWidget* _critterTab;
    QWidget* _sceneryTab;
    QWidget* _wallTab;
    QWidget* _tileTab;
    
    // Common tab controls
    QLabel* _nameLabel;
    QTextEdit* _descriptionLabel;
    QSpinBox* _pidEdit;
    QSpinBox* _messageIdEdit;
    QLabel* _fidLabel;
    QPushButton* _fidSelectorButton;
    QSpinBox* _lightDistanceEdit;
    QSpinBox* _lightIntensityEdit;
    QSpinBox* _flagsEdit;
    
    // Basic object flag checkboxes
    QCheckBox* _flatCheck;           // 0x00000008 - Flat (rendered first, just after tiles)
    QCheckBox* _noBlockCheck;        // 0x00000010 - NoBlock (doesn't block the tile)
    QCheckBox* _multiHexCheck;       // 0x00000800 - MultiHex
    QCheckBox* _noHighlightCheck;    // 0x00001000 - No Highlight (doesn't highlight border; used for containers)
    QCheckBox* _transRedCheck;       // 0x00004000 - TransRed
    QCheckBox* _transNoneCheck;      // 0x00008000 - TransNone (opaque)
    QCheckBox* _transWallCheck;      // 0x00010000 - TransWall  
    QCheckBox* _transGlassCheck;     // 0x00020000 - TransGlass
    QCheckBox* _transSteamCheck;     // 0x00040000 - TransSteam
    QCheckBox* _transEnergyCheck;    // 0x00080000 - TransEnergy
    QCheckBox* _wallTransEndCheck;   // 0x10000000 - WallTransEnd (changes transparency egg logic)
    QCheckBox* _lightThruCheck;      // 0x20000000 - LightThru
    QCheckBox* _shootThruCheck;      // 0x80000000 - ShootThru
    
    // Extended flags controls - organized by category
    QGroupBox* _extendedFlagsGroup;
    QSpinBox* _animationPrimaryEdit;
    QSpinBox* _animationSecondaryEdit;
    QCheckBox* _bigGunCheck;
    QCheckBox* _twoHandedCheck;
    QCheckBox* _canUseCheck;
    QCheckBox* _canUseOnCheck;
    QCheckBox* _generalFlagCheck;
    QCheckBox* _interactionFlagCheck;
    QCheckBox* _itemHiddenCheck;
    QCheckBox* _lightFlag1Check;
    QCheckBox* _lightFlag2Check;
    QCheckBox* _lightFlag3Check;
    QCheckBox* _lightFlag4Check;
    QSpinBox* _sidEdit;
    QComboBox* _materialIdEdit;
    QSpinBox* _containerSizeEdit;
    QSpinBox* _weightEdit;
    QSpinBox* _basePriceEdit;
    QLabel* _inventoryFIDLabel;
    QPushButton* _inventoryFIDSelectorButton;
    QSpinBox* _soundIdEdit;
    
    // Armor tab controls
    QSpinBox* _armorClassEdit;
    QSpinBox* _damageResistEdits[7];
    QSpinBox* _damageThresholdEdits[7];
    QComboBox* _armorPerkCombo;
    QLabel* _armorMaleFIDLabel;
    QPushButton* _armorMaleFIDSelectorButton;
    QLabel* _armorFemaleFIDLabel;
    QPushButton* _armorFemaleFIDSelectorButton;
    QLabel* _armorAIPriorityLabel;  // Read-only AI priority calculation display
    
    // Container tab controls
    QSpinBox* _containerMaxSizeEdit;
    QCheckBox* _containerFlagChecks[5];  // Use, UseOn, Look, Talk, Pickup
    
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
    QCheckBox* _weaponEnergyWeaponCheck;  // Energy weapon flag (sfall 4.2/3.8.20 feature)
    QLabel* _weaponAIPriorityLabel;  // Read-only AI priority calculation display
    
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
    
    // Wall tab controls
    QComboBox* _wallMaterialIdEdit;
    
    // Tile tab controls
    QComboBox* _tileMaterialIdEdit;
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
    CommonData _commonData;
    
    // Internal FID storage (since labels only display text)
    int32_t _mainFID = 0;           // Ground/main FID
    int32_t _inventoryFID = 0;
    int32_t _armorMaleFID = 0;
    int32_t _armorFemaleFID = 0;
    int32_t _critterHeadFID = 0;
    ArmorData _armorData;
    ContainerData _containerData;
    DrugData _drugData;
    WeaponData _weaponData;
    AmmoData _ammoData;
    MiscData _miscData;
    KeyData _keyData;
    CritterData _critterData;
    SceneryData _sceneryData;
};

} // namespace geck