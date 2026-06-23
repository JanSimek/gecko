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

#include "editor/HexagonGrid.h"

namespace geck {

SpatialScriptDialog::SpatialScriptDialog(const std::vector<ScriptSelectorDialog::Entry>& scripts, QWidget* parent)
    : BaseDialog("Add Spatial Script", parent)
    , _scripts(scripts) {

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    auto* scriptRow = new QHBoxLayout();
    _scriptLabel = new QLabel("None");
    auto* chooseButton = new QPushButton("Choose...");
    scriptRow->addWidget(_scriptLabel, 1);
    scriptRow->addWidget(chooseButton);
    formLayout->addRow("Script:", scriptRow);

    _tileSpin = new QSpinBox(this);
    _tileSpin->setRange(0, HexagonGrid::POSITION_COUNT - 1);
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

    auto* buttonBox = createButtonBox();
    _okButton = buttonBox->button(QDialogButtonBox::Ok);
    _okButton->setEnabled(false); // need a script first
    connect(chooseButton, &QPushButton::clicked, this, &SpatialScriptDialog::onChooseScript);
    mainLayout->addWidget(buttonBox);
}

void SpatialScriptDialog::onChooseScript() {
    ScriptSelectorDialog dialog(_scripts, _programIndex, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const int index = dialog.selectedIndex();
    if (index < 0) {
        return;
    }
    _programIndex = index;
    if (static_cast<size_t>(index) < _scripts.size()) {
        const ScriptSelectorDialog::Entry& entry = _scripts[static_cast<size_t>(index)];
        QString label = QString::fromStdString(entry.filename);
        if (!entry.name.empty()) {
            label += " — " + QString::fromStdString(entry.name);
        }
        _scriptLabel->setText(label);
    } else {
        _scriptLabel->setText(QString("Script #%1").arg(index));
    }
    _okButton->setEnabled(true);
}

int SpatialScriptDialog::tile() const { return _tileSpin->value(); }
int SpatialScriptDialog::elevation() const { return _elevationCombo->currentData().toInt(); }
int SpatialScriptDialog::radius() const { return _radiusSpin->value(); }

} // namespace geck
