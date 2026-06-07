#include "ProWallWidget.h"
#include <QFormLayout>
#include <QGroupBox>

namespace geck {

ProWallWidget::ProWallWidget(resource::GameResources& resources, QWidget* parent)
    : ProTabWidget(resources, parent)
    , _materialIdCombo(nullptr) {
    setupUI();
}

void ProWallWidget::setupUI() {
    QGroupBox* wallGroup = createStandardGroupBox("Wall Properties");
    QFormLayout* wallLayout = createStandardFormLayout();

    _materialIdCombo = createMaterialComboBox("Material type for this wall");
    wallLayout->addRow("Material:", _materialIdCombo);

    wallGroup->setLayout(wallLayout);
    _mainLayout->addWidget(wallGroup);
    _mainLayout->addStretch();
}

void ProWallWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    _wallData.materialId = pro->wallData.materialId;
    setComboIndexSafe(_materialIdCombo, _wallData.materialId);
}

void ProWallWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    _wallData.materialId = static_cast<uint32_t>(getComboIndex(_materialIdCombo));
    pro->wallData.materialId = _wallData.materialId;
}

bool ProWallWidget::canHandle(const std::shared_ptr<Pro>& pro) const {
    return pro && pro->type() == Pro::OBJECT_TYPE::WALL;
}

QString ProWallWidget::getTabLabel() const {
    return "Wall";
}

} // namespace geck
