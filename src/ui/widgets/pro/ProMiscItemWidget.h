#pragma once

#include "ProTabWidget.h"

namespace geck {

class ProMiscItemWidget : public ProTabWidget {
    Q_OBJECT

public:
    explicit ProMiscItemWidget(resource::GameResources& resources, QWidget* parent = nullptr);
    ~ProMiscItemWidget() override = default;

    void loadFromPro(const std::shared_ptr<Pro>& pro) override;
    void saveToPro(std::shared_ptr<Pro>& pro) override;
    bool canHandle(const std::shared_ptr<Pro>& pro) const override;
    QString getTabLabel() const override;

private:
    void setupUI();

    QSpinBox* _powerTypeEdit;
    QSpinBox* _chargesEdit;

    ProMiscData _miscData;
};

} // namespace geck
