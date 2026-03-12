#include "ProTileWidget.h"
#include <QFormLayout>
#include <QGroupBox>

namespace geck {

ProTileWidget::ProTileWidget(resource::GameResources& resources, QWidget* parent)
    : ProTabWidget(resources, parent)
    , _materialIdCombo(nullptr) {
    setupUI();
}

void ProTileWidget::setupUI() {
    // Tile properties group
    QGroupBox* tileGroup = createStandardGroupBox("Tile Properties");
    QFormLayout* tileLayout = createStandardFormLayout();

    // Material ID combo box
    _materialIdCombo = createMaterialComboBox("Material type for this tile");
    tileLayout->addRow("Material:", _materialIdCombo);

    tileGroup->setLayout(tileLayout);
    _mainLayout->addWidget(tileGroup);

    // Add stretch to push content to top
    _mainLayout->addStretch();
}

void ProTileWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    // Load tile-specific data
    _tileData.materialId = pro->tileData.materialId;

    // Update UI
    setComboIndexSafe(_materialIdCombo, _tileData.materialId);
}

void ProTileWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    // Update data from UI
    _tileData.materialId = static_cast<uint32_t>(getComboIndex(_materialIdCombo));

    // Save to PRO
    pro->tileData.materialId = _tileData.materialId;
}

bool ProTileWidget::canHandle(const std::shared_ptr<Pro>& pro) const {
    return pro && pro->type() == Pro::OBJECT_TYPE::TILE;
}

QString ProTileWidget::getTabLabel() const {
    return "Tile";
}

} // namespace geck
