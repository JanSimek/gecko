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
#include "ui/IconHelper.h"

namespace geck {

SpatialScriptDialog::SpatialScriptDialog(const std::vector<ScriptSelectorDialog::Entry>& scripts, QWidget* parent)
    : BaseDialog("Add Spatial Script", parent)
    , _scripts(scripts)
    , _editSourceButton(new QPushButton("Edit Source...")) {

    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();

    auto* scriptRow = new QHBoxLayout();
    _scriptLabel = new QLabel("None");
    auto* chooseButton = new QPushButton("Choose...");
    _editSourceButton->setToolTip("Open the chosen script's SSL source in the configured editor");
    _editSourceButton->setVisible(false); // shown once a host wires setSourceEditRequester
    _editSourceButton->setEnabled(false); // needs a chosen script
    scriptRow->addWidget(_scriptLabel, 1);
    scriptRow->addWidget(chooseButton);
    scriptRow->addWidget(_editSourceButton);
    formLayout->addRow("Script:", scriptRow);

    _tileSpin = new QSpinBox(this);
    _tileSpin->setRange(0, HexagonGrid::POSITION_COUNT - 1);
    auto* tileRow = new QHBoxLayout();
    tileRow->addWidget(_tileSpin, 1);
    auto* pickButton = new QPushButton("Pick on map...", this);
    pickButton->setIcon(createIcon(":/icons/actions/target-arrow.svg"));
    pickButton->setToolTip("Click a hex on the map to set the position");
    tileRow->addWidget(pickButton);
    formLayout->addRow("Hex tile:", tileRow);

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
    connect(pickButton, &QPushButton::clicked, this, &SpatialScriptDialog::pickPositionRequested);
    connect(_editSourceButton, &QPushButton::clicked, this, [this]() {
        if (_sourceEditRequester && _programIndex >= 0) {
            _sourceEditRequester(_programIndex);
        }
    });
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
    selectProgram(index);
}

void SpatialScriptDialog::selectProgram(int index) {
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
    _okButton->setEnabled(index >= 0);
    _editSourceButton->setEnabled(index >= 0);
}

void SpatialScriptDialog::setSourceEditRequester(std::function<void(int)> requester) {
    _sourceEditRequester = std::move(requester);
    _editSourceButton->setVisible(static_cast<bool>(_sourceEditRequester));
}

void SpatialScriptDialog::setProgramIndex(int index) {
    if (index >= 0) {
        selectProgram(index);
    }
}

void SpatialScriptDialog::setTile(int tile) { _tileSpin->setValue(tile); }

void SpatialScriptDialog::setElevation(int elevation) {
    const int comboIndex = _elevationCombo->findData(elevation);
    if (comboIndex >= 0) {
        _elevationCombo->setCurrentIndex(comboIndex);
    }
}

void SpatialScriptDialog::setRadius(int radius) { _radiusSpin->setValue(radius); }

int SpatialScriptDialog::tile() const { return _tileSpin->value(); }
int SpatialScriptDialog::elevation() const { return _elevationCombo->currentData().toInt(); }
int SpatialScriptDialog::radius() const { return _radiusSpin->value(); }

} // namespace geck
