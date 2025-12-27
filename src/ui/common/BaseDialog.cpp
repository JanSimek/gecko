#include "BaseDialog.h"

namespace geck {

BaseDialog::BaseDialog(const QString& title, QWidget* parent, ButtonConfig buttons)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _buttonBox(nullptr) {

    setWindowTitle(title);
    setupDialogDefaults();
}

void BaseDialog::setupDialogDefaults() {
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
}

QVBoxLayout* BaseDialog::createMainLayout() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(
        ui::theme::spacing::MARGIN_LOOSE,
        ui::theme::spacing::MARGIN_LOOSE,
        ui::theme::spacing::MARGIN_LOOSE,
        ui::theme::spacing::MARGIN_LOOSE);
    _mainLayout->setSpacing(ui::theme::spacing::LOOSE);
    return _mainLayout;
}

QDialogButtonBox* BaseDialog::createButtonBox(ButtonConfig config) {
    switch (config) {
        case OkCancel:
            _buttonBox = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
            break;
        case OkOnly:
            _buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, this);
            break;
        case Custom:
            return nullptr;
    }

    connect(_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (_mainLayout) {
        _mainLayout->addWidget(_buttonBox);
    }

    return _buttonBox;
}

void BaseDialog::setDialogSize(int minWidth, int minHeight,
    int prefWidth, int prefHeight) {
    setMinimumSize(minWidth, minHeight);
    resize(prefWidth, prefHeight);
}

void BaseDialog::setDialogSize(int width, int height) {
    resize(width, height);
}

void BaseDialog::setFixedDialogSize(int width, int height) {
    setFixedSize(width, height);
}

} // namespace geck
