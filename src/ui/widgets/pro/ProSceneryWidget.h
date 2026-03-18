#pragma once

#include "ProTabWidget.h"

class QGroupBox;

namespace geck {

class ProSceneryWidget : public ProTabWidget {
    Q_OBJECT

public:
    explicit ProSceneryWidget(resource::GameResources& resources, QWidget* parent = nullptr);
    ~ProSceneryWidget() override = default;

    void loadFromPro(const std::shared_ptr<Pro>& pro) override;
    void saveToPro(std::shared_ptr<Pro>& pro) override;
    bool canHandle(const std::shared_ptr<Pro>& pro) const override;
    QString getTabLabel() const override;

private:
    void setupUI();
    void updateTypeSpecificGroups();
    Pro::SCENERY_TYPE currentSceneryType() const;

    QComboBox* _materialIdCombo;
    QSpinBox* _soundIdEdit;
    QComboBox* _typeCombo;

    QGroupBox* _doorGroup;
    QGroupBox* _stairsGroup;
    QGroupBox* _elevatorGroup;
    QGroupBox* _ladderGroup;
    QGroupBox* _genericGroup;

    QCheckBox* _doorWalkThroughCheck;
    QSpinBox* _doorUnknownEdit;
    QSpinBox* _stairsDestTileEdit;
    QSpinBox* _stairsDestElevationEdit;
    QSpinBox* _elevatorTypeEdit;
    QSpinBox* _elevatorLevelEdit;
    QSpinBox* _ladderDestTileElevationEdit;
    QSpinBox* _genericUnknownEdit;
};

} // namespace geck
