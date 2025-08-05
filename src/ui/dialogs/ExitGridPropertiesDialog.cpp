#include "ExitGridPropertiesDialog.h"
#include "../UIConstants.h"
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
    resize(400, 300);
    
    setupUI();
}

ExitGridPropertiesDialog::ExitGridPropertiesDialog(const ExitGridProperties& properties, QWidget* parent)
    : ExitGridPropertiesDialog(parent) {
    setProperties(properties);
}

void ExitGridPropertiesDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(DEFAULT_MARGIN, DEFAULT_MARGIN, DEFAULT_MARGIN, DEFAULT_MARGIN);
    _mainLayout->setSpacing(DEFAULT_SPACING);

    setupFormLayout();
    setupButtonBox();
    
    // Status label for validation feedback
    _statusLabel = new QLabel(this);
    _statusLabel->setWordWrap(true);
    _statusLabel->setStyleSheet("QLabel { color: red; font-size: 11px; }");
    _statusLabel->hide();
    
    _mainLayout->addLayout(_formLayout);
    _mainLayout->addWidget(_statusLabel);
    _mainLayout->addStretch();
    _mainLayout->addWidget(_buttonBox);
}

void ExitGridPropertiesDialog::setupFormLayout() {
    _formLayout = new QFormLayout();
    _formLayout->setSpacing(DEFAULT_SPACING);

    // Map ID input
    _mapIdSpinBox = new QSpinBox(this);
    _mapIdSpinBox->setRange(0, 999999); // Large range for map IDs
    _mapIdSpinBox->setToolTip("Destination map ID (0-999999)");
    _formLayout->addRow("Destination Map ID:", _mapIdSpinBox);

    // Position input (hex coordinate)
    _positionSpinBox = new QSpinBox(this);
    _positionSpinBox->setRange(0, HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT - 1); // 0-39999
    _positionSpinBox->setToolTip("Player spawn position on destination map (0-39999)");
    connect(_positionSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &ExitGridPropertiesDialog::onPositionChanged);
    _formLayout->addRow("Player Position (Hex):", _positionSpinBox);

    // Elevation combo box
    _elevationComboBox = new QComboBox(this);
    _elevationComboBox->addItem("Ground Level (0)", 0);
    _elevationComboBox->addItem("1st Floor (1)", 1);
    _elevationComboBox->addItem("2nd Floor (2)", 2);
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
    
    connect(_buttonBox, &QDialogButtonBox::accepted, this, &ExitGridPropertiesDialog::onAccept);
    connect(_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ExitGridPropertiesDialog::initializeDefaults() {
    _properties.exitMap = 0;
    _properties.exitPosition = 0;
    _properties.exitElevation = 0;
    _properties.exitOrientation = 0;
}

void ExitGridPropertiesDialog::updateUI() {
    _mapIdSpinBox->setValue(static_cast<int>(_properties.exitMap));
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

void ExitGridPropertiesDialog::onAccept() {
    if (isValidInput()) {
        _properties = getProperties();
        accept();
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
    if (position < 0 || position >= HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT) {
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

} // namespace geck