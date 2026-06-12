#include "LightPropertiesDialog.h"

#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace geck {

LightPropertiesDialog::LightPropertiesDialog(uint32_t lightRadius, uint32_t lightIntensity, QWidget* parent)
    : BaseDialog("Light Properties", parent) {

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    _distanceSpin = new QSpinBox(this);
    _distanceSpin->setRange(0, MAX_DISTANCE);
    _distanceSpin->setValue(static_cast<int>(std::min<uint32_t>(lightRadius, MAX_DISTANCE)));
    _distanceSpin->setSuffix(" hexes");
    formLayout->addRow("Light distance:", _distanceSpin);

    _intensitySpin = new QSpinBox(this);
    _intensitySpin->setRange(0, MAX_PERCENT);
    // Raw intensity -> percentage. Clamp to the engine's 100% ceiling.
    uint32_t clampedRaw = std::min<uint32_t>(lightIntensity, LIGHT_SCALE);
    int percent = static_cast<int>((static_cast<uint64_t>(clampedRaw) * MAX_PERCENT) / LIGHT_SCALE);
    _intensitySpin->setValue(percent);
    _intensitySpin->setSuffix(" %");
    formLayout->addRow("Light intensity:", _intensitySpin);

    mainLayout->addLayout(formLayout);

    mainLayout->addWidget(createButtonBox());
}

uint32_t LightPropertiesDialog::getLightRadius() const {
    return static_cast<uint32_t>(_distanceSpin->value());
}

uint32_t LightPropertiesDialog::getLightIntensity() const {
    // Percentage -> raw, matching the engine scale. 100% maps exactly to LIGHT_SCALE.
    return static_cast<uint32_t>((static_cast<uint64_t>(_intensitySpin->value()) * LIGHT_SCALE) / MAX_PERCENT);
}

} // namespace geck
