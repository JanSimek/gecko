#pragma once

#include "ProTabWidget.h"

namespace geck {

class ProAmmoWidget : public ProTabWidget {
    Q_OBJECT

public:
    explicit ProAmmoWidget(QWidget* parent = nullptr);
    ~ProAmmoWidget() override = default;

    void loadFromPro(const std::shared_ptr<Pro>& pro) override;
    void saveToPro(std::shared_ptr<Pro>& pro) override;
    bool canHandle(const std::shared_ptr<Pro>& pro) const override;
    QString getTabLabel() const override;

private:
    void setupUI();

    QComboBox* _caliberCombo;
    QSpinBox* _quantityEdit;
    QSpinBox* _damageModifierEdit;
    QSpinBox* _damageResistModifierEdit;
    QSpinBox* _damageMultiplierEdit;
    QSpinBox* _damageTypeModifierEdit;

    ProAmmoData _ammoData;
};

} // namespace geck
