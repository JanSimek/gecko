#include "ProDrugWidget.h"
#include "ui/theme/ThemeManager.h"
#include "ui/UIConstants.h"
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>
#include <climits>

namespace geck {

ProDrugWidget::ProDrugWidget(resource::GameResources& resources, QWidget* parent)
    : ProTabWidget(resources, parent) {

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
    populateStatOptions();
    populatePerkOptions();
}

void ProDrugWidget::setupUI() {
    // === Stat Effects Group ===
    QGroupBox* effectsGroup = new QGroupBox("Modify Stats");
    effectsGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QGridLayout* effectsGridLayout = new QGridLayout(effectsGroup);
    effectsGridLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN_VERTICAL, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    effectsGridLayout->setSpacing(ui::constants::SPACING_FORM);

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

    effectsGridLayout->setColumnStretch(0, 0);
    effectsGridLayout->setColumnStretch(1, 2);
    effectsGridLayout->setColumnStretch(2, 1);
    effectsGridLayout->setColumnStretch(3, 1);
    effectsGridLayout->setColumnStretch(4, 1);

    for (int i = 0; i < NUM_DRUG_STATS; ++i) {
        int row = i + 1;

        QLabel* statLabel = new QLabel(QString("Stat %1:").arg(i + 1));
        effectsGridLayout->addWidget(statLabel, row, 0);

        _drugStatCombos[i] = createComboBox(QStringList{}, QString("Stat %1 to modify (None=no effect)").arg(i + 1));
        effectsGridLayout->addWidget(_drugStatCombos[i], row, 1);

        _drugStatAmountEdits[i] = createSpinBox(INT_MIN, INT_MAX, QString("Immediate effect amount for stat %1").arg(i + 1));
        effectsGridLayout->addWidget(_drugStatAmountEdits[i], row, 2);

        _drugFirstStatAmountEdits[i] = createSpinBox(INT_MIN, INT_MAX, QString("Mid-time effect amount for stat %1").arg(i + 1));
        effectsGridLayout->addWidget(_drugFirstStatAmountEdits[i], row, 3);

        _drugSecondStatAmountEdits[i] = createSpinBox(INT_MIN, INT_MAX, QString("Long-time effect amount for stat %1").arg(i + 1));
        effectsGridLayout->addWidget(_drugSecondStatAmountEdits[i], row, 4);
    }

    _mainLayout->addWidget(effectsGroup);
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
    _mainLayout->addSpacing(ui::constants::SPACING_WIDE);

    // === Addiction Group ===
    QGroupBox* addictionGroup = new QGroupBox("Addiction");
    addictionGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* addictionLayout = createStandardFormLayout();

    _drugAddictionChanceEdit = createSpinBox(0, INT_MAX, "Percentage chance of addiction");
    _drugAddictionChanceEdit->setSuffix("%");
    addictionLayout->addRow("Rate:", _drugAddictionChanceEdit);

    _drugAddictionPerkCombo = createComboBox(QVector<game::enums::EnumOption>{}, "Perk applied when addicted");
    addictionLayout->addRow("Effect:", _drugAddictionPerkCombo);

    _drugAddictionDelayEdit = createSpinBox(0, INT_MAX, "Delay in game minutes before addiction effect is applied");
    addictionLayout->addRow("Onset:", _drugAddictionDelayEdit);

    addictionGroup->setLayout(addictionLayout);
    _mainLayout->addWidget(addictionGroup);
    _mainLayout->addStretch();
}

void ProDrugWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    _drugData = pro->drugData;

    uint32_t stats[NUM_DRUG_STATS] = { _drugData.stat0, _drugData.stat1, _drugData.stat2 };
    int32_t amounts[NUM_DRUG_STATS] = { _drugData.amount0, _drugData.amount1, _drugData.amount2 };
    int32_t firstAmounts[NUM_DRUG_STATS] = { _drugData.amount0_1, _drugData.amount1_1, _drugData.amount2_1 };
    int32_t secondAmounts[NUM_DRUG_STATS] = { _drugData.amount0_2, _drugData.amount1_2, _drugData.amount2_2 };

    for (int i = 0; i < NUM_DRUG_STATS; ++i) {
        if (_drugStatCombos[i]) {
            // 0xFFFFFFFF on disk means "no stat"; otherwise it is the engine stat id.
            const int statValue = (stats[i] == 0xFFFFFFFFu) ? STAT_NONE_VALUE : static_cast<int>(stats[i]);
            setComboValue(_drugStatCombos[i], statValue);
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
    setComboValue(_drugAddictionPerkCombo, static_cast<int>(_drugData.addictionEffect));
    if (_drugAddictionDelayEdit)
        _drugAddictionDelayEdit->setValue(static_cast<int>(_drugData.addictionOnset));
}

void ProDrugWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    uint32_t stats[NUM_DRUG_STATS];
    int32_t amounts[NUM_DRUG_STATS];
    int32_t firstAmounts[NUM_DRUG_STATS];
    int32_t secondAmounts[NUM_DRUG_STATS];

    for (int i = 0; i < NUM_DRUG_STATS; ++i) {
        if (_drugStatCombos[i]) {
            const int statValue = getComboValue(_drugStatCombos[i], STAT_NONE_VALUE);
            stats[i] = (statValue == STAT_NONE_VALUE) ? 0xFFFFFFFFu : static_cast<uint32_t>(statValue);
        }
        if (_drugStatAmountEdits[i])
            amounts[i] = static_cast<int32_t>(_drugStatAmountEdits[i]->value());
        if (_drugFirstStatAmountEdits[i])
            firstAmounts[i] = static_cast<int32_t>(_drugFirstStatAmountEdits[i]->value());
        if (_drugSecondStatAmountEdits[i])
            secondAmounts[i] = static_cast<int32_t>(_drugSecondStatAmountEdits[i]->value());
    }

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
    _drugData.addictionEffect = static_cast<uint32_t>(getComboValue(_drugAddictionPerkCombo));
    if (_drugAddictionDelayEdit)
        _drugData.addictionOnset = static_cast<uint32_t>(_drugAddictionDelayEdit->value());

    pro->drugData = _drugData;
}

bool ProDrugWidget::canHandle(const std::shared_ptr<Pro>& pro) const {
    return pro && pro->type() == Pro::OBJECT_TYPE::ITEM && pro->itemType() == Pro::ITEM_TYPE::DRUG;
}

QString ProDrugWidget::getTabLabel() const {
    return "Drug";
}

void ProDrugWidget::populateStatOptions() {
    const QStringList statNames = game::enums::statNames(_resources);

    for (int i = 0; i < NUM_DRUG_STATS; ++i) {
        if (_drugStatCombos[i]) {
            // Preserve the selection by value, not by list position.
            const int currentValue = getComboValue(_drugStatCombos[i], STAT_NONE_VALUE);
            _drugStatCombos[i]->clear();
            // Bind each item to its engine stat id so loads/saves go through value
            // mapping (set/getComboValue) instead of assuming index == stat id.
            _drugStatCombos[i]->addItem("None", STAT_NONE_VALUE);
            for (int stat = 0; stat < statNames.size(); ++stat) {
                _drugStatCombos[i]->addItem(statNames[stat], stat);
            }
            setComboValue(_drugStatCombos[i], currentValue);
        }
    }
}

void ProDrugWidget::populatePerkOptions() {
    if (_drugAddictionPerkCombo) {
        int currentValue = getComboValue(_drugAddictionPerkCombo);
        _drugAddictionPerkCombo->clear();
        const QVector<game::enums::EnumOption> perkOptions = game::enums::allPerkOptions(_resources);
        for (const auto& perkOption : perkOptions) {
            _drugAddictionPerkCombo->addItem(perkOption.label, perkOption.value);
        }
        setComboValue(_drugAddictionPerkCombo, currentValue);
    }
}

} // namespace geck
