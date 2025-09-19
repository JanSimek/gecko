#include "ProArmorWidget.h"
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFont>

namespace geck {

ProArmorWidget::ProArmorWidget(QWidget* parent)
    : ProTabWidget(parent)
    , _armorClassEdit(nullptr)
    , _armorPerkCombo(nullptr)
    , _armorMaleFIDLabel(nullptr)
    , _armorMaleFIDSelectorButton(nullptr)
    , _armorFemaleFIDLabel(nullptr)
    , _armorFemaleFIDSelectorButton(nullptr)
    , _armorAIPriorityLabel(nullptr)
    , _armorMaleFID(0)
    , _armorFemaleFID(0) {
    
    // Initialize arrays
    for (int i = 0; i < DAMAGE_TYPES_COUNT; ++i) {
        _damageResistEdits[i] = nullptr;
        _damageThresholdEdits[i] = nullptr;
    }
    
    setupUI();
}

void ProArmorWidget::setupUI() {
    // Create two-column layout
    QHBoxLayout* columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(12);
    
    // Left column
    QWidget* leftColumn = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    
    // Right column
    QWidget* rightColumn = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    
    // === LEFT COLUMN: Protection Values ===
    
    // Armor Class
    QGroupBox* acGroup = new QGroupBox("Protection Values");
    acGroup->setStyleSheet("QGroupBox { font-weight: bold; }");
    QFormLayout* acLayout = createStandardFormLayout();
    
    _armorClassEdit = createSpinBox(0, 999, "Armor Class - higher values provide better protection");
    connect(_armorClassEdit, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &ProArmorWidget::updateAIPriority);
    acLayout->addRow("Armor Class:", _armorClassEdit);
    
    acGroup->setLayout(acLayout);
    leftLayout->addWidget(acGroup);
    
    // Defence State (unified Threshold and Resistance table)
    QGroupBox* defenceGroup = new QGroupBox("Defence State");
    defenceGroup->setStyleSheet("QGroupBox { font-weight: bold; }");
    QGridLayout* defenceLayout = new QGridLayout(defenceGroup);
    defenceLayout->setContentsMargins(8, 12, 8, 8);
    defenceLayout->setSpacing(2);
    defenceLayout->setHorizontalSpacing(2);
    
    const QStringList damageTypes = {"Normal", "Laser", "Fire", "Plasma", "Energy", "EMP", "Explode"};
    
    // Headers
    QLabel* thresholdHeader = new QLabel("Threshold");
    thresholdHeader->setFont(QFont("Microsoft Sans Serif", 9));
    thresholdHeader->setAlignment(Qt::AlignCenter);
    defenceLayout->addWidget(thresholdHeader, 0, 1);
    
    QLabel* resistanceHeader = new QLabel("Resistance");
    resistanceHeader->setFont(QFont("Microsoft Sans Serif", 9));
    resistanceHeader->setAlignment(Qt::AlignCenter);
    defenceLayout->addWidget(resistanceHeader, 0, 3);
    
    // Create the unified table for each damage type
    for (int i = 0; i < DAMAGE_TYPES_COUNT; ++i) {
        // Damage type label
        QLabel* typeLabel = new QLabel(damageTypes[i]);
        typeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        typeLabel->setFixedSize(50, 19);
        defenceLayout->addWidget(typeLabel, i + 1, 0);
        
        // Threshold input
        _damageThresholdEdits[i] = createSpinBox(0, 999, 
            QString("Damage threshold against %1 damage").arg(damageTypes[i]));
        _damageThresholdEdits[i]->setFixedWidth(40);
        connect(_damageThresholdEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), 
                this, &ProArmorWidget::updateAIPriority);
        defenceLayout->addWidget(_damageThresholdEdits[i], i + 1, 1);
        
        // Separator "/"
        QLabel* separator = new QLabel("/");
        separator->setAlignment(Qt::AlignCenter);
        separator->setEnabled(false);
        defenceLayout->addWidget(separator, i + 1, 2);
        
        // Resistance input
        _damageResistEdits[i] = createSpinBox(0, 100, 
            QString("Damage resistance against %1 damage").arg(damageTypes[i]));
        _damageResistEdits[i]->setFixedWidth(40);
        connect(_damageResistEdits[i], QOverload<int>::of(&QSpinBox::valueChanged), 
                this, &ProArmorWidget::updateAIPriority);
        defenceLayout->addWidget(_damageResistEdits[i], i + 1, 3);
        
        // "%" label
        QLabel* percentLabel = new QLabel("%");
        percentLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        defenceLayout->addWidget(percentLabel, i + 1, 4);
    }
    
    leftLayout->addWidget(defenceGroup);
    leftLayout->addStretch();
    
    // === RIGHT COLUMN: Armor Views and Misc Properties ===
    
    // Armor Views
    QGroupBox* viewsGroup = new QGroupBox("Armor Views");
    viewsGroup->setStyleSheet("QGroupBox { font-weight: bold; }");
    QFormLayout* viewsLayout = createStandardFormLayout();
    
    // Male armor FID
    QHBoxLayout* maleFidLayout = new QHBoxLayout();
    _armorMaleFIDLabel = new QLabel("None");
    _armorMaleFIDLabel->setMinimumWidth(100);
    _armorMaleFIDSelectorButton = new QPushButton("...");
    _armorMaleFIDSelectorButton->setMaximumWidth(30);
    connect(_armorMaleFIDSelectorButton, &QPushButton::clicked, 
            this, &ProArmorWidget::armorMaleFidRequested);
    maleFidLayout->addWidget(_armorMaleFIDLabel);
    maleFidLayout->addWidget(_armorMaleFIDSelectorButton);
    maleFidLayout->addStretch();
    viewsLayout->addRow("Male:", maleFidLayout);
    
    // Female armor FID
    QHBoxLayout* femaleFidLayout = new QHBoxLayout();
    _armorFemaleFIDLabel = new QLabel("None");
    _armorFemaleFIDLabel->setMinimumWidth(100);
    _armorFemaleFIDSelectorButton = new QPushButton("...");
    _armorFemaleFIDSelectorButton->setMaximumWidth(30);
    connect(_armorFemaleFIDSelectorButton, &QPushButton::clicked, 
            this, &ProArmorWidget::armorFemaleFidRequested);
    femaleFidLayout->addWidget(_armorFemaleFIDLabel);
    femaleFidLayout->addWidget(_armorFemaleFIDSelectorButton);
    femaleFidLayout->addStretch();
    viewsLayout->addRow("Female:", femaleFidLayout);
    
    viewsGroup->setLayout(viewsLayout);
    rightLayout->addWidget(viewsGroup);
    
    // Misc Properties
    QGroupBox* miscGroup = createStandardGroupBox("Misc Properties");
    QFormLayout* miscLayout = createStandardFormLayout();
    static_cast<QVBoxLayout*>(miscGroup->layout())->addLayout(miscLayout);
    
    _armorPerkCombo = createComboBox({"None", "PowerArmor", "CombatArmor", "Other"}, 
                                     "Special perk associated with this armor");
    miscLayout->addRow("Perk:", _armorPerkCombo);
    
    // AI Priority display
    _armorAIPriorityLabel = new QLabel("0");
    _armorAIPriorityLabel->setStyleSheet("font-weight: bold; color: #0066CC;");
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
    if (!pro || !canHandle(pro)) return;
    
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
    
    // Update UI
    if (_armorClassEdit) {
        _armorClassEdit->setValue(static_cast<int>(_armorData.armorClass));
    }
    
    // Load damage thresholds and resistances
    loadIntArrayToWidgets(_damageThresholdEdits, _armorData.damageThreshold, DAMAGE_TYPES_COUNT);
    loadIntArrayToWidgets(_damageResistEdits, _armorData.damageResist, DAMAGE_TYPES_COUNT);
    
    if (_armorPerkCombo) {
        _armorPerkCombo->setCurrentIndex(static_cast<int>(_armorData.perk));
    }
    
    // Update FID labels
    if (_armorMaleFIDLabel) {
        _armorMaleFIDLabel->setText(QString("0x%1").arg(_armorMaleFID, 6, 16, QChar('0')));
    }
    if (_armorFemaleFIDLabel) {
        _armorFemaleFIDLabel->setText(QString("0x%1").arg(_armorFemaleFID, 6, 16, QChar('0')));
    }
    
    updateAIPriority();
}

void ProArmorWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro)) return;
    
    // Update data from UI
    if (_armorClassEdit) {
        _armorData.armorClass = static_cast<uint32_t>(_armorClassEdit->value());
    }
    
    // Save damage thresholds and resistances
    saveWidgetsToIntArray(_damageThresholdEdits, _armorData.damageThreshold, DAMAGE_TYPES_COUNT);
    saveWidgetsToIntArray(_damageResistEdits, _armorData.damageResist, DAMAGE_TYPES_COUNT);
    
    if (_armorPerkCombo) {
        _armorData.perk = static_cast<uint32_t>(_armorPerkCombo->currentIndex());
    }
    
    _armorData.armorMaleFID = _armorMaleFID;
    _armorData.armorFemaleFID = _armorFemaleFID;
    
    // Save to PRO - copy fields individually
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
    return pro && pro->type() == Pro::OBJECT_TYPE::ITEM && 
           pro->itemType() == Pro::ITEM_TYPE::ARMOR;
}

QString ProArmorWidget::getTabLabel() const {
    return "Armor";
}

void ProArmorWidget::updateAIPriority() {
    if (_armorAIPriorityLabel) {
        _armorAIPriorityLabel->setText(QString::number(calculateAIPriority()));
    }
}

int ProArmorWidget::calculateAIPriority() const {
    int priority = 0;
    
    // Add armor class
    if (_armorClassEdit) {
        priority += _armorClassEdit->value();
    }
    
    // Add all damage thresholds
    for (int i = 0; i < DAMAGE_TYPES_COUNT; ++i) {
        if (_damageThresholdEdits[i]) {
            priority += _damageThresholdEdits[i]->value();
        }
    }
    
    // Add all damage resistances
    for (int i = 0; i < DAMAGE_TYPES_COUNT; ++i) {
        if (_damageResistEdits[i]) {
            priority += _damageResistEdits[i]->value();
        }
    }
    
    return priority;
}

} // namespace geck