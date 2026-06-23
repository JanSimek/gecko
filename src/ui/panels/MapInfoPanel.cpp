#include "MapInfoPanel.h"
#include "ui/theme/ThemeManager.h"
#include "ui/GameEnums.h"
#include "ui/UIConstants.h"

#include <QHeaderView>
#include <QApplication>
#include <QMessageBox>
#include <QSize>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <optional>

#include "format/map/Map.h"
#include "format/map/MapScript.h"
#include "format/gam/Gam.h"
#include "format/lst/Lst.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"
#include "resource/MapNameEditor.h"
#include "resource/WritableDataRoot.h"
#include "ui/Settings.h"
#include "reader/ReaderFactory.h"
#include "resource/ResourcePaths.h"
#include "resource/ScriptNames.h"
#include "util/Coordinates.h"
#include "ui/IconHelper.h"
#include "ui/dialogs/ScriptSelectorDialog.h"
#include "ui/dialogs/SpatialScriptDialog.h"

namespace geck {

MapInfoPanel::MapInfoPanel(resource::GameResources& resources, std::shared_ptr<Settings> settings, QWidget* parent)
    : QWidget(parent)
    , _mainLayout(nullptr)
    , _scrollArea(nullptr)
    , _contentWidget(nullptr)
    , _contentLayout(nullptr)
    , _mapHeaderGroup(nullptr)
    , _filenameEdit(nullptr)
    , _displayNameEdit(nullptr)
    , _lookupNameEdit(nullptr)
    , _overlayHintLabel(nullptr)
    , _elevation1Check(nullptr)
    , _elevation2Check(nullptr)
    , _elevation3Check(nullptr)
    , _playerPositionSpin(nullptr)
    , _setPositionButton(nullptr)
    , _centerViewButton(nullptr)
    , _playerElevationSpin(nullptr)
    , _playerOrientationCombo(nullptr)
    , _globalVarsSpin(nullptr)
    , _localVarsSpin(nullptr)
    , _mapScriptEdit(nullptr)
    , _mapScriptIdSpin(nullptr)
    , _darknessSpin(nullptr)
    , _mapIdSpin(nullptr)
    , _timestampSpin(nullptr)
    , _savegameCheck(nullptr)
    , _globalVarsGroup(nullptr)
    , _globalVarsTree(nullptr)
    , _mapScriptsGroup(nullptr)
    , _mapScriptsLabel(nullptr)
    , _mapOperationsGroup(nullptr)
    , _clearElevationCombo(nullptr)
    , _copyFromCombo(nullptr)
    , _copyToCombo(nullptr)
    , _resources(resources)
    , _settings(std::move(settings))
    , _map(nullptr)
    , _mapScriptName("no script") {

    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    setupUI();
}

MapInfoPanel::~MapInfoPanel() = default;

QSize MapInfoPanel::sizeHint() const {
    return QSize(ui::constants::sizes::PANEL_PREFERRED_WIDTH, ui::constants::sizes::PANEL_PREFERRED_HEIGHT);
}

QSize MapInfoPanel::minimumSizeHint() const {
    return QSize(ui::constants::sizes::PANEL_MIN_SIZE_WIDTH, ui::constants::sizes::PANEL_MIN_SIZE_HEIGHT);
}

void MapInfoPanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN);

    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setMinimumSize(0, 0);
    _scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    _contentWidget = new QWidget();
    _contentWidget->setMinimumSize(0, 0);
    _contentWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    _contentLayout = new QVBoxLayout(_contentWidget);
    _contentLayout->setContentsMargins(ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN);

    _mapHeaderGroup = new QGroupBox("Map Header");
    QFormLayout* headerLayout = new QFormLayout(_mapHeaderGroup);

    _filenameEdit = new QLineEdit();
    headerLayout->addRow("Filename:", _filenameEdit);

    // Editable friendly names from maps.txt / map.msg (blank + read-only when game data is unmounted or
    // the map isn't registered). Editing a name marks the map modified; the value is written to a
    // writable copy when the map is saved (see persistMapNames).
    _displayNameEdit = new QLineEdit();
    _displayNameEdit->setObjectName("mapDisplayName");
    headerLayout->addRow("Map name:", _displayNameEdit);

    _lookupNameEdit = new QLineEdit();
    _lookupNameEdit->setObjectName("mapLookupName");
    headerLayout->addRow("Lookup name:", _lookupNameEdit);

    connect(_displayNameEdit, &QLineEdit::textEdited, this, [this]() { Q_EMIT mapNamesChanged(); });
    connect(_lookupNameEdit, &QLineEdit::textEdited, this, [this]() { Q_EMIT mapNamesChanged(); });

    // Appears once maps.txt/map.msg have been extracted to the writable copy; tells the user where the
    // edited files live (the originals in the game archive are read-only).
    _overlayHintLabel = new QLabel();
    _overlayHintLabel->setObjectName("mapNamesOverlayHint");
    _overlayHintLabel->setWordWrap(true);
    _overlayHintLabel->setVisible(false);
    _overlayHintLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(ui::theme::colors::TEXT_SECONDARY));
    headerLayout->addRow("", _overlayHintLabel);

    QWidget* elevationsWidget = new QWidget();
    QVBoxLayout* elevationsLayout = new QVBoxLayout(elevationsWidget);
    elevationsLayout->setContentsMargins(0, 0, 0, 0);
    elevationsLayout->setSpacing(5);

    _elevation1Check = new QCheckBox("Elevation 1");
    _elevation2Check = new QCheckBox("Elevation 2");
    _elevation3Check = new QCheckBox("Elevation 3");

    elevationsLayout->addWidget(_elevation1Check);
    elevationsLayout->addWidget(_elevation2Check);
    elevationsLayout->addWidget(_elevation3Check);

    headerLayout->addRow("Map elevations:", elevationsWidget);

    QHBoxLayout* positionLayout = new QHBoxLayout();
    _playerPositionSpin = new QSpinBox();
    _playerPositionSpin->setRange(0, HexPosition::MAX_VALUE); // Max hex position
    _setPositionButton = new QPushButton();
    _setPositionButton->setIcon(createIcon(":/icons/actions/map-pin.svg"));
    _setPositionButton->setMaximumWidth(ui::constants::sizes::NAV_BUTTON);
    _setPositionButton->setToolTip("Click to select position on map");
    _centerViewButton = new QPushButton();
    _centerViewButton->setIcon(createIcon(":/icons/actions/target-arrow.svg"));
    _centerViewButton->setMaximumWidth(ui::constants::sizes::NAV_BUTTON);
    _centerViewButton->setToolTip("Center view on player position");
    positionLayout->addWidget(_playerPositionSpin);
    positionLayout->addWidget(_setPositionButton);
    positionLayout->addWidget(_centerViewButton);
    headerLayout->addRow("Player default position:", positionLayout);

    _playerElevationSpin = new QSpinBox();
    _playerElevationSpin->setRange(ELEVATION_1, ELEVATION_3);
    headerLayout->addRow("Player default elevation:", _playerElevationSpin);

    _playerOrientationCombo = new QComboBox();
    _playerOrientationCombo->addItems(game::enums::orientations());
    headerLayout->addRow("Player default orientation:", _playerOrientationCombo);

    _globalVarsSpin = new QSpinBox();
    _globalVarsSpin->setRange(0, INT_MAX);
    _globalVarsSpin->setReadOnly(true); // Keep read-only - depends on actual variables
    _globalVarsSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Global variables #:", _globalVarsSpin);

    _localVarsSpin = new QSpinBox();
    _localVarsSpin->setRange(0, INT_MAX);
    _localVarsSpin->setReadOnly(true); // Keep read-only - depends on actual variables
    _localVarsSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Local variables #:", _localVarsSpin);

    _mapScriptEdit = new QLineEdit();
    _mapScriptEdit->setObjectName("mapScript");
    _mapScriptEdit->setReadOnly(true);
    headerLayout->addRow("Map script:", _mapScriptEdit);

    _mapScriptIdSpin = new QSpinBox();
    _mapScriptIdSpin->setRange(-1, INT_MAX); // -1 means no script
    headerLayout->addRow("Map script ID:", _mapScriptIdSpin);

    _darknessSpin = new QSpinBox();
    _darknessSpin->setReadOnly(true); // unused
    _darknessSpin->setRange(0, 10);
    headerLayout->addRow("Darkness:", _darknessSpin);

    _mapIdSpin = new QSpinBox();
    _mapIdSpin->setRange(0, INT_MAX);
    _mapIdSpin->setReadOnly(true); // Map ID should not be changed
    _mapIdSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Map ID:", _mapIdSpin);

    _timestampSpin = new QSpinBox();
    _timestampSpin->setRange(0, INT_MAX);
    headerLayout->addRow("Timestamp:", _timestampSpin);

    _savegameCheck = new QCheckBox("Save game map");
    _savegameCheck->setEnabled(true);
    headerLayout->addRow(_savegameCheck);

    _contentLayout->addWidget(_mapHeaderGroup);

    connect(_playerPositionSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_playerElevationSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_playerOrientationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MapInfoPanel::onOrientationChanged);
    connect(_setPositionButton, &QPushButton::clicked, this, &MapInfoPanel::onSelectPositionClicked);
    connect(_centerViewButton, &QPushButton::clicked, this, &MapInfoPanel::onCenterViewClicked);
    connect(_mapScriptIdSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_darknessSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_timestampSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_savegameCheck, &QCheckBox::toggled, this, &MapInfoPanel::onFieldChanged);

    connect(_elevation1Check, &QCheckBox::toggled, this, &MapInfoPanel::onElevationCheckboxChanged);
    connect(_elevation2Check, &QCheckBox::toggled, this, &MapInfoPanel::onElevationCheckboxChanged);
    connect(_elevation3Check, &QCheckBox::toggled, this, &MapInfoPanel::onElevationCheckboxChanged);

    _globalVarsGroup = new QGroupBox("Map Global Variables");
    QVBoxLayout* varsLayout = new QVBoxLayout(_globalVarsGroup);

    _globalVarsTree = new QTreeWidget();
    _globalVarsTree->setHeaderLabels({ "Variable", "Value" });
    _globalVarsTree->header()->setStretchLastSection(true);
    _globalVarsTree->setAlternatingRowColors(true);
    _globalVarsTree->setRootIsDecorated(false);
    varsLayout->addWidget(_globalVarsTree);

    _contentLayout->addWidget(_globalVarsGroup);

    _mapScriptsGroup = new QGroupBox("Map Scripts");
    QVBoxLayout* scriptsLayout = new QVBoxLayout(_mapScriptsGroup);

    _mapScriptsLabel = new QLabel("No script information available");
    _mapScriptsLabel->setStyleSheet(ui::theme::styles::italicSecondaryText());
    _mapScriptsLabel->setWordWrap(true);
    scriptsLayout->addWidget(_mapScriptsLabel);

    _contentLayout->addWidget(_mapScriptsGroup);

    // === Map Operations group (clear / copy elevation) ===
    _mapOperationsGroup = new QGroupBox("Map Operations");
    QFormLayout* opsLayout = new QFormLayout(_mapOperationsGroup);

    auto makeElevationCombo = []() {
        auto* combo = new QComboBox();
        combo->addItem("Elevation 1", ELEVATION_1);
        combo->addItem("Elevation 2", ELEVATION_2);
        combo->addItem("Elevation 3", ELEVATION_3);
        return combo;
    };

    QHBoxLayout* clearRow = new QHBoxLayout();
    _clearElevationCombo = makeElevationCombo();
    auto* clearButton = new QPushButton("Clear Objects");
    clearButton->setToolTip("Delete every object on the chosen elevation (tiles are kept).");
    clearRow->addWidget(_clearElevationCombo, 1);
    clearRow->addWidget(clearButton);
    opsLayout->addRow("Clear:", clearRow);

    QHBoxLayout* copyRow = new QHBoxLayout();
    _copyFromCombo = makeElevationCombo();
    _copyToCombo = makeElevationCombo();
    _copyToCombo->setCurrentIndex(1);
    auto* copyButton = new QPushButton("Copy");
    copyButton->setToolTip("Copy tiles and objects from one elevation to another (overwrites the destination).");
    copyRow->addWidget(_copyFromCombo, 1);
    copyRow->addWidget(new QLabel("to"));
    copyRow->addWidget(_copyToCombo, 1);
    copyRow->addWidget(copyButton);
    opsLayout->addRow("Copy:", copyRow);

    auto* spatialScriptButton = new QPushButton("Add Spatial Script...");
    spatialScriptButton->setToolTip("Place a spatial (hex trigger-zone) script at a tile with a radius.");
    opsLayout->addRow("Scripts:", spatialScriptButton);

    connect(clearButton, &QPushButton::clicked, this, &MapInfoPanel::onClearElevationClicked);
    connect(copyButton, &QPushButton::clicked, this, &MapInfoPanel::onCopyElevationClicked);
    connect(spatialScriptButton, &QPushButton::clicked, this, &MapInfoPanel::onAddSpatialScriptClicked);

    _contentLayout->addWidget(_mapOperationsGroup);

    _contentLayout->addStretch();

    _scrollArea->setWidget(_contentWidget);
    _mainLayout->addWidget(_scrollArea);

    clearMapInfo();
}

void MapInfoPanel::setMap(Map* map) {
    _map = map;
    if (_map) {
        updateMapInfo();
        spdlog::debug("MapInfoPanel: Map set and info updated");
    } else {
        clearMapInfo();
        spdlog::debug("MapInfoPanel: Map cleared");
    }
}

void MapInfoPanel::updateMapInfo() {
    if (!_map) {
        clearMapInfo();
        return;
    }

    try {
        auto& mapInfo = _map->getMapFile();

        // Populating the widgets below emits their change signals; suppress the write-back to the map so
        // a half-refreshed widget set can't clobber fields that haven't been updated yet.
        _suppressFieldChanged = true;

        // Update elevation checkboxes based on flags (inverted logic: 0 = enabled, 1 = disabled)
        bool hasElevation1 = (mapInfo.header.flags & 0x2) == 0;
        bool hasElevation2 = (mapInfo.header.flags & 0x4) == 0;
        bool hasElevation3 = (mapInfo.header.flags & 0x8) == 0;

        // Temporarily disconnect signals to avoid triggering changes during update
        setElevationCheckboxesBlocked(true);
        _elevation1Check->setChecked(hasElevation1);
        _elevation2Check->setChecked(hasElevation2);
        _elevation3Check->setChecked(hasElevation3);
        setElevationCheckboxesBlocked(false);

        spdlog::debug("Map elevations: 1={}, 2={}, 3={}", hasElevation1, hasElevation2, hasElevation3);

        _filenameEdit->setText(QString::fromStdString(mapInfo.header.filename));
        _playerPositionSpin->setValue(static_cast<int>(mapInfo.header.player_default_position));
        _playerElevationSpin->setValue(static_cast<int>(mapInfo.header.player_default_elevation));
        _playerOrientationCombo->setCurrentIndex(static_cast<int>(mapInfo.header.player_default_orientation));
        _globalVarsSpin->setValue(static_cast<int>(mapInfo.header.num_global_vars));
        _localVarsSpin->setValue(static_cast<int>(mapInfo.header.num_local_vars));
        _mapScriptIdSpin->setValue(static_cast<int>(mapInfo.header.script_id));
        _darknessSpin->setValue(static_cast<int>(mapInfo.header.darkness));
        _mapIdSpin->setValue(static_cast<int>(mapInfo.header.map_id));
        _timestampSpin->setValue(static_cast<int>(mapInfo.header.timestamp));

        bool isSavegame = ((mapInfo.header.flags & 0x1) != 0);
        _savegameCheck->setChecked(isSavegame);

        _suppressFieldChanged = false; // done populating; user edits write back from here on

        loadScriptVars();
        _mapScriptEdit->setText(QString::fromStdString(_mapScriptName));

        updateMapNameDisplay();

        _globalVarsTree->clear();

        if (_mvars.empty() && mapInfo.header.num_global_vars > 0) {
            // Show placeholder when GAM file couldn't be loaded but header indicates variables exist
            QTreeWidgetItem* placeholderItem = new QTreeWidgetItem(_globalVarsTree);
            placeholderItem->setText(0, QString("⚠️ %1 global variables expected").arg(mapInfo.header.num_global_vars));
            placeholderItem->setText(1, "GAM file not loaded");
            placeholderItem->setForeground(0, QBrush(ui::theme::colors::statusWarningRgb()));
            placeholderItem->setForeground(1, QBrush(ui::theme::colors::statusWarningRgb()));
        } else if (_mvars.empty() && mapInfo.header.num_global_vars == 0) {
            QTreeWidgetItem* infoItem = new QTreeWidgetItem(_globalVarsTree);
            infoItem->setText(0, "No global variables defined");
            infoItem->setText(1, "(Map header indicates 0 variables)");
            infoItem->setForeground(0, QBrush(ui::theme::colors::statusInfoRgb()));
            infoItem->setForeground(1, QBrush(ui::theme::colors::statusInfoRgb()));
        } else {
            for (const auto& [key, value] : _mvars) {
                QTreeWidgetItem* item = new QTreeWidgetItem(_globalVarsTree);
                item->setText(0, QString::fromStdString(key));
                item->setText(1, QString::number(value));

                item->setForeground(0, QBrush(ui::theme::colors::statusSuccessRgb()));
                item->setForeground(1, QBrush(ui::theme::colors::statusSuccessRgb()));
            }

            if (!_mvars.empty()) {
                QTreeWidgetItem* summaryItem = new QTreeWidgetItem(_globalVarsTree);
                summaryItem->setText(0, QString("✅ Total: %1 variables loaded").arg(_mvars.size()));
                summaryItem->setText(1, "From GAM file");
                summaryItem->setForeground(0, QBrush(ui::theme::colors::statusSuccessRgb()));
                summaryItem->setForeground(1, QBrush(ui::theme::colors::statusSuccessRgb()));
            }
        }

        _globalVarsTree->expandAll();
        _globalVarsTree->resizeColumnToContents(0);

        updateMapScriptsDisplay();

        // Disable the last remaining elevation's checkbox.
        updateElevationCheckboxStates();

    } catch (const std::exception& e) {
        _suppressFieldChanged = false; // never leave write-back disabled if population threw mid-way
        spdlog::error("Error updating map info: {}", e.what());
        clearMapInfo();
    }
}

void MapInfoPanel::updateMapNameDisplay() {
    if (!_displayNameEdit || !_lookupNameEdit) {
        return;
    }
    if (!_map) {
        _displayNameEdit->clear();
        _lookupNameEdit->clear();
        updateOverlayHint();
        return;
    }

    // Built lazily: maps.txt / map.msg are read once, and by the time a map is open the game data is
    // mounted. Resolution degrades to blank when the data is missing (resolver returns "").
    if (!_mapNames) {
        _mapNames = std::make_unique<resource::MapNameResolver>(_resources);
    }

    const auto& header = _map->getMapFile().header;
    // Resolve by the actual file basename, NOT header.filename: the .map's 16-byte filename field is
    // upper-cased and NUL-padded (e.g. "ARTEMPLE.MAP\0\0\0\0"), which never matches maps.txt. _map's
    // path filename ("artemple.map") is what maps.txt uses (and what the CLI/MCP resolve by).
    const std::string file = _map->filename();
    const int index = _mapNames->indexOf(file);
    const int elevation = static_cast<int>(header.player_default_elevation);
    const bool registered = index >= 0; // present in maps.txt -> names are editable + persistable

    const std::string display = registered ? _mapNames->displayName(index, elevation) : std::string();
    const std::string lookup = _mapNames->lookupNameOf(file);

    // setText resets QLineEdit::isModified() to false, so populating is not treated as a user edit.
    _displayNameEdit->setText(QString::fromStdString(display));
    _lookupNameEdit->setText(registered ? QString::fromStdString(lookup) : QStringLiteral("(not in maps.txt)"));
    _displayNameEdit->setReadOnly(!registered);
    _lookupNameEdit->setReadOnly(!registered);
    updateOverlayHint();
}

void MapInfoPanel::updateOverlayHint() {
    if (!_overlayHintLabel || !_displayNameEdit) {
        return;
    }
    // The names are editable only for a registered map. When they are, but there is no writable folder
    // in the Data Paths to save them to (every path is a read-only archive), hint the user to add one —
    // the editor never creates or mounts a hidden writable location.
    const bool editable = !_displayNameEdit->isReadOnly();
    const bool hasWritablePath = _settings && resource::findWritableDataPath(_settings->getDataPaths()).has_value();
    const bool needsWritablePath = editable && !hasWritablePath;
    _overlayHintLabel->setVisible(needsWritablePath);
    if (needsWritablePath) {
        _overlayHintLabel->setText(QStringLiteral(
            "To save Map name / Lookup name edits, add a writable folder to your Data Paths "
            "(all current data paths are read-only archives)."));
    }
}

void MapInfoPanel::loadScriptVars() {
    _mvars.clear();
    _mapScriptName = "no script";

    if (!_map) {
        return;
    }

    try {
        // The map's own script (header.script_id, 1-based) names a scripts.lst entry. Resolve its
        // filename and scrname.msg description independently of the .gam file (which only carries the
        // map's global variables), so the name shows even for a map that has no .gam.
        const int map_script_id = _map->getMapFile().header.script_id;
        if (map_script_id > 0) {
            auto scripts = _resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS);
            const auto& scriptList = scripts->list();
            if (map_script_id <= static_cast<int>(scriptList.size()) && map_script_id >= 1) {
                _mapScriptName = scripts->at(map_script_id - 1); // script id starts at 1
                // Append the scrname.msg description for the same script (its 0-based index is id - 1),
                // mirroring how object scripts are named in the script picker.
                const std::string desc = resource::scriptDisplayName(_resources, map_script_id - 1);
                if (!desc.empty()) {
                    _mapScriptName += " — " + desc;
                }
            } else {
                _mapScriptName = "invalid script index";
                spdlog::warn("Map script ID {} out of bounds for scripts.lst size {}", map_script_id, scriptList.size());
            }
        }

        // Global variables come from the map's optional .gam file.
        const std::string baseName = _map->filename().substr(0, _map->filename().find("."));
        const std::string gam_path = "maps/" + baseName + ".gam";
        if (_resources.files().exists(gam_path)) {
            if (Gam* gam_file = _resources.repository().load<Gam>(gam_path); gam_file != nullptr) {
                for (uint32_t index = 0; index < _map->getMapFile().header.num_global_vars; index++) {
                    _mvars.emplace(gam_file->mvarKey(index), gam_file->mvarValue(index));
                }
            }
        } else {
            spdlog::debug("GAM file '{}' not found in VFS; map global variables unavailable", gam_path);
        }
    } catch (const std::exception& e) {
        spdlog::error("Error loading script vars: {}", e.what());
        _mvars.clear();
    }
}

void MapInfoPanel::clearMapInfo() {
    _filenameEdit->clear();
    _filenameEdit->setPlaceholderText("No map loaded");

    setElevationCheckboxesBlocked(true);
    _elevation1Check->setChecked(false);
    _elevation2Check->setChecked(false);
    _elevation3Check->setChecked(false);
    setElevationCheckboxesBlocked(false);
    _playerPositionSpin->setValue(0);
    _playerElevationSpin->setValue(0);
    _playerOrientationCombo->setCurrentIndex(0);
    _globalVarsSpin->setValue(0);
    _localVarsSpin->setValue(0);
    _mapScriptIdSpin->setValue(0);
    _darknessSpin->setValue(0);
    _mapIdSpin->setValue(0);
    _timestampSpin->setValue(0);
    _savegameCheck->setChecked(false);

    _mapScriptEdit->clear();
    _mapScriptEdit->setPlaceholderText("No script");

    if (_displayNameEdit) {
        _displayNameEdit->clear();
    }
    if (_lookupNameEdit) {
        _lookupNameEdit->clear();
    }
    updateOverlayHint();

    _globalVarsTree->clear();

    _mvars.clear();
    _mapScriptName = "no script";

    updateMapScriptsDisplay();
}

void MapInfoPanel::onFieldChanged() {
    // Ignore the valueChanged/toggled signals that fire while updateMapInfo() is populating the widgets
    // from the map; otherwise a half-updated widget set would be written back, corrupting fields that
    // haven't been refreshed yet (e.g. script_id reset to the stale spin value during load).
    if (_suppressFieldChanged || !_map) {
        return;
    }

    auto& mapInfo = _map->getMapFile();

    mapInfo.header.player_default_position = static_cast<uint32_t>(_playerPositionSpin->value());
    mapInfo.header.player_default_elevation = static_cast<uint32_t>(_playerElevationSpin->value());
    mapInfo.header.script_id = _mapScriptIdSpin->value();
    mapInfo.header.darkness = static_cast<uint32_t>(_darknessSpin->value());
    mapInfo.header.timestamp = static_cast<uint32_t>(_timestampSpin->value());

    // flags bit 0 = savegame map
    if (_savegameCheck->isChecked()) {
        mapInfo.header.flags |= 0x1;
    } else {
        mapInfo.header.flags &= ~0x1;
    }

    QObject* sender = QObject::sender();
    if (sender == _playerPositionSpin) {
        Q_EMIT playerPositionChanged(_playerPositionSpin->value());
    } else if (sender == _playerElevationSpin) {
        updateMapNameDisplay(); // the map.msg display name is per-elevation
        Q_EMIT playerElevationChanged(_playerElevationSpin->value());
    } else if (sender == _mapScriptIdSpin) {
        Q_EMIT mapScriptIdChanged(_mapScriptIdSpin->value());
    } else if (sender == _darknessSpin) {
        Q_EMIT darknessChanged(_darknessSpin->value());
    } else if (sender == _timestampSpin) {
        Q_EMIT timestampChanged(_timestampSpin->value());
    }

    spdlog::debug("MapInfoPanel: Field changed, map header updated");
}

void MapInfoPanel::onOrientationChanged(int index) {
    if (!_map) {
        return;
    }

    auto& mapInfo = _map->getMapFile();
    mapInfo.header.player_default_orientation = static_cast<uint32_t>(index);

    Q_EMIT playerOrientationChanged(index);
    spdlog::debug("MapInfoPanel: Player orientation changed to {}", index);
}

void MapInfoPanel::onSelectPositionClicked() {
    Q_EMIT selectPlayerPositionRequested();
    spdlog::debug("MapInfoPanel: Player position selection requested");
}

void MapInfoPanel::onCenterViewClicked() {
    Q_EMIT centerViewOnPlayerPositionRequested();
    spdlog::debug("MapInfoPanel: Center view on player position requested");
}

void MapInfoPanel::setPlayerPosition(int hexPosition, int elevation) {
    if (!_map) {
        return;
    }

    _playerPositionSpin->setValue(hexPosition);
    _playerElevationSpin->setValue(elevation);
}

void MapInfoPanel::updateMapScriptsDisplay() {
    if (!_map) {
        _mapScriptsLabel->setText("No map loaded");
        _mapScriptsLabel->setStyleSheet(ui::theme::styles::italicSecondaryText());
        return;
    }

    try {
        auto& mapInfo = _map->getMapFile();
        QString scriptsInfo;

        if (mapInfo.header.script_id > 0) {
            scriptsInfo += QString("Map Script ID: %1\n").arg(mapInfo.header.script_id);
            scriptsInfo += QString("Map Script Name: %1\n").arg(QString::fromStdString(_mapScriptName));

            if (_mapScriptName.find("not found") != std::string::npos) {
                scriptsInfo += "⚠️ Script file could not be resolved\n";
            } else if (_mapScriptName.find("Error") != std::string::npos) {
                scriptsInfo += "❌ Error loading script information\n";
            } else if (_mapScriptName.find("GAM file") != std::string::npos) {
                scriptsInfo += "⚠️ GAM file missing, using script ID only\n";
            } else if (_mapScriptName != "no script") {
                scriptsInfo += "✅ Script information loaded successfully\n";
            }
        } else if (mapInfo.header.script_id == -1 || mapInfo.header.script_id == 0) {
            scriptsInfo += "No map script assigned\n";
        } else {
            scriptsInfo += QString("⚠️ Invalid script ID: %1\n").arg(mapInfo.header.script_id);
        }

        scriptsInfo += QString("\nGlobal Variables: %1\n").arg(mapInfo.header.num_global_vars);
        if (_mvars.empty() && mapInfo.header.num_global_vars > 0) {
            scriptsInfo += "⚠️ Global variables not loaded (GAM file issue)\n";
        } else if (!_mvars.empty()) {
            scriptsInfo += QString("✅ %1 global variables loaded\n").arg(_mvars.size());
        }

        scriptsInfo += QString("Local Variables: %1\n").arg(mapInfo.header.num_local_vars);

        int totalScripts = 0;
        bool hasObjectScripts = false;

        scriptsInfo += "\n--- Object Scripts ---\n";

        for (int i = 0; i < Map::SCRIPT_SECTIONS; i++) {
            int sectionCount = mapInfo.scripts_in_section[i];
            if (sectionCount > 0) {
                hasObjectScripts = true;

                // Map section numbers to readable names
                QString sectionName;
                switch (i) {
                    case 0:
                        sectionName = "System";
                        break;
                    case 1:
                        sectionName = "Spatial";
                        break;
                    case 2:
                        sectionName = "Timer";
                        break;
                    case 3:
                        sectionName = "Item";
                        break;
                    case 4:
                        sectionName = "Critter";
                        break;
                    default:
                        sectionName = QString("Section %1").arg(i);
                        break;
                }

                scriptsInfo += QString("%1 Scripts: %2\n").arg(sectionName).arg(sectionCount);

                const auto& scripts = mapInfo.map_scripts[i];
                int actualScriptCount = static_cast<int>(scripts.size());
                totalScripts += actualScriptCount;

                // Warn if the header count doesn't match the actual scripts present.
                if (sectionCount != actualScriptCount) {
                    scriptsInfo += QString("  ⚠️ Header says %1, but found %2 actual scripts\n")
                                       .arg(sectionCount)
                                       .arg(actualScriptCount);
                }

                int displayCount = std::min(3, actualScriptCount);
                for (int j = 0; j < displayCount; j++) {
                    const auto& script = scripts[j];
                    auto scriptType = MapScript::fromPid(script.pid);
                    scriptsInfo += QString("  • PID: %1, Type: %2, Script ID: %3\n")
                                       .arg(script.pid)
                                       .arg(QString::fromStdString(std::string(MapScript::toString(scriptType))))
                                       .arg(script.script_id);
                }
                if (actualScriptCount > 3) {
                    scriptsInfo += QString("  • ... and %1 more scripts\n").arg(actualScriptCount - 3);
                }
            }
        }

        if (!hasObjectScripts) {
            scriptsInfo += "No object scripts found\n";
        } else {
            scriptsInfo += QString("\n✅ Total object scripts: %1").arg(totalScripts);
        }

        _mapScriptsLabel->setText(scriptsInfo.trimmed());

        if (scriptsInfo.contains("❌") || scriptsInfo.contains("Error")) {
            _mapScriptsLabel->setStyleSheet(ui::theme::styles::errorMonospace());
        } else if (scriptsInfo.contains("⚠️")) {
            _mapScriptsLabel->setStyleSheet(ui::theme::styles::warningMonospace());
        } else {
            _mapScriptsLabel->setStyleSheet(ui::theme::styles::monospaceText());
        }

    } catch (const std::exception& e) {
        QString errorMsg = QString("❌ Error loading script information:\n%1").arg(e.what());
        _mapScriptsLabel->setText(errorMsg);
        _mapScriptsLabel->setStyleSheet(ui::theme::styles::errorMonospace());
        spdlog::error("Error updating map scripts display: {}", e.what());
    }
}

void MapInfoPanel::setElevationCheckboxesBlocked(bool blocked) {
    _elevation1Check->blockSignals(blocked);
    _elevation2Check->blockSignals(blocked);
    _elevation3Check->blockSignals(blocked);
}

void MapInfoPanel::updateElevationCheckboxStates() {
    if (!_map) {
        return;
    }

    auto& mapFile = _map->getMapFile();

    // Elevation flag bits are inverted: bit clear (0) = enabled.
    int enabledCount = 0;
    bool hasElevation1 = (mapFile.header.flags & 0x2) == 0;
    bool hasElevation2 = (mapFile.header.flags & 0x4) == 0;
    bool hasElevation3 = (mapFile.header.flags & 0x8) == 0;

    if (hasElevation1)
        enabledCount++;
    if (hasElevation2)
        enabledCount++;
    if (hasElevation3)
        enabledCount++;

    // If only one elevation remains, disable that checkbox to prevent removal
    // (but only if it's currently checked - we don't want to prevent adding elevations)
    if (enabledCount <= 1) {
        if (hasElevation1 && _elevation1Check->isChecked()) {
            _elevation1Check->setEnabled(false);
            _elevation1Check->setToolTip("Cannot remove the last remaining elevation");
        }
        if (hasElevation2 && _elevation2Check->isChecked()) {
            _elevation2Check->setEnabled(false);
            _elevation2Check->setToolTip("Cannot remove the last remaining elevation");
        }
        if (hasElevation3 && _elevation3Check->isChecked()) {
            _elevation3Check->setEnabled(false);
            _elevation3Check->setToolTip("Cannot remove the last remaining elevation");
        }
    } else {
        _elevation1Check->setEnabled(true);
        _elevation1Check->setToolTip("");
        _elevation2Check->setEnabled(true);
        _elevation2Check->setToolTip("");
        _elevation3Check->setEnabled(true);
        _elevation3Check->setToolTip("");
    }

    spdlog::debug("MapInfoPanel: Updated elevation checkbox states - {} elevations enabled", enabledCount);
}

void MapInfoPanel::onElevationCheckboxChanged() {
    if (!_map) {
        return;
    }

    QCheckBox* sender = qobject_cast<QCheckBox*>(QObject::sender());
    if (!sender) {
        return;
    }

    int elevation = -1;
    uint32_t flagBit = 0;
    QString elevationName;

    if (sender == _elevation1Check) {
        elevation = ELEVATION_1;
        flagBit = 0x2;
        elevationName = "Elevation 1";
    } else if (sender == _elevation2Check) {
        elevation = ELEVATION_2;
        flagBit = 0x4;
        elevationName = "Elevation 2";
    } else if (sender == _elevation3Check) {
        elevation = ELEVATION_3;
        flagBit = 0x8;
        elevationName = "Elevation 3";
    } else {
        return;
    }

    auto& mapFile = _map->getMapFile();
    bool isChecked = sender->isChecked();
    bool wasEnabled = (mapFile.header.flags & flagBit) == 0;

    if (isChecked && !wasEnabled) {
        mapFile.header.flags &= ~flagBit; // Clear bit to enable elevation

        if (mapFile.tiles.find(elevation) == mapFile.tiles.end()) {
            mapFile.tiles[elevation] = Map::createEmptyElevation();
        }

        if (mapFile.map_objects.find(elevation) == mapFile.map_objects.end()) {
            mapFile.map_objects[elevation].clear();
        }

        Q_EMIT elevationAdded(elevation);
        spdlog::info("MapInfoPanel: Added {} to map", elevationName.toStdString());

        updateElevationCheckboxStates();

    } else if (!isChecked && wasEnabled) {
        QString message = QString("Are you sure you want to remove %1?\n\n"
                                  "This will permanently delete:\n"
                                  "• All tiles on this elevation\n"
                                  "• All objects on this elevation\n"
                                  "• Any scripts associated with objects on this elevation\n\n"
                                  "This action cannot be undone.")
                              .arg(elevationName);

        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            "Remove Elevation",
            message,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            mapFile.header.flags |= flagBit; // Set bit to disable elevation

            auto tileIt = mapFile.tiles.find(elevation);
            if (tileIt != mapFile.tiles.end()) {
                tileIt->second.clear();
                mapFile.tiles.erase(tileIt);
            }

            auto objectIt = mapFile.map_objects.find(elevation);
            if (objectIt != mapFile.map_objects.end()) {
                objectIt->second.clear();
                mapFile.map_objects.erase(objectIt);
            }

            Q_EMIT elevationRemoved(elevation);
            spdlog::info("MapInfoPanel: Removed {} from map", elevationName.toStdString());

            updateElevationCheckboxStates();

        } else {
            // User cancelled: revert checkbox state.
            sender->blockSignals(true);
            sender->setChecked(true);
            sender->blockSignals(false);
            spdlog::debug("MapInfoPanel: Elevation removal cancelled by user");
        }
    }
}

// Bulk map operations confirm with the user, then emit a request the editor
// routes through ObjectCommandController so they are recorded as one undoable command.
void MapInfoPanel::onClearElevationClicked() {
    if (!_map) {
        return;
    }

    const int elevation = _clearElevationCombo->currentData().toInt();
    auto& mapFile = _map->getMapFile();

    auto it = mapFile.map_objects.find(elevation);
    const size_t count = (it != mapFile.map_objects.end()) ? it->second.size() : 0;
    if (count == 0) {
        QMessageBox::information(this, "Clear Elevation",
            QString("Elevation %1 has no objects to clear.").arg(elevation + 1));
        return;
    }

    const auto reply = QMessageBox::question(this, "Clear Elevation",
        QString("Delete all %1 object(s) on Elevation %2?")
            .arg(count)
            .arg(elevation + 1),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    Q_EMIT clearElevationRequested(elevation);
}

void MapInfoPanel::onCopyElevationClicked() {
    if (!_map) {
        return;
    }

    const int from = _copyFromCombo->currentData().toInt();
    const int to = _copyToCombo->currentData().toInt();
    if (from == to) {
        QMessageBox::information(this, "Copy Elevation",
            "Source and destination elevations are the same.");
        return;
    }

    auto& mapFile = _map->getMapFile();

    // Both elevations must be enabled (present) for a meaningful, saveable copy.
    if (!Map::elevationIsPresent(mapFile.header.flags, from)) {
        QMessageBox::warning(this, "Copy Elevation",
            QString("Source Elevation %1 is not enabled.").arg(from + 1));
        return;
    }
    if (!Map::elevationIsPresent(mapFile.header.flags, to)) {
        QMessageBox::warning(this, "Copy Elevation",
            QString("Destination Elevation %1 is not enabled. Enable it first.").arg(to + 1));
        return;
    }

    const size_t dstObjs = mapFile.map_objects.count(to) ? mapFile.map_objects.at(to).size() : 0;
    const auto reply = QMessageBox::question(this, "Copy Elevation",
        QString("Copy tiles and objects from Elevation %1 to Elevation %2?\n\n"
                "This overwrites Elevation %2 (%3 existing object(s)).")
            .arg(from + 1)
            .arg(to + 1)
            .arg(dstObjs),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    Q_EMIT copyElevationRequested(from, to);
}

void MapInfoPanel::onAddSpatialScriptClicked() {
    if (!_map) {
        return;
    }

    auto* scriptsLst = _resources.repository().load<Lst>(std::string(ResourcePaths::Lst::SCRIPTS));
    if (!scriptsLst) {
        QMessageBox::warning(this, "Add Spatial Script", "Could not load scripts.lst.");
        return;
    }

    SpatialScriptDialog dialog(ScriptSelectorDialog::buildEntries(_resources), this);
    if (dialog.exec() != QDialog::Accepted || dialog.programIndex() < 0) {
        return;
    }

    // Direct (synchronous) connection: the script is created before this returns.
    Q_EMIT addSpatialScriptRequested(dialog.programIndex(), dialog.tile(),
        dialog.elevation(), dialog.radius());
    updateMapScriptsDisplay();
}

void MapInfoPanel::persistMapNames() {
    if (!_map || !_mapNames || !_settings) {
        return; // (_displayNameEdit/_lookupNameEdit are created in setupUI and always present)
    }

    // Persist only the field the user actually edited (QLineEdit tracks isModified since setText). This
    // is the common fast path on a map save: nothing edited -> nothing to do.
    std::optional<std::string> lookup;
    std::optional<std::string> display;
    if (_lookupNameEdit->isModified()) {
        lookup = _lookupNameEdit->text().toStdString();
    }
    if (_displayNameEdit->isModified()) {
        display = _displayNameEdit->text().toStdString();
    }
    if (!lookup.has_value() && !display.has_value()) {
        return; // no name edits to save
    }

    const int index = _mapNames->indexOf(_map->filename()); // by basename, not the NUL-padded header field
    if (index < 0) {
        // We only get here with pending edits (checked above). The map's current name isn't in maps.txt
        // — typically because Save As renamed it to a file with no registry entry — so there's nowhere to
        // write the edits. Warn rather than dropping them silently.
        QMessageBox::warning(this, "Save Map Names",
            QString("Map name edits weren't saved: \"%1\" isn't registered in maps.txt.")
                .arg(QString::fromStdString(_map->filename())));
        return;
    }

    // Write into a writable folder from the user's Data Paths (no hidden location). If there's none, the
    // edit can't be saved -> tell the user to add one. updateOverlayHint() already shows the same hint.
    const auto target = resource::findWritableDataPath(_settings->getDataPaths());
    if (!target.has_value()) {
        QMessageBox::warning(this, "Save Map Names",
            "These map name edits can't be saved: add a writable folder to your Data Paths "
            "(all current data paths are read-only archives).");
        return;
    }
    const int elevation = static_cast<int>(_map->getMapFile().header.player_default_elevation);
    writeNameEdits(*target, index, elevation, lookup, display);
}

void MapInfoPanel::writeNameEdits(const std::filesystem::path& writableRoot, int index, int elevation,
    const std::optional<std::string>& lookup, const std::optional<std::string>& display) {
    try {
        // The whole load -> set -> validate -> serialize -> write cycle (with the hard-block) lives in
        // resource::saveMapNames, so it is unit-testable without this widget.
        const auto result = resource::saveMapNames(_resources.files(), writableRoot, index, elevation, lookup, display);
        if (!result.ok) {
            QMessageBox::warning(this, "Save Map Names", QString::fromStdString(result.error));
            return;
        }

        // Reflect the edit this session: re-mount the target so the VFS's file listing includes the
        // freshly-written file (vfspp caches the listing at mount time). Re-mounted last, the copy
        // shadows the archives. Then drop the cached map.msg and rebuild the resolver.
        _resources.files().addDataPath(writableRoot);
        _resources.repository().clear();
        _mapNames = std::make_unique<resource::MapNameResolver>(_resources);
        updateMapNameDisplay();
        spdlog::info("MapInfoPanel: saved map names to {}", writableRoot.string());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Save Map Names", QString("Failed to save map names:\n%1").arg(e.what()));
    }
}

} // namespace geck
