#include "ProArmorWidget.h"
#include <algorithm>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFont>
#include "util/ResourceManager.h"
#include "../../dialogs/FrmSelectorDialog.h"
#include "../../theme/ThemeManager.h"
#include "../../GameEnums.h"
#include "../../UIConstants.h"

namespace geck {

ProArmorWidget::ProArmorWidget(QWidget* parent)
    : ProTabWidget(parent)
    , _armorClassEdit(nullptr)
    , _armorPerkCombo(nullptr)
    , _armorAIPriorityLabel(nullptr)
    , _armorMalePreviewWidget(nullptr)
    , _armorFemalePreviewWidget(nullptr)
    , _armorMaleFID(0)
    , _armorFemaleFID(0) {

    for (int i = 0; i < DAMAGE_TYPES_COUNT; ++i) {
        _damageResistEdits[i] = nullptr;
        _damageThresholdEdits[i] = nullptr;
    }

    setupUI();
}

void ProArmorWidget::setupUI() {
    QHBoxLayout* columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(ui::constants::SPACING_COLUMNS);

    // Left column
    QWidget* leftColumn = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(ui::constants::SPACING_NORMAL);

    // Right column
    QWidget* rightColumn = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(ui::constants::SPACING_NORMAL);

    // === LEFT COLUMN: Protection Values ===

    // Armor Class
    QGroupBox* acGroup = new QGroupBox("Protection Values");
    acGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* acLayout = createStandardFormLayout();

    _armorClassEdit = createSpinBox(0, 999, "Armor Class - higher values provide better protection");
    connect(_armorClassEdit, QOverload<int>::of(&QSpinBox::valueChanged),
        this, &ProArmorWidget::updateAIPriority);
    acLayout->addRow("Armor Class:", _armorClassEdit);

    acGroup->setLayout(acLayout);
    leftLayout->addWidget(acGroup);

    // Defence State (unified Threshold and Resistance table)
    QGroupBox* defenceGroup = new QGroupBox("Defence State");
    defenceGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QGridLayout* defenceLayout = new QGridLayout(defenceGroup);
    defenceLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN_VERTICAL, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    defenceLayout->setSpacing(ui::constants::SPACING_GRID);
    defenceLayout->setHorizontalSpacing(ui::constants::SPACING_GRID);

    const QStringList damageTypes = game::enums::damageTypes7();

    // Headers
    QLabel* thresholdHeader = new QLabel("Threshold");
    thresholdHeader->setFont(ui::theme::fonts::standard());
    thresholdHeader->setAlignment(Qt::AlignCenter);
    defenceLayout->addWidget(thresholdHeader, 0, 1);

    QLabel* resistanceHeader = new QLabel("Resistance");
    resistanceHeader->setFont(ui::theme::fonts::standard());
    resistanceHeader->setAlignment(Qt::AlignCenter);
    defenceLayout->addWidget(resistanceHeader, 0, 3);

    for (int i = 0; i < DAMAGE_TYPES_COUNT; ++i) {
        QLabel* typeLabel = new QLabel(damageTypes[i]);
        typeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        typeLabel->setFixedSize(ui::constants::sizes::WIDTH_TYPE_LABEL, ui::constants::sizes::HEIGHT_TYPE_LABEL);
        defenceLayout->addWidget(typeLabel, i + 1, 0);

        _damageThresholdEdits[i] = createSpinBox(0, 999,
            QString("Damage threshold against %1 damage").arg(damageTypes[i]));
        _damageThresholdEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_SMALL);
        connect(_damageThresholdEdits[i], QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ProArmorWidget::updateAIPriority);
        defenceLayout->addWidget(_damageThresholdEdits[i], i + 1, 1);

        QLabel* separator = new QLabel("/");
        separator->setAlignment(Qt::AlignCenter);
        separator->setEnabled(false);
        defenceLayout->addWidget(separator, i + 1, 2);

        _damageResistEdits[i] = createSpinBox(0, 100,
            QString("Damage resistance against %1 damage").arg(damageTypes[i]));
        _damageResistEdits[i]->setFixedWidth(ui::constants::sizes::WIDTH_INPUT_SMALL);
        connect(_damageResistEdits[i], QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ProArmorWidget::updateAIPriority);
        defenceLayout->addWidget(_damageResistEdits[i], i + 1, 3);

        QLabel* percentLabel = new QLabel("%");
        percentLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        defenceLayout->addWidget(percentLabel, i + 1, 4);
    }

    leftLayout->addWidget(defenceGroup);
    leftLayout->addStretch();

    // === RIGHT COLUMN: Armor Views and Misc Properties ===

    // Armor Views with previews
    QGroupBox* viewsGroup = new QGroupBox("Armor Views");
    viewsGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QVBoxLayout* viewsLayout = new QVBoxLayout(viewsGroup);
    viewsLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN_VERTICAL, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    viewsLayout->setSpacing(ui::constants::SPACING_NORMAL);

    // Horizontal layout for male/female previews
    QHBoxLayout* previewsLayout = new QHBoxLayout();
    previewsLayout->setContentsMargins(0, 0, 0, 0);
    previewsLayout->setSpacing(ui::constants::SPACING_NORMAL);

    // Male preview
    _armorMalePreviewWidget = new ObjectPreviewWidget(this,
        ObjectPreviewWidget::ShowAnimationControls,
        QSize(120, 120));
    _armorMalePreviewWidget->setTitle("Male");
    connect(_armorMalePreviewWidget, &ObjectPreviewWidget::fidChangeRequested,
        this, &ProArmorWidget::onArmorMaleFidChangeRequested);

    // Female preview
    _armorFemalePreviewWidget = new ObjectPreviewWidget(this,
        ObjectPreviewWidget::ShowAnimationControls,
        QSize(120, 120));
    _armorFemalePreviewWidget->setTitle("Female");
    connect(_armorFemalePreviewWidget, &ObjectPreviewWidget::fidChangeRequested,
        this, &ProArmorWidget::onArmorFemaleFidChangeRequested);

    previewsLayout->addWidget(_armorMalePreviewWidget);
    previewsLayout->addWidget(_armorFemalePreviewWidget);
    viewsLayout->addLayout(previewsLayout);

    rightLayout->addWidget(viewsGroup);

    // Misc Properties
    QGroupBox* miscGroup = createStandardGroupBox("Misc Properties");
    QFormLayout* miscLayout = createStandardFormLayout();
    static_cast<QVBoxLayout*>(miscGroup->layout())->addLayout(miscLayout);

    _armorPerkCombo = createComboBox(game::enums::armorPerkOptions(),
        "Special perk associated with this armor");
    miscLayout->addRow("Perk:", _armorPerkCombo);

    // AI Priority display
    _armorAIPriorityLabel = new QLabel("0");
    _armorAIPriorityLabel->setStyleSheet(ui::theme::styles::emphasisLabel());
    _armorAIPriorityLabel->setToolTip("AI Priority = AC + all DT values + all DR values (used by AI to select best armor)");
    miscLayout->addRow("AI Priority:", _armorAIPriorityLabel);

    rightLayout->addWidget(miscGroup);
    rightLayout->addStretch();

    // Add columns to main layout
    columnsLayout->addWidget(leftColumn, 1);
    columnsLayout->addWidget(rightColumn, 1);
    _mainLayout->addLayout(columnsLayout);
}

void ProArmorWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    // Load armor data - copy fields individually
    _armorData.armorClass = pro->armorData.armorClass;
    for (int i = 0; i < DAMAGE_TYPES_COUNT; ++i) {
        _armorData.damageResist[i] = pro->armorData.damageResist[i];
        _armorData.damageThreshold[i] = pro->armorData.damageThreshold[i];
    }
    _armorData.perk = pro->armorData.perk;
    _armorData.armorMaleFID = pro->armorData.armorMaleFID;
    _armorData.armorFemaleFID = pro->armorData.armorFemaleFID;
    _armorMaleFID = pro->armorData.armorMaleFID;
    _armorFemaleFID = pro->armorData.armorFemaleFID;

    if (_armorClassEdit) {
        _armorClassEdit->setValue(static_cast<int>(_armorData.armorClass));
    }

    loadIntArrayToWidgets(_damageThresholdEdits, _armorData.damageThreshold, DAMAGE_TYPES_COUNT);
    loadIntArrayToWidgets(_damageResistEdits, _armorData.damageResist, DAMAGE_TYPES_COUNT);

    if (_armorData.perk == UINT32_MAX) {
        setComboValue(_armorPerkCombo, fallout::NoItemPerk);
    } else {
        setComboValue(_armorPerkCombo, static_cast<int>(_armorData.perk));
    }

    updateAIPriority();
    updateArmorPreviews();
}

void ProArmorWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    if (_armorClassEdit) {
        _armorData.armorClass = static_cast<uint32_t>(_armorClassEdit->value());
    }

    saveWidgetsToIntArray(_damageThresholdEdits, _armorData.damageThreshold, DAMAGE_TYPES_COUNT);
    saveWidgetsToIntArray(_damageResistEdits, _armorData.damageResist, DAMAGE_TYPES_COUNT);

    int armorPerk = getComboValue(_armorPerkCombo, fallout::NoItemPerk);
    _armorData.perk = armorPerk < 0 ? UINT32_MAX : static_cast<uint32_t>(armorPerk);

    _armorData.armorMaleFID = _armorMaleFID;
    _armorData.armorFemaleFID = _armorFemaleFID;

    pro->armorData.armorClass = _armorData.armorClass;
    for (int i = 0; i < DAMAGE_TYPES_COUNT; ++i) {
        pro->armorData.damageResist[i] = _armorData.damageResist[i];
        pro->armorData.damageThreshold[i] = _armorData.damageThreshold[i];
    }
    pro->armorData.perk = _armorData.perk;
    pro->armorData.armorMaleFID = _armorData.armorMaleFID;
    pro->armorData.armorFemaleFID = _armorData.armorFemaleFID;
}

bool ProArmorWidget::canHandle(const std::shared_ptr<Pro>& pro) const {
    return pro && pro->type() == Pro::OBJECT_TYPE::ITEM && pro->itemType() == Pro::ITEM_TYPE::ARMOR;
}

QString ProArmorWidget::getTabLabel() const {
    return "Armor";
}

void ProArmorWidget::updateAIPriority() {
    if (_armorAIPriorityLabel) {
        _armorAIPriorityLabel->setText(QString::number(calculateAIPriority()));
    }
}

void ProArmorWidget::updateArmorPreviews() {
    if (_armorMalePreviewWidget) {
        if (_armorMaleFID <= 0) {
            _armorMalePreviewWidget->clear();
        } else {
            try {
                auto& resourceManager = ResourceManager::getInstance();
                std::string maleFrmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(_armorMaleFID));

                if (!maleFrmPath.empty()) {
                    _armorMalePreviewWidget->setFid(_armorMaleFID);
                    _armorMalePreviewWidget->setFrmPath(QString::fromStdString(maleFrmPath));
                } else {
                    _armorMalePreviewWidget->clear();
                }
            } catch (const std::exception&) {
                _armorMalePreviewWidget->clear();
            }
        }
    }

    if (_armorFemalePreviewWidget) {
        if (_armorFemaleFID <= 0) {
            _armorFemalePreviewWidget->clear();
        } else {
            try {
                auto& resourceManager = ResourceManager::getInstance();
                std::string femaleFrmPath = resourceManager.FIDtoFrmName(static_cast<unsigned int>(_armorFemaleFID));

                if (!femaleFrmPath.empty()) {
                    _armorFemalePreviewWidget->setFid(_armorFemaleFID);
                    _armorFemalePreviewWidget->setFrmPath(QString::fromStdString(femaleFrmPath));
                } else {
                    _armorFemalePreviewWidget->clear();
                }
            } catch (const std::exception&) {
                _armorFemalePreviewWidget->clear();
            }
        }
    }
}

int ProArmorWidget::calculateAIPriority() const {
    int priority = 0;

    if (_armorClassEdit) {
        priority += _armorClassEdit->value();
    }

    for (int i = 0; i < DAMAGE_TYPES_COUNT; ++i) {
        if (_damageThresholdEdits[i]) {
            priority += _damageThresholdEdits[i]->value();
        }
    }

    for (int i = 0; i < DAMAGE_TYPES_COUNT; ++i) {
        if (_damageResistEdits[i]) {
            priority += _damageResistEdits[i]->value();
        }
    }

    return priority;
}

void ProArmorWidget::onArmorMaleFidChangeRequested() {
    selectArmorFid(_armorMalePreviewWidget, _armorMaleFID);
}

void ProArmorWidget::onArmorFemaleFidChangeRequested() {
    selectArmorFid(_armorFemalePreviewWidget, _armorFemaleFID);
}

void ProArmorWidget::selectArmorFid(ObjectPreviewWidget* previewWidget, int32_t& fid) {
    if (!previewWidget) {
        return;
    }

    FrmSelectorDialog dialog(this);
    dialog.setObjectTypeFilter(1);
    dialog.setInitialFrmPid(static_cast<uint32_t>(std::max(fid, 0)));

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    uint32_t selectedFrmPid = dialog.getSelectedFrmPid();
    if (selectedFrmPid == 0) {
        return;
    }

    fid = static_cast<int32_t>(selectedFrmPid);
    updateArmorPreviews();
    emit fieldChanged();
}

void ProArmorWidget::setArmorMaleFID(int32_t fid) {
    _armorMaleFID = fid;
    updateArmorPreviews();
}

void ProArmorWidget::setArmorFemaleFID(int32_t fid) {
    _armorFemaleFID = fid;
    updateArmorPreviews();
}

} // namespace geck
