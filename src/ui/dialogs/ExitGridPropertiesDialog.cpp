#include "ExitGridPropertiesDialog.h"
#include "ui/UIConstants.h"
#include "ui/theme/ThemeManager.h"
#include "ui/GameEnums.h"
#include "util/Constants.h"
#include "editor/HexagonGrid.h"
#include "resource/MapNameResolver.h"

#include <QApplication>
#include <QCompleter>
#include <QStyle>
#include <QMessageBox>
#include <spdlog/spdlog.h>

#include <vector>

namespace geck {

using namespace ui::constants;

using MarkerArt = ExitGridPropertiesDialog::ExitGridProperties::MarkerArt;

ExitGridPropertiesDialog::ExitGridPropertiesDialog(QWidget* parent, const resource::MapNameResolver* names)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _formLayout(nullptr)
    , _buttonBox(nullptr)
    , _mapComboBox(nullptr)
    , _positionSpinBox(nullptr)
    , _elevationComboBox(nullptr)
    , _orientationComboBox(nullptr)
    , _markerArtComboBox(nullptr)
    , _statusLabel(nullptr)
    , _names(names) {

    setWindowTitle("Exit Grid Properties");
    setModal(true);
    setMinimumSize(350, 250);
    resize(ui::constants::dialog_sizes::SMALL_WIDTH, ui::constants::dialog_sizes::SMALL_HEIGHT);

    setupUI();
}

ExitGridPropertiesDialog::ExitGridPropertiesDialog(const ExitGridProperties& properties, QWidget* parent,
    const resource::MapNameResolver* names)
    : ExitGridPropertiesDialog(parent, names) {
    setProperties(properties);
}

void ExitGridPropertiesDialog::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE, SPACING_LOOSE);
    _mainLayout->setSpacing(SPACING_LOOSE);

    setupFormLayout();
    setupButtonBox();

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
    _exitToWorldmapCheckBox->setObjectName("exitToWorldmap");
    _exitToWorldmapCheckBox->setToolTip("Exit leads to world map (map ID -2)");
    connect(_exitToWorldmapCheckBox, &QCheckBox::toggled, this, &ExitGridPropertiesDialog::onExitToWorldmapToggled);
    _formLayout->addRow(_exitToWorldmapCheckBox);

    // Town map exit checkbox (-1) - mutually exclusive with world map
    _townMapCheckBox = new QCheckBox("Exit to town map", this);
    _townMapCheckBox->setObjectName("exitToTownmap");
    _townMapCheckBox->setToolTip("Exit leads to town map (map ID -1)");
    connect(_townMapCheckBox, &QCheckBox::toggled, this, &ExitGridPropertiesDialog::onTownMapToggled);
    _formLayout->addRow(_townMapCheckBox);

    // Destination map picker: a searchable by-name list of the mounted maps. The combo's edit field
    // is for filtering only (NoInsert); the user picks an existing item.
    _mapComboBox = new QComboBox(this);
    _mapComboBox->setObjectName("destinationMap");
    _mapComboBox->setEditable(true);
    _mapComboBox->setInsertPolicy(QComboBox::NoInsert);
    _mapComboBox->setToolTip("Destination map (type to search by filename or name)");
    populateMapComboBox();
    _formLayout->addRow("Destination Map:", _mapComboBox);

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

    // Marker direction (directional art override). Auto picks each hex's art from its outward
    // facing; the explicit sides force one art for the whole drawn region. Not serialized — the
    // map format has no per-instance side field, so this only chooses which marker proto is drawn.
    _markerArtComboBox = new QComboBox(this);
    _markerArtComboBox->addItem("Auto", static_cast<int>(MarkerArt::Auto));
    _markerArtComboBox->addItem("Left", static_cast<int>(MarkerArt::Left));
    _markerArtComboBox->addItem("Right", static_cast<int>(MarkerArt::Right));
    _markerArtComboBox->addItem("Bottom", static_cast<int>(MarkerArt::Bottom));
    _markerArtComboBox->addItem("Top", static_cast<int>(MarkerArt::Top));
    _markerArtComboBox->addItem("Diagonal / (A)", static_cast<int>(MarkerArt::ForwardA));
    _markerArtComboBox->addItem("Diagonal / (B)", static_cast<int>(MarkerArt::ForwardB));
    _markerArtComboBox->addItem("Diagonal \\ (A)", static_cast<int>(MarkerArt::BackA));
    _markerArtComboBox->addItem("Diagonal \\ (B)", static_cast<int>(MarkerArt::BackB));
    _markerArtComboBox->setToolTip("Which directional marker art to draw. Auto uses each hex's drawn "
                                   "segment + outward facing; the explicit directions force one art "
                                   "for the whole line (also disambiguates the two diagonal sides).");
    _formLayout->addRow("Marker Direction:", _markerArtComboBox);

    // Connect validation
    connect(_mapComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ExitGridPropertiesDialog::validateInput);
    connect(_elevationComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ExitGridPropertiesDialog::validateInput);
    connect(_orientationComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ExitGridPropertiesDialog::validateInput);
}

void ExitGridPropertiesDialog::populateMapComboBox() {
    if (_names == nullptr) {
        return; // no resolver (e.g. no game data mounted) -> the combo starts empty
    }

    const std::vector<resource::MapName> maps = _names->allMaps();
    for (const auto& map : maps) {
        QString text = QString::fromStdString(map.fileName);
        if (!map.displayName.empty()) {
            text += QString::fromUtf8(" \xC2\xB7 ") + QString::fromStdString(map.displayName); // " · "
        }
        _mapComboBox->addItem(text, QVariant(map.index));
    }

    // Filter the list as the user types: match anywhere, case-insensitively.
    auto* completer = new QCompleter(_mapComboBox->model(), _mapComboBox);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    _mapComboBox->setCompleter(completer);
}

// Select the combo item whose data == mapId. When no item matches (an unknown/unlisted destination,
// or no resolver was supplied) insert a "Map <id>" entry carrying that id, so the value is preserved
// and round-trips through getProperties() unchanged.
void ExitGridPropertiesDialog::selectMap(uint32_t mapId) {
    const int id = static_cast<int>(mapId);
    for (int i = 0; i < _mapComboBox->count(); ++i) {
        if (_mapComboBox->itemData(i).toInt() == id) {
            _mapComboBox->setCurrentIndex(i);
            return;
        }
    }
    _mapComboBox->addItem(QString("Map %1").arg(id), QVariant(id));
    _mapComboBox->setCurrentIndex(_mapComboBox->count() - 1);
}

void ExitGridPropertiesDialog::setupButtonBox() {
    _buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

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

    // For specific-map exits, select the destination by id (inserting a "Map <id>" entry if it isn't
    // listed). For world/town exits the combo is disabled below; still point it at a real map so the
    // dialog has a sensible value if the user unchecks the box.
    selectMap((isTownMap || isWorldMap) ? 0u : _properties.exitMap);
    _positionSpinBox->setValue(static_cast<int>(_properties.exitPosition));

    for (int i = 0; i < _elevationComboBox->count(); ++i) {
        if (_elevationComboBox->itemData(i).toUInt() == _properties.exitElevation) {
            _elevationComboBox->setCurrentIndex(i);
            break;
        }
    }

    for (int i = 0; i < _orientationComboBox->count(); ++i) {
        if (_orientationComboBox->itemData(i).toUInt() == _properties.exitOrientation) {
            _orientationComboBox->setCurrentIndex(i);
            break;
        }
    }

    for (int i = 0; i < _markerArtComboBox->count(); ++i) {
        if (_markerArtComboBox->itemData(i).toInt() == static_cast<int>(_properties.markerArt)) {
            _markerArtComboBox->setCurrentIndex(i);
            break;
        }
    }

    updateMapControlsEnabled();

    // Run validation to enable/disable the OK button
    validateInput();
}

ExitGridPropertiesDialog::ExitGridProperties ExitGridPropertiesDialog::getProperties() const {
    ExitGridProperties props;
    if (_exitToWorldmapCheckBox->isChecked()) {
        props.exitMap = ExitGrid::WORLD_MAP_EXIT;
    } else if (_townMapCheckBox->isChecked()) {
        props.exitMap = ExitGrid::TOWN_MAP_EXIT;
    } else {
        // The selected map's index (the unknown-id fallback guarantees a current item with data).
        props.exitMap = static_cast<uint32_t>(_mapComboBox->currentData().toInt());
    }
    props.exitPosition = static_cast<uint32_t>(_positionSpinBox->value());
    props.exitElevation = _elevationComboBox->currentData().toUInt();
    props.exitOrientation = _orientationComboBox->currentData().toUInt();
    props.markerArt = static_cast<MarkerArt>(_markerArtComboBox->currentData().toInt());
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
    // A world/town map exit is always a valid destination; otherwise the combo must carry a map index
    // (the unknown-id fallback guarantees one whenever a specific-map exit is selected).
    const bool isSpecialExit = _exitToWorldmapCheckBox->isChecked() || _townMapCheckBox->isChecked();
    if (!isSpecialExit && !_mapComboBox->currentData().isValid()) {
        return false;
    }

    int position = _positionSpinBox->value();
    if (position < 0 || position >= HexagonGrid::POSITION_COUNT) { // hex position 0-39999
        return false;
    }

    uint32_t elevation = _elevationComboBox->currentData().toUInt();
    if (elevation > 2) { // elevation 0-2
        return false;
    }

    uint32_t orientation = _orientationComboBox->currentData().toUInt();
    if (orientation > 5) { // orientation 0-5
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

        // World map exit: a fixed spawn/orientation; getProperties() reports WORLD_MAP_EXIT.
        _positionSpinBox->setValue(0);
        _elevationComboBox->setCurrentIndex(0);   // Ground level
        _orientationComboBox->setCurrentIndex(0); // North
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

        // Town map exit: a fixed spawn/orientation; getProperties() reports TOWN_MAP_EXIT.
        _positionSpinBox->setValue(0);
        _elevationComboBox->setCurrentIndex(0);   // Ground level
        _orientationComboBox->setCurrentIndex(0); // North
    }

    updateMapControlsEnabled();
    validateInput();
}

void ExitGridPropertiesDialog::updateMapControlsEnabled() {
    // Disable map-specific controls when either world map or town map is checked
    bool isSpecificMapExit = !_exitToWorldmapCheckBox->isChecked() && !_townMapCheckBox->isChecked();
    _mapComboBox->setEnabled(isSpecificMapExit);
    _positionSpinBox->setEnabled(isSpecificMapExit);
    _elevationComboBox->setEnabled(isSpecificMapExit);
    _orientationComboBox->setEnabled(isSpecificMapExit);
}

} // namespace geck
