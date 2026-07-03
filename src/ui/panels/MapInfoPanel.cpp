#include "MapInfoPanel.h"
#include "ui/theme/ThemeManager.h"
#include "ui/GameEnums.h"
#include "ui/UIConstants.h"

#include <QHeaderView>
#include <QApplication>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSize>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <optional>

#include "format/map/Map.h"
#include "format/lst/Lst.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"
#include "resource/MapNameEditor.h"
#include "resource/WritableDataRoot.h"
#include "ui/Settings.h"
#include "reader/ReaderFactory.h"
#include "reader/gam/GamReader.h"
#include "writer/gam/GamSerializer.h"
#include "writer/gam/GamValidator.h"
#include "util/FileIo.h"
#include "resource/ResourcePaths.h"
#include "resource/ScriptNames.h"
#include "util/Coordinates.h"
#include "ui/IconHelper.h"
#include "ui/widgets/IntCellDelegate.h"

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
    , _newGlobalVarNameEdit(nullptr)
    , _addGlobalVarButton(nullptr)
    , _removeGlobalVarButton(nullptr)
    , _globalVarsSummaryLabel(nullptr)
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
    // Only the Value column (1) is editable; the delegate returns no editor for the name column and
    // constrains the value to a signed int. The view owns the delegate.
    _globalVarsTree->setItemDelegate(new IntCellDelegate(1, _globalVarsTree));
    connect(_globalVarsTree, &QTreeWidget::itemChanged, this, &MapInfoPanel::onGlobalVarChanged);
    connect(_globalVarsTree, &QTreeWidget::itemSelectionChanged, this, &MapInfoPanel::onGlobalVarSelectionChanged);
    varsLayout->addWidget(_globalVarsTree);

    // Count + source summary, directly under the table.
    _globalVarsSummaryLabel = new QLabel();
    _globalVarsSummaryLabel->setObjectName("globalVarsSummary");
    _globalVarsSummaryLabel->setStyleSheet(ui::theme::styles::italicSecondaryText());
    _globalVarsSummaryLabel->setWordWrap(true);
    varsLayout->addWidget(_globalVarsSummaryLabel);

    // Add / Remove controls. A new variable is appended as the positionally-last map global (so existing
    // indices stay stable); Remove shifts later indices and is gated on a real variable row being selected.
    QHBoxLayout* varsControls = new QHBoxLayout();
    _newGlobalVarNameEdit = new QLineEdit();
    _newGlobalVarNameEdit->setObjectName("newGlobalVarName");
    _newGlobalVarNameEdit->setPlaceholderText("New variable name");
    _addGlobalVarButton = new QPushButton("Add");
    _addGlobalVarButton->setObjectName("addGlobalVar");
    _addGlobalVarButton->setToolTip("Append a new map global variable (added last so existing indices don't move).");
    _removeGlobalVarButton = new QPushButton("Remove");
    _removeGlobalVarButton->setObjectName("removeGlobalVar");
    _removeGlobalVarButton->setToolTip("Remove the selected variable (shifts later variables' indices).");
    varsControls->addWidget(_newGlobalVarNameEdit, 1);
    varsControls->addWidget(_addGlobalVarButton);
    varsControls->addWidget(_removeGlobalVarButton);
    varsLayout->addLayout(varsControls);

    connect(_addGlobalVarButton, &QPushButton::clicked, this, &MapInfoPanel::onAddGlobalVar);
    connect(_removeGlobalVarButton, &QPushButton::clicked, this, &MapInfoPanel::onRemoveGlobalVar);
    connect(_newGlobalVarNameEdit, &QLineEdit::returnPressed, this, &MapInfoPanel::onAddGlobalVar);

    _contentLayout->addWidget(_globalVarsGroup);

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

        populateGlobalVars();

        // Disable the last remaining elevation's checkbox.
        updateElevationCheckboxStates();

    } catch (const std::exception& e) {
        _suppressFieldChanged = false; // never leave write-back disabled if population threw mid-way
        _suppressVarEdit = false;
        spdlog::error("Error updating map info: {}", e.what());
        clearMapInfo();
    }
}

void MapInfoPanel::populateGlobalVars() {
    const uint32_t headerVarCount = _map ? _map->getMapFile().header.num_global_vars : 0;

    // Guard the value-edited handler while we fill the tree: the setText calls below would otherwise
    // fire onGlobalVarChanged as spurious user edits. Cleared at the end (and by updateMapInfo's catch).
    _suppressVarEdit = true;
    _globalVarsTree->clear();

    if (_mvars.empty() && headerVarCount > 0) {
        // Show placeholder when GAM file couldn't be loaded but header indicates variables exist
        QTreeWidgetItem* placeholderItem = new QTreeWidgetItem(_globalVarsTree);
        placeholderItem->setText(0, QString("⚠️ %1 global variables expected").arg(headerVarCount));
        placeholderItem->setText(1, "GAM file not loaded");
        placeholderItem->setForeground(0, QBrush(ui::theme::colors::statusWarningRgb()));
        placeholderItem->setForeground(1, QBrush(ui::theme::colors::statusWarningRgb()));
    } else if (_mvars.empty()) {
        QTreeWidgetItem* infoItem = new QTreeWidgetItem(_globalVarsTree);
        infoItem->setText(0, "No global variables defined");
        infoItem->setText(1, "(Map header indicates 0 variables)");
        infoItem->setForeground(0, QBrush(ui::theme::colors::statusInfoRgb()));
        infoItem->setForeground(1, QBrush(ui::theme::colors::statusInfoRgb()));
    } else {
        // The i-th _mvars row is the i-th MAP_GLOBAL_VARS variable in the .gam. Make the Value cell
        // editable and stash that index in its UserRole so onGlobalVarChanged can write straight to
        // Gam::setMapGlobalVar(i, ...).
        for (int i = 0; i < static_cast<int>(_mvars.size()); ++i) {
            const auto& [key, value] = _mvars[static_cast<size_t>(i)];
            QTreeWidgetItem* item = new QTreeWidgetItem(_globalVarsTree);
            item->setText(0, QString::fromStdString(key));
            item->setText(1, QString::number(value));
            item->setData(1, Qt::UserRole, i);
            // Per-item flag turns on editing for the whole row; the delegate gates the name column.
            // Variable rows use the default text colour (they're data, not a status).
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
    }

    // Count + source caption, shown as a label under the table rather than a row inside it.
    if (!_mvars.empty()) {
        const QString gamFile = QString::fromStdString(std::filesystem::path(_gamPath).filename().string());
        _globalVarsSummaryLabel->setText(QString("Total: %1 variables from %2").arg(_mvars.size()).arg(gamFile));
        _globalVarsSummaryLabel->setVisible(true);
    } else {
        _globalVarsSummaryLabel->clear();
        _globalVarsSummaryLabel->setVisible(false);
    }

    _suppressVarEdit = false; // population done; user edits write back from here on

    _globalVarsTree->expandAll();
    _globalVarsTree->resizeColumnToContents(0);
    updateGlobalVarButtons();
}

void MapInfoPanel::updateGlobalVarButtons() {
    // Add needs a loaded .gam to append into; Remove also needs a real (index-carrying) variable row.
    const bool hasGam = _gamDoc.has_value();
    if (_addGlobalVarButton) {
        _addGlobalVarButton->setEnabled(hasGam);
    }
    if (_newGlobalVarNameEdit) {
        _newGlobalVarNameEdit->setEnabled(hasGam);
    }
    if (_removeGlobalVarButton) {
        QTreeWidgetItem* selected = _globalVarsTree->currentItem();
        const bool realRow = selected != nullptr && selected->data(1, Qt::UserRole).isValid();
        _removeGlobalVarButton->setEnabled(hasGam && realRow);
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
    _gamDoc.reset();
    _gamPath.clear();
    _globalVarsEdited = false;
    _mapScriptName = "no script";

    if (!_map) {
        return;
    }

    try {
        // The map's own script (header.script_id, 1-based) names a scripts.lst entry. Resolve its
        // filename and scrname.msg description independently of the .gam file (which only carries the
        // map's global variables), so the name shows even for a map that has no .gam.
        if (const int map_script_id = _map->getMapFile().header.script_id; map_script_id > 0) {
            auto scripts = _resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS);
            const auto& scriptList = scripts->list();
            if (map_script_id <= static_cast<int>(scriptList.size()) && map_script_id >= 1) {
                _mapScriptName = scripts->at(map_script_id - 1); // script id starts at 1
                // Append the friendly description for the same script (its 0-based index is id - 1):
                // the scrname.msg name, or the scripts.lst comment when scrname.msg doesn't name it.
                const std::string desc = resource::scriptDescription(_resources, map_script_id - 1);
                if (!desc.empty()) {
                    _mapScriptName += " — " + desc;
                }
            } else {
                _mapScriptName = "invalid script index";
                spdlog::warn("Map script ID {} out of bounds for scripts.lst size {}", map_script_id, scriptList.size());
            }
        }

        // For a BASE map the engine re-reads the map's global variables from the map's .gam
        // MAP_GLOBAL_VARS section (fallout2-ce map.cc), ignoring the .map's own map_global_vars block. So
        // the .gam — not the .map — is both the source of truth for the displayed value AND where edits
        // are written back. Parse it losslessly so an edited value can be written without disturbing
        // names, spacing, or comments.
        const std::string baseName = _map->filename().substr(0, _map->filename().find("."));
        _gamPath = "maps/" + baseName + ".gam";
        if (const auto bytes = _resources.files().readRawBytes(_gamPath); bytes.has_value()) {
            _gamDoc = GamReader::parse(std::string(bytes->begin(), bytes->end()));
            for (const auto& [name, value] : _gamDoc->mapGlobalVars()) {
                _mvars.emplace_back(name, value);
            }
        } else {
            spdlog::debug("GAM file '{}' not found in VFS; map global variables unavailable", _gamPath);
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
    if (_newGlobalVarNameEdit) {
        _newGlobalVarNameEdit->clear();
    }

    _mvars.clear();
    _gamDoc.reset();
    _gamPath.clear();
    _globalVarsEdited = false;
    _mapScriptName = "no script";

    updateGlobalVarButtons(); // no map -> Add/Remove disabled
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
        spdlog::debug("MapInfoPanel: Added {} to map", elevationName.toStdString());

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
            spdlog::debug("MapInfoPanel: Removed {} from map", elevationName.toStdString());

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
    // The dialog (with its map-pick flow) is owned by MainWindow, which brokers the map click.
    Q_EMIT addSpatialScriptRequested();
}

void MapInfoPanel::onGlobalVarChanged(QTreeWidgetItem* item, int column) {
    // Ignore the setText calls fired while updateMapInfo() populates the tree, edits to the name column,
    // and the placeholder/info/summary rows (which carry no UserRole index).
    if (_suppressVarEdit || !_map || !_gamDoc.has_value() || item == nullptr || column != 1) {
        return;
    }

    const QVariant indexData = item->data(1, Qt::UserRole);
    if (!indexData.isValid()) {
        return; // not a real variable row (placeholder / info / summary)
    }
    const int index = indexData.toInt();
    if (index < 0 || index >= static_cast<int>(_mvars.size())) {
        return; // tree row outruns the loaded variables; leave the .gam untouched
    }

    bool parsed = false;
    const int value = item->text(1).toInt(&parsed);
    if (!parsed) {
        // The delegate's QIntValidator normally prevents this, but guard anyway: revert the cell text to
        // the stored value without re-triggering this handler.
        _suppressVarEdit = true;
        item->setText(1, QString::number(_mvars[static_cast<size_t>(index)].second));
        _suppressVarEdit = false;
        return;
    }

    // The .gam is the source of truth for a BASE map's global variables: rewrite the value in the
    // MAP_GLOBAL_VARS line (preserving its name/spacing/comment) and flag the .gam dirty so it's written
    // alongside the .map on save.
    _gamDoc->setMapGlobalVar(static_cast<std::size_t>(index), value);
    _mvars[static_cast<size_t>(index)].second = value; // keep the display copy in sync
    // Mirror the edit into the .map's stored global-var block so the two stay in sync — the engine
    // snapshots the .gam into the .map on save and our analyzer reads the .map. saveMap writes it.
    if (auto& mapVars = _map->getMapFile().map_global_vars; static_cast<std::size_t>(index) < mapVars.size()) {
        mapVars[static_cast<std::size_t>(index)] = value;
    }
    _globalVarsEdited = true;

    Q_EMIT mapVariablesChanged();
    spdlog::debug("MapInfoPanel: global var [{}] set to {}", index, value);
}

void MapInfoPanel::onGlobalVarSelectionChanged() {
    updateGlobalVarButtons();
}

void MapInfoPanel::onAddGlobalVar() {
    if (!_map || !_gamDoc.has_value() || !_newGlobalVarNameEdit) {
        return; // no .gam loaded -> nothing to append into (the controls are disabled too)
    }

    const QString name = _newGlobalVarNameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Add Map Variable", "Enter a name for the new global variable.");
        return;
    }

    // The engine names map globals with a script identifier; only accept that shape so the line stays
    // parseable (and matches the engine's NAME := value; variable shape).
    static const QRegularExpression kIdentifier(QStringLiteral("^\\w+$"));
    if (!kIdentifier.match(name).hasMatch()) {
        QMessageBox::warning(this, "Add Map Variable",
            QString("\"%1\" is not a valid variable name. Use letters, digits, and underscores only.").arg(name));
        return;
    }

    // The engine indexes positionally but the names must stay distinct for the editor; reject a duplicate
    // (case-sensitive — the engine treats names verbatim).
    const std::string newName = name.toStdString();
    const bool duplicate = std::any_of(_mvars.begin(), _mvars.end(),
        [&newName](const std::pair<std::string, int>& var) { return var.first == newName; });
    if (duplicate) {
        QMessageBox::warning(this, "Add Map Variable",
            QString("A map global variable named \"%1\" already exists.").arg(name));
        return;
    }

    // New variables default to 0 and are appended last so existing variables keep their index. The user
    // can then edit the value cell. Re-run the populate path so the new (editable) row shows.
    if (!_gamDoc->addMapGlobalVar(newName, 0)) {
        QMessageBox::warning(this, "Add Map Variable",
            "This map's .gam has no MAP_GLOBAL_VARS section to add a variable to.");
        return;
    }
    _mvars.emplace_back(newName, 0);
    // Keep the .map's stored block in sync: append a matching slot (the .gam appended last, so indices
    // line up) and bump the header count.
    auto& addMapFile = _map->getMapFile();
    addMapFile.map_global_vars.push_back(0);
    addMapFile.header.num_global_vars = static_cast<uint32_t>(addMapFile.map_global_vars.size());
    _globalVarsEdited = true;
    _newGlobalVarNameEdit->clear();
    populateGlobalVars();

    Q_EMIT mapVariablesChanged();
    spdlog::debug("MapInfoPanel: added map global var '{}'", newName);
}

void MapInfoPanel::onRemoveGlobalVar() {
    if (!_map || !_gamDoc.has_value()) {
        return;
    }

    QTreeWidgetItem* selected = _globalVarsTree->currentItem();
    if (selected == nullptr) {
        return;
    }
    const QVariant indexData = selected->data(1, Qt::UserRole);
    if (!indexData.isValid()) {
        return; // not a real variable row (placeholder / info / summary)
    }
    const int index = indexData.toInt();
    if (index < 0 || index >= static_cast<int>(_mvars.size())) {
        return;
    }

    const QString varName = QString::fromStdString(_mvars[static_cast<size_t>(index)].first);
    const auto reply = QMessageBox::warning(this, "Remove Map Variable",
        QString("Remove global variable \"%1\"? This shifts the index of every variable after it. "
                "Compiled scripts reference map variables by position, so this can break scripts on maps "
                "that use them.")
            .arg(varName),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (reply != QMessageBox::Yes) {
        return;
    }

    if (!_gamDoc->removeMapGlobalVar(static_cast<std::size_t>(index))) {
        return;
    }
    _mvars.erase(_mvars.begin() + index);
    // Keep the .map's stored block in sync: drop the same slot (later vars shift down in both) and
    // lower the header count.
    if (auto& removeMapFile = _map->getMapFile(); static_cast<std::size_t>(index) < removeMapFile.map_global_vars.size()) {
        removeMapFile.map_global_vars.erase(removeMapFile.map_global_vars.begin() + index);
        removeMapFile.header.num_global_vars = static_cast<uint32_t>(removeMapFile.map_global_vars.size());
    }
    _globalVarsEdited = true;
    populateGlobalVars();

    Q_EMIT mapVariablesChanged();
    spdlog::debug("MapInfoPanel: removed map global var [{}] '{}'", index, varName.toStdString());
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

void MapInfoPanel::persistMapVars() {
    // Fast path: nothing edited (the common case on a map save), or no .gam was loaded to edit.
    if (!_globalVarsEdited || !_gamDoc.has_value() || !_settings) {
        return;
    }

    // Validate before writing: never persist a .gam that an edit corrupted. The round-trip check confirms
    // the file will re-read to the same map-global variables. On any Error, list them and bail without
    // touching the file — mirroring how the maps.txt save path hard-blocks on Error.
    const std::vector<writer::GamIssue> issues = writer::validateGam(*_gamDoc);
    if (writer::hasErrors(issues)) {
        QString details;
        for (const writer::GamIssue& issue : issues) {
            if (issue.severity == writer::GamIssue::Severity::Error) {
                details += "\n- " + QString::fromStdString(issue.message);
            }
        }
        QMessageBox::warning(this, "Save Map Variables",
            "These global variable edits can't be saved (validation failed):" + details);
        return;
    }

    // Write into a writable folder from the user's Data Paths (no hidden location). If there's none, the
    // edit can't be saved -> tell the user to add one (mirrors persistMapNames()).
    const auto target = resource::findWritableDataPath(_settings->getDataPaths());
    if (!target.has_value()) {
        QMessageBox::warning(this, "Save Map Variables",
            "These global variable edits can't be saved: add a writable folder to your Data Paths "
            "(all current data paths are read-only archives).");
        return;
    }

    try {
        // The .gam path mirrors its VFS-relative path ("maps/<base>.gam") under the writable root.
        const std::filesystem::path outPath = *target / _gamPath;
        geck::io::writeFile(outPath, writer::serializeGam(*_gamDoc));
        // The VFS caches each native mount's file list, so tell it to re-scan — otherwise this freshly
        // written .gam stays invisible to the next reload and the archived (unedited) copy is read back.
        _resources.files().refresh();
        _globalVarsEdited = false; // persisted; clear the dirty flag
        spdlog::debug("MapInfoPanel: saved map global variables to {}", outPath.string());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Save Map Variables",
            QString("Failed to save map variables:\n%1").arg(e.what()));
    }
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
        spdlog::debug("MapInfoPanel: saved map names to {}", writableRoot.string());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Save Map Names", QString("Failed to save map names:\n%1").arg(e.what()));
    }
}

} // namespace geck
