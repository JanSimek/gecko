#include "ProCommonFieldsWidget.h"
#include "../../util/ResourceManager.h"
#include "../../util/ProHelper.h"
#include "../theme/ThemeManager.h"
#include "../GameEnums.h"
#include "../UIConstants.h"
#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <spdlog/spdlog.h>

namespace geck {

ProCommonFieldsWidget::ProCommonFieldsWidget(QWidget* parent)
    : QWidget(parent)
    , _mainLayout(nullptr)
    , _lightingGroup(nullptr)
    , _objectFlagsGroup(nullptr)
    , _extendedFlagsGroup(nullptr)
    , _itemFieldsGroup(nullptr) {
    setupUI();
}

void ProCommonFieldsWidget::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setSpacing(ui::constants::SPACING_NORMAL);
    _mainLayout->setContentsMargins(ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN);

    QHBoxLayout* columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(ui::constants::SPACING_NORMAL);

    // === LEFT COLUMN ===
    QVBoxLayout* leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(ui::constants::SPACING_FORM);

    // Item Properties Group (for items only)
    _itemFieldsGroup = new QGroupBox("Item Properties", this);
    _itemFieldsGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    auto itemLayout = new QFormLayout(_itemFieldsGroup);
    itemLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN_VERTICAL, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    setupItemFields(itemLayout);
    leftColumn->addWidget(_itemFieldsGroup);
    _itemFieldsGroup->setVisible(false);

    // Object Flags Group
    _objectFlagsGroup = new QGroupBox("Object Flags", this);
    _objectFlagsGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    auto flagsLayout = new QFormLayout(_objectFlagsGroup);
    flagsLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN_VERTICAL, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    setupObjectFlags(flagsLayout);
    leftColumn->addWidget(_objectFlagsGroup);

    leftColumn->addStretch();

    // === RIGHT COLUMN ===
    QVBoxLayout* rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(ui::constants::SPACING_FORM);

    // Lighting & Transparency Group
    _lightingGroup = new QGroupBox("Lighting & Transparency", this);
    _lightingGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    auto lightingLayout = new QFormLayout(_lightingGroup);
    lightingLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN_VERTICAL, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    setupLightingFields(lightingLayout);
    rightColumn->addWidget(_lightingGroup);

    // Animation Control Group
    _extendedFlagsGroup = new QGroupBox("Animation Control", this);
    _extendedFlagsGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    auto extFlagsLayout = new QFormLayout(_extendedFlagsGroup);
    extFlagsLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN_VERTICAL, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    setupExtendedFlags(extFlagsLayout);
    rightColumn->addWidget(_extendedFlagsGroup);

    rightColumn->addStretch();

    // Add columns to main layout
    columnsLayout->addLayout(leftColumn, 1);
    columnsLayout->addLayout(rightColumn, 1);
    _mainLayout->addLayout(columnsLayout);
}

void ProCommonFieldsWidget::setupLightingFields(QFormLayout* layout) {
    // Lighting controls
    _lightingCheck = new QCheckBox("Has Lighting", this);
    _lightingCheck->setToolTip("Object provides lighting effects");
    connectCheckBox(_lightingCheck);
    layout->addRow(_lightingCheck);

    _lightRadiusEdit = createSpinBox(0, MAX_LIGHT_RADIUS, "Light radius in hexes (0-8)");
    connectSpinBox(_lightRadiusEdit);
    layout->addRow("Radius (hexes):", _lightRadiusEdit);

    _lightIntensityEdit = createSpinBox(0, MAX_LIGHT_INTENSITY, "Light intensity (0-65536, interpreted as 0-100%)");
    connectSpinBox(_lightIntensityEdit);
    layout->addRow("Intensity:", _lightIntensityEdit);

    // Add separator
    auto separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addRow(separator);

    // Transparency options
    auto transLabel = new QLabel("<b>Transparency Type:</b>");
    layout->addRow(transLabel);

    _transNoneCheck = new QCheckBox("Opaque", this);
    connectCheckBox(_transNoneCheck);
    layout->addRow(_transNoneCheck);

    _transWallCheck = new QCheckBox("Wall", this);
    connectCheckBox(_transWallCheck);
    layout->addRow(_transWallCheck);

    _transGlassCheck = new QCheckBox("Glass", this);
    connectCheckBox(_transGlassCheck);
    layout->addRow(_transGlassCheck);

    _transRedCheck = new QCheckBox("Red", this);
    connectCheckBox(_transRedCheck);
    layout->addRow(_transRedCheck);

    _transSteamCheck = new QCheckBox("Steam", this);
    connectCheckBox(_transSteamCheck);
    layout->addRow(_transSteamCheck);

    _transEnergyCheck = new QCheckBox("Energy", this);
    connectCheckBox(_transEnergyCheck);
    layout->addRow(_transEnergyCheck);
}

void ProCommonFieldsWidget::setupObjectFlags(QFormLayout* layout) {
    // Core behavior flags
    _flatCheck = new QCheckBox("Flat", this);
    _flatCheck->setToolTip("Object is rendered flat with tiles (no height)");
    connectCheckBox(_flatCheck);
    layout->addRow(_flatCheck);

    _noBlockCheck = new QCheckBox("No Block", this);
    _noBlockCheck->setToolTip("Does not block character movement");
    connectCheckBox(_noBlockCheck);
    layout->addRow(_noBlockCheck);

    _multiHexCheck = new QCheckBox("Multi-hex", this);
    _multiHexCheck->setToolTip("Object occupies multiple hexes");
    connectCheckBox(_multiHexCheck);
    layout->addRow(_multiHexCheck);

    _noHighlightCheck = new QCheckBox("No Highlight", this);
    _noHighlightCheck->setToolTip("Cannot be highlighted by mouse cursor");
    connectCheckBox(_noHighlightCheck);
    layout->addRow(_noHighlightCheck);

    // Pass-through flags
    _lightThruCheck = new QCheckBox("Light Through", this);
    _lightThruCheck->setToolTip("Light can pass through this object");
    connectCheckBox(_lightThruCheck);
    layout->addRow(_lightThruCheck);

    _shootThruCheck = new QCheckBox("Shoot Through", this);
    _shootThruCheck->setToolTip("Projectiles can pass through this object");
    connectCheckBox(_shootThruCheck);
    layout->addRow(_shootThruCheck);
}

void ProCommonFieldsWidget::setupExtendedFlags(QFormLayout* layout) {
    // Animation control fields (directly in the main group, no nested group)
    _animationPrimaryEdit = createSpinBox(0, 15, "Primary attack animation index (0-15)");
    connectSpinBox(_animationPrimaryEdit);
    layout->addRow("Primary Animation:", _animationPrimaryEdit);

    _animationSecondaryEdit = createSpinBox(0, 15, "Secondary attack animation index (0-15)");
    connectSpinBox(_animationSecondaryEdit);
    layout->addRow("Secondary Animation:", _animationSecondaryEdit);
}

void ProCommonFieldsWidget::setupItemFields(QFormLayout* layout) {
    // Script ID
    _sidEdit = createSpinBox(0, 999999, "Script ID for this object");
    connectSpinBox(_sidEdit);
    layout->addRow("Script ID:", _sidEdit);

    // Material type
    _materialCombo = createMaterialComboBox("Material type affects sound and destruction");
    connectComboBox(_materialCombo);
    layout->addRow("Material:", _materialCombo);

    // Container size
    _containerSizeEdit = createSpinBox(0, INT_MAX, "Maximum container volume");
    connectSpinBox(_containerSizeEdit);
    layout->addRow("Container Size:", _containerSizeEdit);

    // Weight (in pounds * 16)
    _weightEdit = createSpinBox(0, INT_MAX, "Weight in pounds * 16");
    connectSpinBox(_weightEdit);
    layout->addRow("Weight:", _weightEdit);

    // Base price
    _basePriceEdit = createSpinBox(0, INT_MAX, "Base price in bottle caps");
    connectSpinBox(_basePriceEdit);
    layout->addRow("Base Price:", _basePriceEdit);

    // Sound ID
    _soundIdEdit = createSpinBox(0, MAX_SOUND_ID, "Sound effect ID (0-255)");
    connectSpinBox(_soundIdEdit);
    layout->addRow("Sound ID:", _soundIdEdit);
}

void ProCommonFieldsWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro) {
        spdlog::warn("ProCommonFieldsWidget::loadFromPro - null PRO object");
        return;
    }

    _pro = pro;

    _lightRadiusEdit->setValue(pro->header.light_distance);
    _lightIntensityEdit->setValue(pro->header.light_intensity);

    loadObjectFlags(pro->header.flags);

    if (pro->type() == Pro::OBJECT_TYPE::ITEM) {
        _sidEdit->setValue(pro->commonItemData.SID);
        _materialCombo->setCurrentIndex(pro->commonItemData.materialId);
        _containerSizeEdit->setValue(pro->commonItemData.containerSize);
        _weightEdit->setValue(pro->commonItemData.weight);
        _basePriceEdit->setValue(pro->commonItemData.basePrice);
        _soundIdEdit->setValue(pro->commonItemData.soundId);
        loadExtendedFlags(pro->commonItemData.flagsExt);
    }
}

void ProCommonFieldsWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro) {
        spdlog::warn("ProCommonFieldsWidget::saveToPro - null PRO object");
        return;
    }

    pro->header.light_distance = _lightRadiusEdit->value();
    pro->header.light_intensity = _lightIntensityEdit->value();

    pro->header.flags = saveObjectFlags();

    if (pro->type() == Pro::OBJECT_TYPE::ITEM) {
        pro->commonItemData.SID = _sidEdit->value();
        pro->commonItemData.materialId = _materialCombo->currentIndex();
        pro->commonItemData.containerSize = _containerSizeEdit->value();
        pro->commonItemData.weight = _weightEdit->value();
        pro->commonItemData.basePrice = _basePriceEdit->value();
        pro->commonItemData.soundId = static_cast<uint8_t>(_soundIdEdit->value());
        pro->commonItemData.flagsExt = saveExtendedFlags();
    }
}

void ProCommonFieldsWidget::setItemFieldsVisible(bool isItem) {
    _itemFieldsGroup->setVisible(isItem);
}

int32_t ProCommonFieldsWidget::getPID() const {
    return 0;
}

void ProCommonFieldsWidget::setPID([[maybe_unused]] int32_t pid) {
    Q_UNUSED(pid);
}

void ProCommonFieldsWidget::loadObjectFlags(uint32_t flags) {
    _flatCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_FLAT));
    _noBlockCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_BLOCK));
    _lightingCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_LIGHTING));
    _multiHexCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_MULTIHEX));
    _noHighlightCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_NO_HIGHLIGHT));
    _transRedCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_RED));
    _transNoneCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_NONE));
    _transWallCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_WALL));
    _transGlassCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_GLASS));
    _transSteamCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_STEAM));
    _transEnergyCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_ENERGY));
    _lightThruCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_LIGHT_THRU));
    _shootThruCheck->setChecked(Pro::hasFlag(flags, Pro::ObjectFlags::OBJECT_SHOOT_THRU));
}

void ProCommonFieldsWidget::loadExtendedFlags(uint32_t flagsExt) {
    _animationPrimaryEdit->setValue(Pro::getAnimationPrimary(flagsExt));
    _animationSecondaryEdit->setValue(Pro::getAnimationSecondary(flagsExt));
}

uint32_t ProCommonFieldsWidget::saveObjectFlags() const {
    uint32_t flags = 0;

    if (_flatCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_FLAT);
    if (_noBlockCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_NO_BLOCK);
    if (_lightingCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_LIGHTING);
    if (_multiHexCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_MULTIHEX);
    if (_noHighlightCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_NO_HIGHLIGHT);
    if (_transRedCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_RED);
    if (_transNoneCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_NONE);
    if (_transWallCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_WALL);
    if (_transGlassCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_GLASS);
    if (_transSteamCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_STEAM);
    if (_transEnergyCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_TRANS_ENERGY);
    if (_lightThruCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_LIGHT_THRU);
    if (_shootThruCheck->isChecked())
        flags = Pro::setFlag(flags, Pro::ObjectFlags::OBJECT_SHOOT_THRU);

    return flags;
}

uint32_t ProCommonFieldsWidget::saveExtendedFlags() const {
    uint32_t flags = 0;
    flags = Pro::setAnimationPrimary(flags, _animationPrimaryEdit->value());
    flags = Pro::setAnimationSecondary(flags, _animationSecondaryEdit->value());
    return flags;
}

QSpinBox* ProCommonFieldsWidget::createSpinBox(int min, int max, const QString& tooltip) {
    auto spinBox = new QSpinBox(this);
    spinBox->setRange(min, max);
    if (!tooltip.isEmpty()) {
        spinBox->setToolTip(tooltip);
    }
    return spinBox;
}

QSpinBox* ProCommonFieldsWidget::createHexSpinBox(int max, const QString& tooltip) {
    auto spinBox = createSpinBox(0, max, tooltip);
    spinBox->setDisplayIntegerBase(16);
    return spinBox;
}

QComboBox* ProCommonFieldsWidget::createMaterialComboBox(const QString& tooltip) {
    auto comboBox = new QComboBox(this);
    comboBox->addItems(game::enums::materialTypes());
    if (!tooltip.isEmpty()) {
        comboBox->setToolTip(tooltip);
    }
    return comboBox;
}

void ProCommonFieldsWidget::connectSpinBox(QSpinBox* spinBox) {
    connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged),
        this, &ProCommonFieldsWidget::onFieldChanged);
}

void ProCommonFieldsWidget::connectComboBox(QComboBox* comboBox) {
    connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ProCommonFieldsWidget::onFieldChanged);
}

void ProCommonFieldsWidget::connectCheckBox(QCheckBox* checkBox) {
    connect(checkBox, &QCheckBox::toggled,
        this, &ProCommonFieldsWidget::onObjectFlagChanged);
}

// Slot implementations
void ProCommonFieldsWidget::onFieldChanged() {
    emit fieldChanged();
}

void ProCommonFieldsWidget::onObjectFlagChanged() {
    emit fieldChanged();
}

} // namespace geck
