#include "ProSceneryWidget.h"

#include "../../theme/ThemeManager.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <climits>

namespace geck {

namespace {

    constexpr int MAX_SOUND_ID = 255;
    constexpr int MAX_MAP_TILE = 39999;
    constexpr int MAX_ELEVATION = 3;

}

ProSceneryWidget::ProSceneryWidget(resource::GameResources& resources, QWidget* parent)
    : ProTabWidget(resources, parent)
    , _materialIdCombo(nullptr)
    , _soundIdEdit(nullptr)
    , _typeCombo(nullptr)
    , _doorGroup(nullptr)
    , _stairsGroup(nullptr)
    , _elevatorGroup(nullptr)
    , _ladderGroup(nullptr)
    , _genericGroup(nullptr)
    , _doorWalkThroughCheck(nullptr)
    , _doorUnknownEdit(nullptr)
    , _stairsDestTileEdit(nullptr)
    , _stairsDestElevationEdit(nullptr)
    , _elevatorTypeEdit(nullptr)
    , _elevatorLevelEdit(nullptr)
    , _ladderDestTileElevationEdit(nullptr)
    , _genericUnknownEdit(nullptr) {
    setupUI();
}

void ProSceneryWidget::setupUI() {
    auto* columnsLayout = new QHBoxLayout();
    auto* leftColumnLayout = new QVBoxLayout();
    auto* rightColumnLayout = new QVBoxLayout();
    columnsLayout->addLayout(leftColumnLayout, 1);
    columnsLayout->addLayout(rightColumnLayout, 1);
    _mainLayout->addLayout(columnsLayout);

    _doorGroup = createStandardGroupBox("Door Properties");
    _stairsGroup = createStandardGroupBox("Stairs Properties");
    _elevatorGroup = createStandardGroupBox("Elevator Properties");
    _ladderGroup = createStandardGroupBox("Ladder Properties");
    _genericGroup = createStandardGroupBox("Generic Properties");

    auto* basicGroup = createStandardGroupBox("Basic Properties");
    basicGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    auto* basicLayout = createStandardFormLayout();
    addLayoutToGroupBox(basicGroup, basicLayout);

    _materialIdCombo = createMaterialComboBox("Material type for scenery");
    basicLayout->addRow("Material:", _materialIdCombo);

    _soundIdEdit = createSpinBox(0, MAX_SOUND_ID, "Sound ID for interactions");
    basicLayout->addRow("Sound ID:", _soundIdEdit);

    _typeCombo = createComboBox(game::enums::sceneryTypes(_resources), "Scenery subtype");
    basicLayout->addRow("Type:", _typeCombo);
    connect(_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        updateTypeSpecificGroups();
    });

    leftColumnLayout->addWidget(basicGroup);
    leftColumnLayout->addStretch();

    auto* doorLayout = createStandardFormLayout();
    addLayoutToGroupBox(_doorGroup, doorLayout);
    _doorGroup->setStyleSheet(ui::theme::styles::boldGroupBox());

    _doorWalkThroughCheck = new QCheckBox("Walk Through");
    _doorWalkThroughCheck->setToolTip("Allow walking through the door");
    connectCheckBox(_doorWalkThroughCheck);
    doorLayout->addRow("", _doorWalkThroughCheck);

    _doorUnknownEdit = createSpinBox(0, INT_MAX, "Door unknown field");
    doorLayout->addRow("Unknown Field:", _doorUnknownEdit);

    auto* stairsLayout = createStandardFormLayout();
    addLayoutToGroupBox(_stairsGroup, stairsLayout);
    _stairsGroup->setStyleSheet(ui::theme::styles::boldGroupBox());

    _stairsDestTileEdit = createSpinBox(0, MAX_MAP_TILE, "Destination tile number");
    stairsLayout->addRow("Dest Tile:", _stairsDestTileEdit);

    _stairsDestElevationEdit = createSpinBox(0, MAX_ELEVATION, "Destination elevation");
    stairsLayout->addRow("Dest Elevation:", _stairsDestElevationEdit);

    auto* elevatorLayout = createStandardFormLayout();
    addLayoutToGroupBox(_elevatorGroup, elevatorLayout);
    _elevatorGroup->setStyleSheet(ui::theme::styles::boldGroupBox());

    _elevatorTypeEdit = createSpinBox(0, INT_MAX, "Elevator type");
    elevatorLayout->addRow("Type:", _elevatorTypeEdit);

    _elevatorLevelEdit = createSpinBox(0, INT_MAX, "Elevator level");
    elevatorLayout->addRow("Level:", _elevatorLevelEdit);

    auto* ladderLayout = createStandardFormLayout();
    addLayoutToGroupBox(_ladderGroup, ladderLayout);
    _ladderGroup->setStyleSheet(ui::theme::styles::boldGroupBox());

    _ladderDestTileElevationEdit = createSpinBox(0, INT_MAX, "Destination tile and elevation combined");
    ladderLayout->addRow("Dest Tile+Elev:", _ladderDestTileElevationEdit);

    auto* genericLayout = createStandardFormLayout();
    addLayoutToGroupBox(_genericGroup, genericLayout);
    _genericGroup->setStyleSheet(ui::theme::styles::boldGroupBox());

    _genericUnknownEdit = createSpinBox(0, INT_MAX, "Generic unknown field");
    genericLayout->addRow("Unknown Field:", _genericUnknownEdit);

    rightColumnLayout->addWidget(_doorGroup);
    rightColumnLayout->addWidget(_stairsGroup);
    rightColumnLayout->addWidget(_elevatorGroup);
    rightColumnLayout->addWidget(_ladderGroup);
    rightColumnLayout->addWidget(_genericGroup);
    rightColumnLayout->addStretch();

    updateTypeSpecificGroups();
}

void ProSceneryWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro)) {
        return;
    }

    setComboIndexSafe(_materialIdCombo, pro->sceneryData.materialId);
    if (_soundIdEdit) {
        _soundIdEdit->setValue(static_cast<int>(pro->sceneryData.soundId));
    }

    const auto sceneryType = static_cast<Pro::SCENERY_TYPE>(pro->objectSubtypeId());
    setComboIndexSafe(_typeCombo, static_cast<uint32_t>(sceneryType));

    switch (sceneryType) {
        case Pro::SCENERY_TYPE::DOOR:
            if (_doorWalkThroughCheck) {
                _doorWalkThroughCheck->setChecked(pro->sceneryData.doorData.walkThroughFlag != 0);
            }
            if (_doorUnknownEdit) {
                _doorUnknownEdit->setValue(static_cast<int>(pro->sceneryData.doorData.unknownField));
            }
            break;
        case Pro::SCENERY_TYPE::STAIRS:
            if (_stairsDestTileEdit) {
                _stairsDestTileEdit->setValue(static_cast<int>(pro->sceneryData.stairsData.destTile));
            }
            if (_stairsDestElevationEdit) {
                _stairsDestElevationEdit->setValue(static_cast<int>(pro->sceneryData.stairsData.destElevation));
            }
            break;
        case Pro::SCENERY_TYPE::ELEVATOR:
            if (_elevatorTypeEdit) {
                _elevatorTypeEdit->setValue(static_cast<int>(pro->sceneryData.elevatorData.elevatorType));
            }
            if (_elevatorLevelEdit) {
                _elevatorLevelEdit->setValue(static_cast<int>(pro->sceneryData.elevatorData.elevatorLevel));
            }
            break;
        case Pro::SCENERY_TYPE::LADDER_BOTTOM:
        case Pro::SCENERY_TYPE::LADDER_TOP:
            if (_ladderDestTileElevationEdit) {
                _ladderDestTileElevationEdit->setValue(static_cast<int>(pro->sceneryData.ladderData.destTileAndElevation));
            }
            break;
        case Pro::SCENERY_TYPE::GENERIC:
            if (_genericUnknownEdit) {
                _genericUnknownEdit->setValue(static_cast<int>(pro->sceneryData.genericData.unknownField));
            }
            break;
    }

    updateTypeSpecificGroups();
}

void ProSceneryWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro)) {
        return;
    }

    if (_materialIdCombo) {
        pro->sceneryData.materialId = static_cast<uint32_t>(_materialIdCombo->currentIndex());
    }
    if (_soundIdEdit) {
        pro->sceneryData.soundId = static_cast<uint8_t>(_soundIdEdit->value());
    }

    const auto sceneryType = currentSceneryType();
    pro->setObjectSubtypeId(static_cast<unsigned>(sceneryType));

    switch (sceneryType) {
        case Pro::SCENERY_TYPE::DOOR:
            if (_doorWalkThroughCheck) {
                pro->sceneryData.doorData.walkThroughFlag = _doorWalkThroughCheck->isChecked() ? 1U : 0U;
            }
            if (_doorUnknownEdit) {
                pro->sceneryData.doorData.unknownField = static_cast<uint32_t>(_doorUnknownEdit->value());
            }
            break;
        case Pro::SCENERY_TYPE::STAIRS:
            if (_stairsDestTileEdit) {
                pro->sceneryData.stairsData.destTile = static_cast<uint32_t>(_stairsDestTileEdit->value());
            }
            if (_stairsDestElevationEdit) {
                pro->sceneryData.stairsData.destElevation = static_cast<uint32_t>(_stairsDestElevationEdit->value());
            }
            break;
        case Pro::SCENERY_TYPE::ELEVATOR:
            if (_elevatorTypeEdit) {
                pro->sceneryData.elevatorData.elevatorType = static_cast<uint32_t>(_elevatorTypeEdit->value());
            }
            if (_elevatorLevelEdit) {
                pro->sceneryData.elevatorData.elevatorLevel = static_cast<uint32_t>(_elevatorLevelEdit->value());
            }
            break;
        case Pro::SCENERY_TYPE::LADDER_BOTTOM:
        case Pro::SCENERY_TYPE::LADDER_TOP:
            if (_ladderDestTileElevationEdit) {
                pro->sceneryData.ladderData.destTileAndElevation = static_cast<uint32_t>(_ladderDestTileElevationEdit->value());
            }
            break;
        case Pro::SCENERY_TYPE::GENERIC:
            if (_genericUnknownEdit) {
                pro->sceneryData.genericData.unknownField = static_cast<uint32_t>(_genericUnknownEdit->value());
            }
            break;
    }
}

bool ProSceneryWidget::canHandle(const std::shared_ptr<Pro>& pro) const {
    return pro && pro->type() == Pro::OBJECT_TYPE::SCENERY;
}

QString ProSceneryWidget::getTabLabel() const {
    return "Scenery";
}

void ProSceneryWidget::updateTypeSpecificGroups() {
    const auto sceneryType = currentSceneryType();
    if (_doorGroup) {
        _doorGroup->setVisible(sceneryType == Pro::SCENERY_TYPE::DOOR);
    }
    if (_stairsGroup) {
        _stairsGroup->setVisible(sceneryType == Pro::SCENERY_TYPE::STAIRS);
    }
    if (_elevatorGroup) {
        _elevatorGroup->setVisible(sceneryType == Pro::SCENERY_TYPE::ELEVATOR);
    }
    const bool isLadder = sceneryType == Pro::SCENERY_TYPE::LADDER_BOTTOM
        || sceneryType == Pro::SCENERY_TYPE::LADDER_TOP;
    if (_ladderGroup) {
        _ladderGroup->setVisible(isLadder);
    }
    if (_genericGroup) {
        _genericGroup->setVisible(sceneryType == Pro::SCENERY_TYPE::GENERIC);
    }
}

Pro::SCENERY_TYPE ProSceneryWidget::currentSceneryType() const {
    if (!_typeCombo) {
        return Pro::SCENERY_TYPE::GENERIC;
    }
    const int currentIndex = _typeCombo->currentIndex();
    if (currentIndex < 0) {
        return Pro::SCENERY_TYPE::GENERIC;
    }
    return static_cast<Pro::SCENERY_TYPE>(currentIndex);
}

} // namespace geck
