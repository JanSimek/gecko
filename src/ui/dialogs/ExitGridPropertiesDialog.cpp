#include "ExitGridPropertiesDialog.h"
#include "../UIConstants.h"
#include "../theme/ThemeManager.h"
#include "../GameEnums.h"
#include "../../util/Constants.h"
#include "../../editor/HexagonGrid.h"

#include <QApplication>
#include <QStyle>
#include <QMessageBox>
#include <spdlog/spdlog.h>

namespace geck {

using namespace ui::constants;

ExitGridPropertiesDialog::ExitGridPropertiesDialog(QWidget* parent)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _formLayout(nullptr)
    , _buttonBox(nullptr)
    , _mapIdSpinBox(nullptr)
    , _positionSpinBox(nullptr)
    , _elevationComboBox(nullptr)
    , _orientationComboBox(nullptr)
    , _statusLabel(nullptr) {

    setWindowTitle("Exit Grid Properties");
    setModal(true);
    setMinimumSize(350, 250);
    resize(ui::constants::dialog_sizes::SMALL_WIDTH, ui::constants::dialog_sizes::SMALL_HEIGHT);

    setupUI();
}

ExitGridPropertiesDialog::ExitGridPropertiesDialog(const ExitGridProperties& properties, QWidget* parent)
    : ExitGridPropertiesDialog(parent) {
    setProperties(properties);
}

void ExitGridPropertiesDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE);
    _mainLayout->setSpacing(SPACING_LOOSE);

    setupFormLayout();
    setupButtonBox();

    // Status label for validation feedback
    _statusLabel = new QLabel(this);
    _statusLabel->setWordWrap(true);
    _statusLabel->setStyleSheet(ui::theme::styles::statusError());
    _statusLabel->hide();

    _mainLayout->addLayout(_formLayout);
    _mainLayout->addWidget(_statusLabel);
    _mainLayout->addStretch();
    _mainLayout->addWidget(_buttonBox);
}

void ExitGridPropertiesDialog::setupFormLayout() {
    _formLayout = new QFormLayout();
    _formLayout->setSpacing(SPACING_LOOSE);

    // World map exit checkbox (-2)
    _exitToWorldmapCheckBox = new QCheckBox("Exit to world map", this);
    _exitToWorldmapCheckBox->setToolTip("Exit leads to world map (map ID -2)");
    connect(_exitToWorldmapCheckBox, &QCheckBox::toggled, this, &ExitGridPropertiesDialog::onExitToWorldmapToggled);
    _formLayout->addRow(_exitToWorldmapCheckBox);

    // Town map exit checkbox (-1) - mutually exclusive with world map
    _townMapCheckBox = new QCheckBox("Exit to town map", this);
    _townMapCheckBox->setToolTip("Exit leads to town map (map ID -1)");
    connect(_townMapCheckBox, &QCheckBox::toggled, this, &ExitGridPropertiesDialog::onTownMapToggled);
    _formLayout->addRow(_townMapCheckBox);

    // Map ID input
    _mapIdSpinBox = new QSpinBox(this);
    _mapIdSpinBox->setRange(-2, 999999); // Allow -2 for worldmap exits
    _mapIdSpinBox->setToolTip("Destination map ID (-2 = worldmap, 0-999999 = specific map)");
    _formLayout->addRow("Destination Map ID:", _mapIdSpinBox);

    // Position input (hex coordinate)
    _positionSpinBox = new QSpinBox(this);
    _positionSpinBox->setRange(0, HexagonGrid::POSITION_COUNT - 1);
    _positionSpinBox->setToolTip("Player spawn position on destination map (0-39999)");
    connect(_positionSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
        this, &ExitGridPropertiesDialog::onPositionChanged);
    _formLayout->addRow("Player Position (Hex):", _positionSpinBox);

    // Elevation combo box
    _elevationComboBox = new QComboBox(this);
    const QStringList elevs = game::enums::elevations();
    for (int i = 0; i < elevs.size(); ++i) {
        _elevationComboBox->addItem(elevs.at(i), i);
    }
    _elevationComboBox->setToolTip("Destination map elevation level");
    _formLayout->addRow("Elevation:", _elevationComboBox);

    // Orientation combo box
    _orientationComboBox = new QComboBox(this);
    _orientationComboBox->addItem("North (0)", 0);
    _orientationComboBox->addItem("North-East (1)", 1);
    _orientationComboBox->addItem("East (2)", 2);
    _orientationComboBox->addItem("South-East (3)", 3);
    _orientationComboBox->addItem("South (4)", 4);
    _orientationComboBox->addItem("South-West (5)", 5);
    _orientationComboBox->setToolTip("Player facing direction when entering destination map");
    _formLayout->addRow("Player Orientation:", _orientationComboBox);

    // Connect validation
    connect(_mapIdSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
        this, &ExitGridPropertiesDialog::validateInput);
    connect(_elevationComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ExitGridPropertiesDialog::validateInput);
    connect(_orientationComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ExitGridPropertiesDialog::validateInput);
}

void ExitGridPropertiesDialog::setupButtonBox() {
    _buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    // Use standard Qt dialog connections
    connect(_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ExitGridPropertiesDialog::updateUI() {
    // Engine treats: -1 (TOWN_MAP_EXIT) = town map, -2 (WORLD_MAP_EXIT) = world map
    bool isTownMap = (_properties.exitMap == ExitGrid::TOWN_MAP_EXIT);
    bool isWorldMap = (_properties.exitMap == ExitGrid::WORLD_MAP_EXIT);

    spdlog::debug("ExitGridPropertiesDialog::updateUI - exitMap={} (0x{:08X}), isWorldMap={}, isTownMap={}",
        _properties.exitMap, _properties.exitMap, isWorldMap, isTownMap);

    // Block signals to prevent cascading updates while setting initial values
    _exitToWorldmapCheckBox->blockSignals(true);
    _townMapCheckBox->blockSignals(true);

    _exitToWorldmapCheckBox->setChecked(isWorldMap);
    _townMapCheckBox->setChecked(isTownMap);

    _exitToWorldmapCheckBox->blockSignals(false);
    _townMapCheckBox->blockSignals(false);

    _mapIdSpinBox->setValue(isTownMap ? -1 : (isWorldMap ? -2 : static_cast<int>(_properties.exitMap)));
    _positionSpinBox->setValue(static_cast<int>(_properties.exitPosition));

    // Set elevation combo box
    for (int i = 0; i < _elevationComboBox->count(); ++i) {
        if (_elevationComboBox->itemData(i).toUInt() == _properties.exitElevation) {
            _elevationComboBox->setCurrentIndex(i);
            break;
        }
    }

    // Set orientation combo box
    for (int i = 0; i < _orientationComboBox->count(); ++i) {
        if (_orientationComboBox->itemData(i).toUInt() == _properties.exitOrientation) {
            _orientationComboBox->setCurrentIndex(i);
            break;
        }
    }

    // Update enabled state of map-specific controls
    updateMapControlsEnabled();

    // Ensure validation is run to enable/disable OK button properly
    validateInput();
}

ExitGridPropertiesDialog::ExitGridProperties ExitGridPropertiesDialog::getProperties() const {
    ExitGridProperties props;
    props.exitMap = static_cast<uint32_t>(_mapIdSpinBox->value());
    props.exitPosition = static_cast<uint32_t>(_positionSpinBox->value());
    props.exitElevation = _elevationComboBox->currentData().toUInt();
    props.exitOrientation = _orientationComboBox->currentData().toUInt();
    return props;
}

void ExitGridPropertiesDialog::setProperties(const ExitGridProperties& properties) {
    _properties = properties;
    updateUI();
}

void ExitGridPropertiesDialog::accept() {
    if (isValidInput()) {
        _properties = getProperties();
        QDialog::accept();
    } else {
        // Show validation error to user
        validateInput();
    }
}

void ExitGridPropertiesDialog::onPositionChanged() {
    validateInput();
}

void ExitGridPropertiesDialog::validateInput() {
    bool valid = isValidInput();
    _buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);

    if (!valid) {
        _statusLabel->setText("Please check input values. Position must be 0-39999, elevation 0-2, orientation 0-5.");
        _statusLabel->show();
    } else {
        _statusLabel->hide();
    }
}

bool ExitGridPropertiesDialog::isValidInput() const {
    // Validate position range
    int position = _positionSpinBox->value();
    if (position < 0 || position >= HexagonGrid::POSITION_COUNT) {
        return false;
    }

    // Validate elevation range
    uint32_t elevation = _elevationComboBox->currentData().toUInt();
    if (elevation > 2) {
        return false;
    }

    // Validate orientation range
    uint32_t orientation = _orientationComboBox->currentData().toUInt();
    if (orientation > 5) {
        return false;
    }

    return true;
}

void ExitGridPropertiesDialog::onExitToWorldmapToggled(bool checked) {
    if (checked) {
        // Uncheck town map (mutually exclusive)
        _townMapCheckBox->blockSignals(true);
        _townMapCheckBox->setChecked(false);
        _townMapCheckBox->blockSignals(false);

        // Set world map exit values
        _mapIdSpinBox->setValue(-2);
        _positionSpinBox->setValue(0);
        _elevationComboBox->setCurrentIndex(0);   // Ground level
        _orientationComboBox->setCurrentIndex(0); // North
    } else {
        // Reset to specific map exit - set map ID to 0 if it was a world/town map value
        if (_mapIdSpinBox->value() < 0) {
            _mapIdSpinBox->setValue(0);
        }
    }

    updateMapControlsEnabled();
    validateInput();
}

void ExitGridPropertiesDialog::onTownMapToggled(bool checked) {
    if (checked) {
        // Uncheck world map (mutually exclusive)
        _exitToWorldmapCheckBox->blockSignals(true);
        _exitToWorldmapCheckBox->setChecked(false);
        _exitToWorldmapCheckBox->blockSignals(false);

        // Set town map exit values
        _mapIdSpinBox->setValue(-1);
        _positionSpinBox->setValue(0);
        _elevationComboBox->setCurrentIndex(0);   // Ground level
        _orientationComboBox->setCurrentIndex(0); // North
    } else {
        // Reset to specific map exit - set map ID to 0 if it was a world/town map value
        if (_mapIdSpinBox->value() < 0) {
            _mapIdSpinBox->setValue(0);
        }
    }

    updateMapControlsEnabled();
    validateInput();
}

void ExitGridPropertiesDialog::updateMapControlsEnabled() {
    // Disable map-specific controls when either world map or town map is checked
    bool isSpecificMapExit = !_exitToWorldmapCheckBox->isChecked() && !_townMapCheckBox->isChecked();
    _mapIdSpinBox->setEnabled(isSpecificMapExit);
    _positionSpinBox->setEnabled(isSpecificMapExit);
    _elevationComboBox->setEnabled(isSpecificMapExit);
    _orientationComboBox->setEnabled(isSpecificMapExit);
}

} // namespace geck
