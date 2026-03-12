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
#include <QListWidget>
#include <QRadioButton>
#include <QButtonGroup>
#include <memory>
#include <unordered_map>
#include <climits>

#include "../../format/pro/Pro.h"
#include "../../format/pro/ProDataModels.h"
#include "../../format/msg/Msg.h"
#include "../widgets/ObjectPreviewWidget.h"
#include "../widgets/AnimationController.h"
#include "../widgets/ProCommonFieldsWidget.h"
#include "../widgets/pro/ProWallWidget.h"
#include "../widgets/pro/ProTileWidget.h"
#include "../widgets/pro/ProArmorWidget.h"
#include "../widgets/pro/ProWeaponWidget.h"
#include "../widgets/pro/ProDrugWidget.h"
#include "../widgets/pro/ProContainerKeyWidget.h"
#include "../widgets/pro/ProAmmoWidget.h"
#include "../widgets/pro/ProMiscItemWidget.h"

namespace geck {

namespace resource {
class GameResources;
}

/**
 * @brief PRO file editor dialog matching f2wedit functionality
 *
 * Provides comprehensive editing capabilities for PRO files with tabbed interface
 * supporting all item types: Armor, Container, Drug, Weapon, Ammo, Misc, Key
 */
class ProEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProEditorDialog(resource::GameResources& resources, std::shared_ptr<Pro> pro, QWidget* parent = nullptr);
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
    void onFrameSliderChanged(int frame);
    void onDirectionChanged(int direction);
    void onAnimationFrameChanged(int frame);
    void onObjectFlagChanged();
    void onTransparencyFlagChanged();

private:
    void setupUI();
    void setupTabs();
    void setupCompactPreview(QVBoxLayout* parentLayout);
    void setupDualPreviewCompact(QVBoxLayout* parentLayout);
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
    void clearFieldsLayouts();

    void loadProData();
    void loadCritterData();
    void loadSceneryData();
    void loadWallData();
    void loadTileData();
    void loadObjectFlags(uint32_t flags);

    void saveProData();
    void saveCritterData();
    void saveSceneryData();
    void saveWallData();
    void saveTileData();

    void updatePreview();
    void updateInventoryPreview();
    void updateGroundPreview();
    int32_t getPreviewFid();
    int32_t getInventoryFid();
    int32_t getGroundFid();
    void updateFilenameLabel();
    void openFrmSelectorForLabel(QLabel* targetLabel, int32_t* fidStorage, uint32_t objectType);
    void loadAnimationFrames();
    void updateWindowTitle();

    void loadNameAndDescription();

    QSpinBox* createSpinBox(int min, int max, const QString& tooltip = QString());
    QSpinBox* createHexSpinBox(int max, const QString& tooltip = QString());
    QComboBox* createComboBox(const QStringList& items, const QString& tooltip = QString());
    QComboBox* createMaterialComboBox(const QString& tooltip = QString());
    void connectSpinBox(QSpinBox* spinBox);
    void connectComboBox(QComboBox* comboBox);
    void connectCheckBox(QCheckBox* checkBox);

    QFormLayout* createStandardFormLayout(QWidget* parent);
    void loadIntArrayToWidgets(QSpinBox** widgets, const uint32_t* arrayValues, int count);
    QString getFrmFilename(int32_t fid);

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

    // Animation controls
    QWidget* _animationControls;
    QHBoxLayout* _animationLayout;
    QPushButton* _playPauseButton;
    QSlider* _frameSlider;
    QLabel* _frameLabel;
    QComboBox* _directionCombo;

    // Animation controller (handles timer, frames, playback state)
    AnimationController* _animationController;

    // Tabs
    QWidget* _commonTab;

    ProCommonFieldsWidget* _commonFieldsWidget;

    // Type-specific tab widgets
    ProWallWidget* _wallWidget;
    ProTileWidget* _tileWidget;
    ProArmorWidget* _armorWidget;
    ProWeaponWidget* _weaponWidget;
    ProDrugWidget* _drugWidget;
    ProContainerKeyWidget* _containerKeyWidget;
    ProAmmoWidget* _ammoWidget;
    ProMiscItemWidget* _miscItemWidget;

    // Left panel widgets
    QLabel* _nameLabel;
    QTextEdit* _descriptionEdit;
    QPushButton* _editMessageButton;
    QSpinBox* _pidEdit;
    QLineEdit* _filenameEdit;

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

    std::shared_ptr<Pro> _pro;
    resource::GameResources& _resources;

    int32_t _critterHeadFID = 0;

    // Cached stat and perk names
    QStringList _statNames;
    QVector<game::enums::EnumOption> _perkOptions;

    // Helper methods for MSG loading
    void loadStatAndPerkNames();
};

} // namespace geck
