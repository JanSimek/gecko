#pragma once

#include <QDialog>

#include <cstdint>

class QSpinBox;

namespace geck {

/// @brief Editor for an object instance's light distance and intensity.
///
/// Mirrors the fallout2-ce mapper instance light editor: distance is 0-8 hexes,
/// intensity is entered as a 0-100% value and stored raw against the engine's
/// light scale (0x10000 == 100%). See object.cc objectSetLight and
/// mp_instance.cc (kInstMaxLightDistance / kInstMaxLightPct / kInstLightScale).
class LightPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    LightPropertiesDialog(uint32_t lightRadius, uint32_t lightIntensity, QWidget* parent = nullptr);

    uint32_t getLightRadius() const;    ///< 0-8 hexes
    uint32_t getLightIntensity() const; ///< raw 0-65536

    static constexpr int MAX_DISTANCE = 8;
    static constexpr int MAX_PERCENT = 100;
    static constexpr uint32_t LIGHT_SCALE = 0x10000; // 65536 == 100%

private:
    QSpinBox* _distanceSpin;
    QSpinBox* _intensitySpin;
};

} // namespace geck
