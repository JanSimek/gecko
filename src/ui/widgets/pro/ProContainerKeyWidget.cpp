#include "ProContainerKeyWidget.h"
#include "../../theme/ThemeManager.h"
#include "../../GameEnums.h"
#include "../../UIConstants.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace geck {

ProContainerKeyWidget::ProContainerKeyWidget(resource::GameResources& resources, QWidget* parent)
    : ProTabWidget(resources, parent)
    , _containerMaxSizeEdit(nullptr)
    , _keyIdEdit(nullptr)
    , _isContainer(false)
    , _isKey(false) {

    for (int i = 0; i < NUM_CONTAINER_FLAGS; ++i) {
        _containerFlagChecks[i] = nullptr;
    }

    setupUI();
}

void ProContainerKeyWidget::setupUI() {
}

void ProContainerKeyWidget::setupContainerUI() {
    while (QLayoutItem* item = _mainLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    QHBoxLayout* columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(ui::constants::SPACING_COLUMNS);

    QWidget* leftColumn = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(ui::constants::SPACING_NORMAL);

    QGroupBox* containerGroup = createStandardGroupBox("Container Properties");
    QFormLayout* containerLayout = createStandardFormLayout();
    addLayoutToGroupBox(containerGroup, containerLayout);

    _containerMaxSizeEdit = createSpinBox(0, 999999, "Maximum size in volume units that this container can hold");
    containerLayout->addRow("Max Size:", _containerMaxSizeEdit);

    leftLayout->addWidget(containerGroup);
    leftLayout->addStretch();

    QWidget* rightColumn = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(ui::constants::SPACING_NORMAL);

    QGroupBox* flagsGroup = new QGroupBox("Container Flags");
    flagsGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QVBoxLayout* flagsLayout = new QVBoxLayout(flagsGroup);
    flagsLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN_VERTICAL, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    flagsLayout->setSpacing(ui::constants::SPACING_TIGHT);

    const QStringList flagNames = game::enums::containerFlags();
    for (int i = 0; i < NUM_CONTAINER_FLAGS; ++i) {
        _containerFlagChecks[i] = new QCheckBox(flagNames[i]);
        _containerFlagChecks[i]->setToolTip(QString("Container flag: %1").arg(flagNames[i]));
        connectCheckBox(_containerFlagChecks[i]);
        flagsLayout->addWidget(_containerFlagChecks[i]);
    }

    rightLayout->addWidget(flagsGroup);
    rightLayout->addStretch();

    columnsLayout->addWidget(leftColumn, 1);
    columnsLayout->addWidget(rightColumn, 1);
    _mainLayout->addLayout(columnsLayout);
}

void ProContainerKeyWidget::setupKeyUI() {
    while (QLayoutItem* item = _mainLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    QGroupBox* keyGroup = createStandardGroupBox("Key Properties");
    QFormLayout* keyLayout = createStandardFormLayout();
    addLayoutToGroupBox(keyGroup, keyLayout);

    _keyIdEdit = createSpinBox(0, 999999, "Unique key identifier");
    keyLayout->addRow("Key ID:", _keyIdEdit);

    _mainLayout->addWidget(keyGroup);
    _mainLayout->addStretch();
}

void ProContainerKeyWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    _isContainer = (pro->itemType() == Pro::ITEM_TYPE::CONTAINER);
    _isKey = (pro->itemType() == Pro::ITEM_TYPE::KEY);

    if (_isContainer) {
        setupContainerUI();

        _containerData.maxSize = pro->containerData.maxSize;
        _containerData.flags = pro->containerData.flags;

        if (_containerMaxSizeEdit) {
            _containerMaxSizeEdit->setValue(static_cast<int>(_containerData.maxSize));
        }

        for (int i = 0; i < NUM_CONTAINER_FLAGS; ++i) {
            if (_containerFlagChecks[i]) {
                bool flagSet = (_containerData.flags & (1 << i)) != 0;
                _containerFlagChecks[i]->setChecked(flagSet);
            }
        }

    } else if (_isKey) {
        setupKeyUI();

        _keyData.keyId = pro->keyData.keyId;

        if (_keyIdEdit) {
            _keyIdEdit->setValue(static_cast<int>(_keyData.keyId));
        }
    }
}

void ProContainerKeyWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    if (_isContainer) {
        if (_containerMaxSizeEdit) {
            _containerData.maxSize = static_cast<uint32_t>(_containerMaxSizeEdit->value());
        }

        uint32_t flags = 0;
        for (int i = 0; i < NUM_CONTAINER_FLAGS; ++i) {
            if (_containerFlagChecks[i] && _containerFlagChecks[i]->isChecked()) {
                flags |= (1 << i);
            }
        }
        _containerData.flags = flags;

        pro->containerData.maxSize = _containerData.maxSize;
        pro->containerData.flags = _containerData.flags;

    } else if (_isKey) {
        if (_keyIdEdit) {
            _keyData.keyId = static_cast<uint32_t>(_keyIdEdit->value());
        }

        pro->keyData.keyId = _keyData.keyId;
    }
}

bool ProContainerKeyWidget::canHandle(const std::shared_ptr<Pro>& pro) const {
    return pro && pro->type() == Pro::OBJECT_TYPE::ITEM && (pro->itemType() == Pro::ITEM_TYPE::CONTAINER || pro->itemType() == Pro::ITEM_TYPE::KEY);
}

QString ProContainerKeyWidget::getTabLabel() const {
    if (_isContainer)
        return "Container";
    if (_isKey)
        return "Key";
    return "Common";
}

} // namespace geck
