#include "ProMiscItemWidget.h"

#include "../../theme/ThemeManager.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <climits>

namespace geck {

ProMiscItemWidget::ProMiscItemWidget(QWidget* parent)
    : ProTabWidget(parent)
    , _powerTypeEdit(nullptr)
    , _chargesEdit(nullptr) {
    setupUI();
}

void ProMiscItemWidget::setupUI() {
    QGroupBox* miscGroup = createStandardGroupBox("Misc Item Properties");
    miscGroup->setStyleSheet(ui::theme::styles::boldGroupBox());
    QFormLayout* miscLayout = createStandardFormLayout();
    static_cast<QVBoxLayout*>(miscGroup->layout())->addLayout(miscLayout);

    _powerTypeEdit = createSpinBox(0, INT_MAX, "Raw power type value stored in the PRO");
    miscLayout->addRow("Power Type:", _powerTypeEdit);

    _chargesEdit = createSpinBox(0, INT_MAX, "Number of charges available on this item");
    miscLayout->addRow("Charges:", _chargesEdit);

    _mainLayout->addWidget(miscGroup);
    _mainLayout->addStretch();
}

void ProMiscItemWidget::loadFromPro(const std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    _miscData.powerType = pro->miscData.powerType;
    _miscData.charges = pro->miscData.charges;

    if (_powerTypeEdit)
        _powerTypeEdit->setValue(static_cast<int>(_miscData.powerType));
    if (_chargesEdit)
        _chargesEdit->setValue(static_cast<int>(_miscData.charges));
}

void ProMiscItemWidget::saveToPro(std::shared_ptr<Pro>& pro) {
    if (!pro || !canHandle(pro))
        return;

    if (_powerTypeEdit)
        _miscData.powerType = static_cast<uint32_t>(_powerTypeEdit->value());
    if (_chargesEdit)
        _miscData.charges = static_cast<uint32_t>(_chargesEdit->value());

    pro->miscData.powerType = _miscData.powerType;
    pro->miscData.charges = _miscData.charges;
}

bool ProMiscItemWidget::canHandle(const std::shared_ptr<Pro>& pro) const {
    return pro && pro->type() == Pro::OBJECT_TYPE::ITEM && pro->itemType() == Pro::ITEM_TYPE::MISC;
}

QString ProMiscItemWidget::getTabLabel() const {
    return "Misc Item";
}

} // namespace geck
