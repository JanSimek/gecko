#include "ProDrugWidget.h"
#include "../../theme/ThemeManager.h"
#include "../../UIConstants.h"
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>
#include <climits>

namespace geck {

ProDrugWidget::ProDrugWidget(QWidget* parent)
    : ProTabWidget(parent) {

    // Initialize arrays
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

    setupUI();
}

void ProDrugWidget::setupUI() {
    // Drug tab uses single column layout (full width)

    // === Stat Effects Group ===
    QGroupBox* effectsGroup = new QGroupBox("Modify Stats");
    effectsGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QGridLayout* effectsGridLayout = new QGridLayout(effectsGroup);
    effectsGridLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN_VERTICAL, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    effectsGridLayout->setSpacing(ui::constants::SPACING_FORM);

    // Header row (row 0)
    effectsGridLayout->addWidget(new QLabel(""), 0, 0); // Empty space for stat labels

    QLabel* statHeader = new QLabel("Stat");
    statHeader->setAlignment(Qt::AlignCenter);
    statHeader->setStyleSheet(ui::theme::styles::boldLabel());
    effectsGridLayout->addWidget(statHeader, 0, 1);

    QLabel* immediateHeader = new QLabel("Immediate");
    immediateHeader->setAlignment(Qt::AlignCenter);
    immediateHeader->setStyleSheet(ui::theme::styles::boldLabel());
    effectsGridLayout->addWidget(immediateHeader, 0, 2);

    QLabel* midTimeHeader = new QLabel("Mid-time");
    midTimeHeader->setAlignment(Qt::AlignCenter);
    midTimeHeader->setStyleSheet(ui::theme::styles::boldLabel());
    effectsGridLayout->addWidget(midTimeHeader, 0, 3);

    QLabel* longTimeHeader = new QLabel("Long-time");
    longTimeHeader->setAlignment(Qt::AlignCenter);
    longTimeHeader->setStyleSheet(ui::theme::styles::boldLabel());
    effectsGridLayout->addWidget(longTimeHeader, 0, 4);

    // Set column stretches for proper sizing
    effectsGridLayout->setColumnStretch(0, 0); // Fixed width for labels
    effectsGridLayout->setColumnStretch(1, 2); // Stretch for combo boxes
    effectsGridLayout->setColumnStretch(2, 1); // Stretch for spin boxes
    effectsGridLayout->setColumnStretch(3, 1); // Stretch for spin boxes
    effectsGridLayout->setColumnStretch(4, 1); // Stretch for spin boxes

    // Create rows for each stat (rows 1-3)
    for (int i = 0; i < NUM_DRUG_STATS; ++i) {
        int row = i + 1;

        // Stat label (column 0)
        QLabel* statLabel = new QLabel(QString("Stat %1:").arg(i + 1));
        effectsGridLayout->addWidget(statLabel, row, 0);

        // Stat dropdown (column 1)
        _drugStatCombos[i] = createComboBox({}, QString("Stat %1 to modify (None=no effect)").arg(i + 1));
        effectsGridLayout->addWidget(_drugStatCombos[i], row, 1);

        // Immediate effect value (column 2)
        _drugStatAmountEdits[i] = createSpinBox(INT_MIN, INT_MAX, QString("Immediate effect amount for stat %1").arg(i + 1));
        effectsGridLayout->addWidget(_drugStatAmountEdits[i], row, 2);

        // Mid-time effect value (column 3)
        _drugFirstStatAmountEdits[i] = createSpinBox(INT_MIN, INT_MAX, QString("Mid-time effect amount for stat %1").arg(i + 1));
        effectsGridLayout->addWidget(_drugFirstStatAmountEdits[i], row, 3);

        // Long-time effect value (column 4)
        _drugSecondStatAmountEdits[i] = createSpinBox(INT_MIN, INT_MAX, QString("Long-time effect amount for stat %1").arg(i + 1));
        effectsGridLayout->addWidget(_drugSecondStatAmountEdits[i], row, 4);
    }

    _mainLayout->addWidget(effectsGroup);

    // Add some spacing
    _mainLayout->addSpacing(ui::constants::SPACING_WIDE);

    // === Effect Timing Group ===
    QGroupBox* timingGroup = new QGroupBox("Effect Timing");
    timingGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* timingLayout = createStandardFormLayout();

    _drugFirstDelayEdit = createSpinBox(0, INT_MAX, "Delay in game minutes before mid-time effect");
    timingLayout->addRow("Mid-time Delay:", _drugFirstDelayEdit);

    _drugSecondDelayEdit = createSpinBox(0, INT_MAX, "Delay in game minutes before long-time effect");
    timingLayout->addRow("Long-time Delay:", _drugSecondDelayEdit);

    timingGroup->setLayout(timingLayout);
    _mainLayout->addWidget(timingGroup);

    // Add some spacing
    _mainLayout->addSpacing(ui::constants::SPACING_WIDE);

    // === Addiction Group ===
    QGroupBox* addictionGroup = new QGroupBox("Addiction");
    addictionGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* addictionLayout = createStandardFormLayout();

    _drugAddictionChanceEdit = createSpinBox(0, INT_MAX, "Percentage chance of addiction");
    _drugAddictionChanceEdit->setSuffix("%");
    addictionLayout->addRow("Rate:", _drugAddictionChanceEdit);

    _drugAddictionPerkCombo = createComboBox({}, "Perk applied when addicted");
    addictionLayout->addRow("Effect:", _drugAddictionPerkCombo);

    _drugAddictionDelayEdit = createSpinBox(0, INT_MAX, "Delay in game minutes before addiction effect is applied");
    addictionLayout->addRow("Onset:", _drugAddictionDelayEdit);

    addictionGroup->setLayout(addictionLayout);
    _mainLayout->addWidget(addictionGroup);

    // Add stretch to push content to top
    _mainLayout->addStretch();
}

void ProDrugWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    // Load drug data - copy fields individually
    _drugData.stat0 = pro->drugData.stat0;
    _drugData.stat1 = pro->drugData.stat1;
    _drugData.stat2 = pro->drugData.stat2;
    _drugData.amount0 = pro->drugData.amount0;
    _drugData.amount1 = pro->drugData.amount1;
    _drugData.amount2 = pro->drugData.amount2;
    _drugData.duration1 = pro->drugData.duration1;
    _drugData.amount0_1 = pro->drugData.amount0_1;
    _drugData.amount1_1 = pro->drugData.amount1_1;
    _drugData.amount2_1 = pro->drugData.amount2_1;
    _drugData.duration2 = pro->drugData.duration2;
    _drugData.amount0_2 = pro->drugData.amount0_2;
    _drugData.amount1_2 = pro->drugData.amount1_2;
    _drugData.amount2_2 = pro->drugData.amount2_2;
    _drugData.addictionRate = pro->drugData.addictionRate;
    _drugData.addictionEffect = pro->drugData.addictionEffect;
    _drugData.addictionOnset = pro->drugData.addictionOnset;

    // Map data to UI arrays
    uint32_t stats[NUM_DRUG_STATS] = { _drugData.stat0, _drugData.stat1, _drugData.stat2 };
    int32_t amounts[NUM_DRUG_STATS] = { _drugData.amount0, _drugData.amount1, _drugData.amount2 };
    int32_t firstAmounts[NUM_DRUG_STATS] = { _drugData.amount0_1, _drugData.amount1_1, _drugData.amount2_1 };
    int32_t secondAmounts[NUM_DRUG_STATS] = { _drugData.amount0_2, _drugData.amount1_2, _drugData.amount2_2 };

    // Update UI controls
    for (int i = 0; i < NUM_DRUG_STATS; ++i) {
        if (_drugStatCombos[i]) {
            // Convert stat ID: -1/0xFFFF means "None" (index 0), otherwise stat ID + 1
            int comboIndex = (stats[i] == 0xFFFFFFFF) ? 0 : static_cast<int>(stats[i]) + 1;
            _drugStatCombos[i]->setCurrentIndex(comboIndex);
        }
        if (_drugStatAmountEdits[i])
            _drugStatAmountEdits[i]->setValue(static_cast<int>(amounts[i]));
        if (_drugFirstStatAmountEdits[i])
            _drugFirstStatAmountEdits[i]->setValue(static_cast<int>(firstAmounts[i]));
        if (_drugSecondStatAmountEdits[i])
            _drugSecondStatAmountEdits[i]->setValue(static_cast<int>(secondAmounts[i]));
    }

    if (_drugFirstDelayEdit)
        _drugFirstDelayEdit->setValue(static_cast<int>(_drugData.duration1));
    if (_drugSecondDelayEdit)
        _drugSecondDelayEdit->setValue(static_cast<int>(_drugData.duration2));
    if (_drugAddictionChanceEdit)
        _drugAddictionChanceEdit->setValue(static_cast<int>(_drugData.addictionRate));
    if (_drugAddictionPerkCombo)
        _drugAddictionPerkCombo->setCurrentIndex(static_cast<int>(_drugData.addictionEffect));
    if (_drugAddictionDelayEdit)
        _drugAddictionDelayEdit->setValue(static_cast<int>(_drugData.addictionOnset));
}

void ProDrugWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    // Update data from UI
    uint32_t stats[NUM_DRUG_STATS];
    int32_t amounts[NUM_DRUG_STATS];
    int32_t firstAmounts[NUM_DRUG_STATS];
    int32_t secondAmounts[NUM_DRUG_STATS];

    for (int i = 0; i < NUM_DRUG_STATS; ++i) {
        if (_drugStatCombos[i]) {
            // Convert combo index: 0 means "None" (-1/0xFFFF), otherwise index - 1
            int comboIndex = _drugStatCombos[i]->currentIndex();
            stats[i] = (comboIndex == 0) ? 0xFFFFFFFF : static_cast<uint32_t>(comboIndex - 1);
        }
        if (_drugStatAmountEdits[i])
            amounts[i] = static_cast<int32_t>(_drugStatAmountEdits[i]->value());
        if (_drugFirstStatAmountEdits[i])
            firstAmounts[i] = static_cast<int32_t>(_drugFirstStatAmountEdits[i]->value());
        if (_drugSecondStatAmountEdits[i])
            secondAmounts[i] = static_cast<int32_t>(_drugSecondStatAmountEdits[i]->value());
    }

    // Map arrays back to data structure
    _drugData.stat0 = stats[0];
    _drugData.stat1 = stats[1];
    _drugData.stat2 = stats[2];
    _drugData.amount0 = amounts[0];
    _drugData.amount1 = amounts[1];
    _drugData.amount2 = amounts[2];
    _drugData.amount0_1 = firstAmounts[0];
    _drugData.amount1_1 = firstAmounts[1];
    _drugData.amount2_1 = firstAmounts[2];
    _drugData.amount0_2 = secondAmounts[0];
    _drugData.amount1_2 = secondAmounts[1];
    _drugData.amount2_2 = secondAmounts[2];

    if (_drugFirstDelayEdit)
        _drugData.duration1 = static_cast<uint32_t>(_drugFirstDelayEdit->value());
    if (_drugSecondDelayEdit)
        _drugData.duration2 = static_cast<uint32_t>(_drugSecondDelayEdit->value());
    if (_drugAddictionChanceEdit)
        _drugData.addictionRate = static_cast<uint32_t>(_drugAddictionChanceEdit->value());
    if (_drugAddictionPerkCombo)
        _drugData.addictionEffect = static_cast<uint32_t>(_drugAddictionPerkCombo->currentIndex());
    if (_drugAddictionDelayEdit)
        _drugData.addictionOnset = static_cast<uint32_t>(_drugAddictionDelayEdit->value());

    // Save to PRO - copy fields individually
    pro->drugData.stat0 = _drugData.stat0;
    pro->drugData.stat1 = _drugData.stat1;
    pro->drugData.stat2 = _drugData.stat2;
    pro->drugData.amount0 = _drugData.amount0;
    pro->drugData.amount1 = _drugData.amount1;
    pro->drugData.amount2 = _drugData.amount2;
    pro->drugData.duration1 = _drugData.duration1;
    pro->drugData.amount0_1 = _drugData.amount0_1;
    pro->drugData.amount1_1 = _drugData.amount1_1;
    pro->drugData.amount2_1 = _drugData.amount2_1;
    pro->drugData.duration2 = _drugData.duration2;
    pro->drugData.amount0_2 = _drugData.amount0_2;
    pro->drugData.amount1_2 = _drugData.amount1_2;
    pro->drugData.amount2_2 = _drugData.amount2_2;
    pro->drugData.addictionRate = _drugData.addictionRate;
    pro->drugData.addictionEffect = _drugData.addictionEffect;
    pro->drugData.addictionOnset = _drugData.addictionOnset;
}

bool ProDrugWidget::canHandle(const std::shared_ptr<Pro>& pro) const {
    return pro && pro->type() == Pro::OBJECT_TYPE::ITEM && pro->itemType() == Pro::ITEM_TYPE::DRUG;
}

QString ProDrugWidget::getTabLabel() const {
    return "Drug";
}

void ProDrugWidget::setStatNames(const QStringList& statNames) {
    _statNames = statNames;

    // Update all stat combo boxes
    QStringList comboItems;
    comboItems << "None";     // Index 0 for no effect
    comboItems << _statNames; // Indices 1+ for actual stats

    for (int i = 0; i < NUM_DRUG_STATS; ++i) {
        if (_drugStatCombos[i]) {
            int currentIndex = _drugStatCombos[i]->currentIndex();
            _drugStatCombos[i]->clear();
            _drugStatCombos[i]->addItems(comboItems);
            _drugStatCombos[i]->setCurrentIndex(currentIndex);
        }
    }
}

void ProDrugWidget::setPerkNames(const QStringList& perkNames) {
    _perkNames = perkNames;

    // Update addiction perk combo box
    if (_drugAddictionPerkCombo) {
        int currentIndex = _drugAddictionPerkCombo->currentIndex();
        _drugAddictionPerkCombo->clear();
        _drugAddictionPerkCombo->addItems(_perkNames);
        _drugAddictionPerkCombo->setCurrentIndex(currentIndex);
    }
}

} // namespace geck