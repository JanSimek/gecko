#include "ProTabWidget.h"
#include "../../GameEnums.h"
#include "../../UIConstants.h"

namespace geck {

ProTabWidget::ProTabWidget(QWidget* parent)
    : QWidget(parent)
    , _mainLayout(new QVBoxLayout(this)) {
    _mainLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    _mainLayout->setSpacing(FORM_SPACING);
}

QSpinBox* ProTabWidget::createSpinBox(int min, int max, const QString& tooltip) {
    QSpinBox* spinBox = new QSpinBox();
    spinBox->setMinimum(min);
    spinBox->setMaximum(max);
    if (!tooltip.isEmpty()) {
        spinBox->setToolTip(tooltip);
    }
    connectSpinBox(spinBox);
    return spinBox;
}

QSpinBox* ProTabWidget::createHexSpinBox(int max, const QString& tooltip) {
    QSpinBox* spinBox = new QSpinBox();
    spinBox->setMinimum(0);
    spinBox->setMaximum(max);
    spinBox->setDisplayIntegerBase(16);
    spinBox->setPrefix("0x");
    if (!tooltip.isEmpty()) {
        spinBox->setToolTip(tooltip);
    }
    connectSpinBox(spinBox);
    return spinBox;
}

QComboBox* ProTabWidget::createComboBox(const QStringList& items, const QString& tooltip) {
    QComboBox* comboBox = new QComboBox();
    comboBox->addItems(items);
    if (!tooltip.isEmpty()) {
        comboBox->setToolTip(tooltip);
    }
    connectComboBox(comboBox);
    return comboBox;
}

QComboBox* ProTabWidget::createMaterialComboBox(const QString& tooltip) {
    return createComboBox(getMaterialNames(), tooltip);
}

QFormLayout* ProTabWidget::createStandardFormLayout() {
    QFormLayout* layout = new QFormLayout();
    layout->setSpacing(FORM_SPACING);
    layout->setContentsMargins(0, 0, 0, 0);
    return layout;
}

QGroupBox* ProTabWidget::createStandardGroupBox(const QString& title) {
    QGroupBox* groupBox = new QGroupBox(title);
    QVBoxLayout* layout = new QVBoxLayout(groupBox);
    layout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    layout->setSpacing(FORM_SPACING);
    return groupBox;
}

void ProTabWidget::connectSpinBox(QSpinBox* spinBox) {
    if (!spinBox)
        return;
    connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged),
        this, &ProTabWidget::onFieldChanged);
}

void ProTabWidget::connectComboBox(QComboBox* comboBox) {
    if (!comboBox)
        return;
    connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ProTabWidget::onFieldChanged);
}

void ProTabWidget::connectCheckBox(QCheckBox* checkBox) {
    if (!checkBox)
        return;
    connect(checkBox, &QCheckBox::toggled, this, &ProTabWidget::onFieldChanged);
}

void ProTabWidget::loadIntArrayToWidgets(QSpinBox** widgets, const uint32_t* values, int count) {
    for (int i = 0; i < count; ++i) {
        if (widgets[i] && values) {
            widgets[i]->setValue(static_cast<int>(values[i]));
        }
    }
}

void ProTabWidget::saveWidgetsToIntArray(QSpinBox** widgets, uint32_t* values, int count) {
    for (int i = 0; i < count; ++i) {
        if (widgets[i] && values) {
            values[i] = static_cast<uint32_t>(widgets[i]->value());
        }
    }
}

void ProTabWidget::setComboIndex(QComboBox* combo, int index) {
    if (combo)
        combo->setCurrentIndex(index);
}

int ProTabWidget::getComboIndex(QComboBox* combo, int defaultValue) {
    return combo ? combo->currentIndex() : defaultValue;
}

void ProTabWidget::setComboIndexSafe(QComboBox* combo, uint32_t index) {
    if (combo && index < static_cast<uint32_t>(combo->count()))
        combo->setCurrentIndex(static_cast<int>(index));
}

QStringList ProTabWidget::getMaterialNames() {
    return game::enums::materialTypes();
}

void ProTabWidget::createSpinBoxArray(QSpinBox** array, int count,
    int min, int max,
    const QString& tooltipTemplate,
    const QStringList& labels) {
    for (int i = 0; i < count; ++i) {
        QString tooltip;
        if (!tooltipTemplate.isEmpty()) {
            if (!labels.isEmpty() && i < labels.size()) {
                tooltip = tooltipTemplate.arg(labels[i]);
            } else {
                tooltip = tooltipTemplate.arg(i);
            }
        }
        array[i] = createSpinBox(min, max, tooltip);
    }
}

void ProTabWidget::createCompactSpinBoxArray(QSpinBox** array, int count,
    int min, int max, int fixedWidth,
    const QString& tooltipTemplate,
    const QStringList& labels) {
    createSpinBoxArray(array, count, min, max, tooltipTemplate, labels);

    for (int i = 0; i < count; ++i) {
        if (array[i]) {
            array[i]->setFixedWidth(fixedWidth);
        }
    }
}

void ProTabWidget::onFieldChanged() {
    emit fieldChanged();
}

} // namespace geck
