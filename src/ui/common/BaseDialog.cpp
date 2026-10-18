#include "BaseDialog.h"

namespace geck {

BaseDialog::BaseDialog(const QString& title, QWidget* parent)
    : QDialog(parent) {

    setWindowTitle(title);
    setupDialogDefaults();
}

void BaseDialog::setupDialogDefaults() {
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
}

QDialogButtonBox* BaseDialog::createButtonBox() {
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    return buttonBox;
}

} // namespace geck
