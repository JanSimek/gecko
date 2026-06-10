#include "SpatialScriptDialog.h"

#include "ScriptSelectorDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace geck {

SpatialScriptDialog::SpatialScriptDialog(const std::vector<std::string>& scriptNames, QWidget* parent)
    : QDialog(parent)
    , _scriptNames(scriptNames) {

    setWindowTitle("Add Spatial Script");

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    auto* scriptRow = new QHBoxLayout();
    _scriptLabel = new QLabel("None");
    auto* chooseButton = new QPushButton("Choose...");
    scriptRow->addWidget(_scriptLabel, 1);
    scriptRow->addWidget(chooseButton);
    formLayout->addRow("Script:", scriptRow);

    _tileSpin = new QSpinBox(this);
    _tileSpin->setRange(0, MAX_HEX_TILE);
    formLayout->addRow("Hex tile:", _tileSpin);

    _elevationCombo = new QComboBox(this);
    _elevationCombo->addItem("Elevation 1", 0);
    _elevationCombo->addItem("Elevation 2", 1);
    _elevationCombo->addItem("Elevation 3", 2);
    formLayout->addRow("Elevation:", _elevationCombo);

    _radiusSpin = new QSpinBox(this);
    _radiusSpin->setRange(0, MAX_RADIUS);
    _radiusSpin->setValue(3);
    formLayout->addRow("Radius:", _radiusSpin);

    mainLayout->addLayout(formLayout);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    _okButton = buttonBox->button(QDialogButtonBox::Ok);
    _okButton->setEnabled(false); // need a script first
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(chooseButton, &QPushButton::clicked, this, &SpatialScriptDialog::onChooseScript);
    mainLayout->addWidget(buttonBox);
}

void SpatialScriptDialog::onChooseScript() {
    ScriptSelectorDialog dialog(_scriptNames, _programIndex, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const int index = dialog.selectedIndex();
    if (index < 0) {
        return;
    }
    _programIndex = index;
    if (static_cast<size_t>(index) < _scriptNames.size()) {
        _scriptLabel->setText(QString("%1: %2").arg(index).arg(QString::fromStdString(_scriptNames[index])));
    } else {
        _scriptLabel->setText(QString("Script #%1").arg(index));
    }
    _okButton->setEnabled(true);
}

int SpatialScriptDialog::tile() const { return _tileSpin->value(); }
int SpatialScriptDialog::elevation() const { return _elevationCombo->currentData().toInt(); }
int SpatialScriptDialog::radius() const { return _radiusSpin->value(); }

} // namespace geck
