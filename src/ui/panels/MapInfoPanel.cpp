#include "MapInfoPanel.h"
#include "../theme/ThemeManager.h"
#include "../GameEnums.h"
#include "../UIConstants.h"

#include <QHeaderView>
#include <QApplication>
#include <QMessageBox>
#include <QSize>
#include <spdlog/spdlog.h>
#include <filesystem>

#include "../../format/map/Map.h"
#include "../../format/gam/Gam.h"
#include "../../format/lst/Lst.h"
#include "../../reader/ReaderFactory.h"
#include "../../util/ResourceManager.h"
#include "../../util/ResourcePaths.h"
#include "../../util/Coordinates.h"
#include "../IconHelper.h"

namespace geck {

MapInfoPanel::MapInfoPanel(QWidget* parent)
    : QWidget(parent)
    , _mainLayout(nullptr)
    , _scrollArea(nullptr)
    , _contentWidget(nullptr)
    , _contentLayout(nullptr)
    , _mapHeaderGroup(nullptr)
    , _filenameEdit(nullptr)
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
    , _map(nullptr)
    , _mapScriptName("no script") {

    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    setupUI();
}

QSize MapInfoPanel::sizeHint() const {
    return QSize(ui::constants::sizes::PANEL_PREFERRED_WIDTH, ui::constants::sizes::PANEL_PREFERRED_HEIGHT);
}

QSize MapInfoPanel::minimumSizeHint() const {
    return QSize(ui::constants::sizes::PANEL_MIN_SIZE_WIDTH, ui::constants::sizes::PANEL_MIN_SIZE_HEIGHT);
}

void MapInfoPanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN);

    // Create scroll area for content
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

    // Map header group
    _mapHeaderGroup = new QGroupBox("Map Header");
    QFormLayout* headerLayout = new QFormLayout(_mapHeaderGroup);

    _filenameEdit = new QLineEdit();
    headerLayout->addRow("Filename:", _filenameEdit);

    // Elevation checkboxes
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

    // Player position with button
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

    // Connect signals for editable fields
    connect(_playerPositionSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_playerElevationSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_playerOrientationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MapInfoPanel::onOrientationChanged);
    connect(_setPositionButton, &QPushButton::clicked, this, &MapInfoPanel::onSelectPositionClicked);
    connect(_centerViewButton, &QPushButton::clicked, this, &MapInfoPanel::onCenterViewClicked);
    connect(_mapScriptIdSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_darknessSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_timestampSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_savegameCheck, &QCheckBox::toggled, this, &MapInfoPanel::onFieldChanged);

    // Connect elevation checkbox signals
    connect(_elevation1Check, &QCheckBox::toggled, this, &MapInfoPanel::onElevationCheckboxChanged);
    connect(_elevation2Check, &QCheckBox::toggled, this, &MapInfoPanel::onElevationCheckboxChanged);
    connect(_elevation3Check, &QCheckBox::toggled, this, &MapInfoPanel::onElevationCheckboxChanged);

    // Global variables group
    _globalVarsGroup = new QGroupBox("Map Global Variables");
    QVBoxLayout* varsLayout = new QVBoxLayout(_globalVarsGroup);

    _globalVarsTree = new QTreeWidget();
    _globalVarsTree->setHeaderLabels({ "Variable", "Value" });
    _globalVarsTree->header()->setStretchLastSection(true);
    _globalVarsTree->setAlternatingRowColors(true);
    _globalVarsTree->setRootIsDecorated(false);
    varsLayout->addWidget(_globalVarsTree);

    _contentLayout->addWidget(_globalVarsGroup);

    // Map scripts group
    _mapScriptsGroup = new QGroupBox("Map Scripts");
    QVBoxLayout* scriptsLayout = new QVBoxLayout(_mapScriptsGroup);

    _mapScriptsLabel = new QLabel("No script information available");
    _mapScriptsLabel->setStyleSheet(ui::theme::styles::italicSecondaryText());
    _mapScriptsLabel->setWordWrap(true);
    scriptsLayout->addWidget(_mapScriptsLabel);

    _contentLayout->addWidget(_mapScriptsGroup);

    _contentLayout->addStretch(); // Add stretch to push content to top

    _scrollArea->setWidget(_contentWidget);
    _mainLayout->addWidget(_scrollArea);

    // Initially clear the display
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

        // Update map header information
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

        // Load script variables and update UI
        loadScriptVars();
        _mapScriptEdit->setText(QString::fromStdString(_mapScriptName));

        // Update global variables tree with enhanced display
        _globalVarsTree->clear();

        if (_mvars.empty() && mapInfo.header.num_global_vars > 0) {
            // Show placeholder when GAM file couldn't be loaded but header indicates variables exist
            QTreeWidgetItem* placeholderItem = new QTreeWidgetItem(_globalVarsTree);
            placeholderItem->setText(0, QString("⚠️ %1 global variables expected").arg(mapInfo.header.num_global_vars));
            placeholderItem->setText(1, "GAM file not loaded");
            placeholderItem->setForeground(0, QBrush(ui::theme::colors::statusWarningRgb())); // Orange color for warning
            placeholderItem->setForeground(1, QBrush(ui::theme::colors::statusWarningRgb()));
        } else if (_mvars.empty() && mapInfo.header.num_global_vars == 0) {
            // Show info message when no variables are expected
            QTreeWidgetItem* infoItem = new QTreeWidgetItem(_globalVarsTree);
            infoItem->setText(0, "No global variables defined");
            infoItem->setText(1, "(Map header indicates 0 variables)");
            infoItem->setForeground(0, QBrush(ui::theme::colors::statusInfoRgb())); // Gray for info
            infoItem->setForeground(1, QBrush(ui::theme::colors::statusInfoRgb()));
        } else {
            // Display actual loaded variables
            for (const auto& [key, value] : _mvars) {
                QTreeWidgetItem* item = new QTreeWidgetItem(_globalVarsTree);
                item->setText(0, QString::fromStdString(key));
                item->setText(1, QString::number(value));

                // Use green color to indicate successfully loaded data
                item->setForeground(0, QBrush(ui::theme::colors::statusSuccessRgb())); // Green for success
                item->setForeground(1, QBrush(ui::theme::colors::statusSuccessRgb()));
            }

            // Add summary item if variables were loaded successfully
            if (!_mvars.empty()) {
                QTreeWidgetItem* summaryItem = new QTreeWidgetItem(_globalVarsTree);
                summaryItem->setText(0, QString("✅ Total: %1 variables loaded").arg(_mvars.size()));
                summaryItem->setText(1, "From GAM file");
                summaryItem->setForeground(0, QBrush(ui::theme::colors::statusSuccessRgb()));
                summaryItem->setForeground(1, QBrush(ui::theme::colors::statusSuccessRgb()));
            }
        }

        // Expand and resize columns
        _globalVarsTree->expandAll();
        _globalVarsTree->resizeColumnToContents(0);

        // Update map scripts display
        updateMapScriptsDisplay();

        // Update elevation checkbox states (disable last remaining elevation)
        updateElevationCheckboxStates();

    } catch (const std::exception& e) {
        spdlog::error("Error updating map info: {}", e.what());
        clearMapInfo();
    }
}

void MapInfoPanel::loadScriptVars() {
    _mvars.clear();
    _mapScriptName = "no script";

    if (!_map) {
        return;
    }

    try {
        // Extract base filename without extension
        std::string mapFilename = _map->filename();
        std::string baseName = mapFilename.substr(0, mapFilename.find("."));
        std::string gam_filename = baseName + ".gam";
        std::string gam_path = "maps/" + gam_filename;

        Gam* gam_file = nullptr;
        if (ResourceManager::getInstance().fileExistsInVFS(gam_path)) {
            gam_file = ResourceManager::getInstance().loadResource<Gam>(gam_path);
            if (gam_file) {
                spdlog::debug("GAM file loaded from VFS: {}", gam_path);
            }
        }

        if (!gam_file) {
            spdlog::warn("GAM file '{}' not found in VFS", gam_path);
            _mapScriptName = "GAM file not found";
            return;
        }

        if (gam_file) {
            // Load global variables
            for (uint32_t index = 0; index < _map->getMapFile().header.num_global_vars; index++) {
                _mvars.emplace(gam_file->mvarKey(index), gam_file->mvarValue(index));
            }

            // Load map script name
            int map_script_id = _map->getMapFile().header.script_id;
            if (map_script_id > 0) {
                auto scripts = ResourceManager::getInstance().loadResource<Lst>(ResourcePaths::Lst::SCRIPTS);
                const auto& scriptList = scripts->list();
                if (map_script_id <= static_cast<int>(scriptList.size()) && map_script_id >= 1) {
                    _mapScriptName = scripts->at(map_script_id - 1); // script id starts at 1
                    spdlog::debug("Script name '{}' found for ID {} in: {}", _mapScriptName, map_script_id, ResourcePaths::Lst::SCRIPTS);
                } else {
                    _mapScriptName = "invalid script index";
                    spdlog::warn("Script ID {} out of bounds for scripts.lst size {} in: {}", map_script_id, scriptList.size(), ResourcePaths::Lst::SCRIPTS);
                }
            } else {
                _mapScriptName = "no script";
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Error loading script vars: {}", e.what());
        _mapScriptName = QString("Error: %1").arg(e.what()).toStdString();
        // Clear any partial data on error
        _mvars.clear();
    }
}

void MapInfoPanel::clearMapInfo() {
    _filenameEdit->clear();
    _filenameEdit->setPlaceholderText("No map loaded");

    // Clear elevation checkboxes
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

    _globalVarsTree->clear();

    _mvars.clear();
    _mapScriptName = "no script";

    // Clear scripts display
    updateMapScriptsDisplay();
}

void MapInfoPanel::onFieldChanged() {
    if (!_map) {
        return;
    }

    auto& mapInfo = _map->getMapFile();

    // Update map header values from the UI
    mapInfo.header.player_default_position = static_cast<uint32_t>(_playerPositionSpin->value());
    mapInfo.header.player_default_elevation = static_cast<uint32_t>(_playerElevationSpin->value());
    mapInfo.header.script_id = _mapScriptIdSpin->value();
    mapInfo.header.darkness = static_cast<uint32_t>(_darknessSpin->value());
    mapInfo.header.timestamp = static_cast<uint32_t>(_timestampSpin->value());

    // Update flags based on savegame checkbox
    if (_savegameCheck->isChecked()) {
        mapInfo.header.flags |= 0x1; // Set bit 0
    } else {
        mapInfo.header.flags &= ~0x1; // Clear bit 0
    }

    // Emit specific signals for position and elevation changes
    QObject* sender = QObject::sender();
    if (sender == _playerPositionSpin) {
        emit playerPositionChanged(_playerPositionSpin->value());
        // Also publish to EventBus
        EventBus::getInstance().publish(PlayerPositionChangedEvent{
            HexPosition(_playerPositionSpin->value()),
            _playerElevationSpin->value() });
    } else if (sender == _playerElevationSpin) {
        emit playerElevationChanged(_playerElevationSpin->value());
        // Position event also carries elevation
        EventBus::getInstance().publish(PlayerPositionChangedEvent{
            HexPosition(_playerPositionSpin->value()),
            _playerElevationSpin->value() });
    } else if (sender == _mapScriptIdSpin) {
        emit mapScriptIdChanged(_mapScriptIdSpin->value());
        EventBus::getInstance().publish(MapScriptChangedEvent{
            _mapScriptIdSpin->value() });
    } else if (sender == _darknessSpin) {
        emit darknessChanged(_darknessSpin->value());
        EventBus::getInstance().publish(MapPropertiesChangedEvent{
            MapPropertiesChangedEvent::Property::Darkness,
            _darknessSpin->value() });
    } else if (sender == _timestampSpin) {
        emit timestampChanged(_timestampSpin->value());
        EventBus::getInstance().publish(MapPropertiesChangedEvent{
            MapPropertiesChangedEvent::Property::Timestamp,
            _timestampSpin->value() });
    }

    spdlog::debug("MapInfoPanel: Field changed, map header updated");
}

void MapInfoPanel::onOrientationChanged(int index) {
    if (!_map) {
        return;
    }

    auto& mapInfo = _map->getMapFile();
    mapInfo.header.player_default_orientation = static_cast<uint32_t>(index);

    emit playerOrientationChanged(index);
    EventBus::getInstance().publish(PlayerOrientationChangedEvent{ index });
    spdlog::debug("MapInfoPanel: Player orientation changed to {}", index);
}

void MapInfoPanel::onSelectPositionClicked() {
    emit selectPlayerPositionRequested();
    spdlog::debug("MapInfoPanel: Player position selection requested");
}

void MapInfoPanel::onCenterViewClicked() {
    emit centerViewOnPlayerPositionRequested();
    spdlog::debug("MapInfoPanel: Center view on player position requested");
}

void MapInfoPanel::setPlayerPosition(int hexPosition) {
    if (!_map) {
        return;
    }

    // Update the UI
    _playerPositionSpin->setValue(hexPosition);

    // Update the map data (this will trigger the onFieldChanged slot)
    auto& mapInfo = _map->getMapFile();
    mapInfo.header.player_default_position = static_cast<uint32_t>(hexPosition);

    emit playerPositionChanged(hexPosition);
    EventBus::getInstance().publish(PlayerPositionChangedEvent{
        HexPosition(hexPosition),
        _playerElevationSpin->value() });
    spdlog::debug("MapInfoPanel: Player position set to hex {}", hexPosition);
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

        // Display basic script information with enhanced error reporting
        if (mapInfo.header.script_id > 0) {
            scriptsInfo += QString("Map Script ID: %1\n").arg(mapInfo.header.script_id);
            scriptsInfo += QString("Map Script Name: %1\n").arg(QString::fromStdString(_mapScriptName));

            // Show status based on script name content
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

        // Add global variables information
        scriptsInfo += QString("\nGlobal Variables: %1\n").arg(mapInfo.header.num_global_vars);
        if (_mvars.empty() && mapInfo.header.num_global_vars > 0) {
            scriptsInfo += "⚠️ Global variables not loaded (GAM file issue)\n";
        } else if (!_mvars.empty()) {
            scriptsInfo += QString("✅ %1 global variables loaded\n").arg(_mvars.size());
        }

        scriptsInfo += QString("Local Variables: %1\n").arg(mapInfo.header.num_local_vars);

        // Count scripts in each section and display details
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

                // Show some details about scripts in this section
                const auto& scripts = mapInfo.map_scripts[i];
                int actualScriptCount = static_cast<int>(scripts.size());
                totalScripts += actualScriptCount;

                // Show discrepancy if header count doesn't match actual scripts
                if (sectionCount != actualScriptCount) {
                    scriptsInfo += QString("  ⚠️ Header says %1, but found %2 actual scripts\n")
                                       .arg(sectionCount)
                                       .arg(actualScriptCount);
                }

                // Show first few scripts as examples
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

        // Set appropriate styling based on content
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

    // Count how many elevations are currently enabled
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
        // Multiple elevations exist, enable all checkboxes
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

    // Determine which checkbox triggered the change and what elevation it represents
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
        // Adding elevation - enable it in the map
        mapFile.header.flags &= ~flagBit; // Clear bit to enable elevation

        // Initialize empty tile data for this elevation if it doesn't exist
        if (mapFile.tiles.find(elevation) == mapFile.tiles.end()) {
            mapFile.tiles[elevation].clear();
            mapFile.tiles[elevation].reserve(Map::TILES_PER_ELEVATION);

            // Fill with empty tiles
            for (unsigned int i = 0; i < Map::TILES_PER_ELEVATION; i++) {
                mapFile.tiles[elevation].emplace_back(Map::EMPTY_TILE, Map::EMPTY_TILE);
            }
        }

        // Initialize empty object list for this elevation if it doesn't exist
        if (mapFile.map_objects.find(elevation) == mapFile.map_objects.end()) {
            mapFile.map_objects[elevation].clear();
        }

        emit elevationAdded(elevation);
        EventBus::getInstance().publish(ElevationChangedEvent{
            ElevationChangedEvent::Type::Added,
            elevation });
        spdlog::info("MapInfoPanel: Added {} to map", elevationName.toStdString());

        // Update checkbox states after adding elevation
        updateElevationCheckboxStates();

    } else if (!isChecked && wasEnabled) {
        // Removing elevation - show confirmation dialog
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
            // User confirmed removal
            mapFile.header.flags |= flagBit; // Set bit to disable elevation

            // Remove tile data for this elevation
            auto tileIt = mapFile.tiles.find(elevation);
            if (tileIt != mapFile.tiles.end()) {
                tileIt->second.clear();
                mapFile.tiles.erase(tileIt);
            }

            // Remove object data for this elevation
            auto objectIt = mapFile.map_objects.find(elevation);
            if (objectIt != mapFile.map_objects.end()) {
                objectIt->second.clear();
                mapFile.map_objects.erase(objectIt);
            }

            emit elevationRemoved(elevation);
            EventBus::getInstance().publish(ElevationChangedEvent{
                ElevationChangedEvent::Type::Removed,
                elevation });
            spdlog::info("MapInfoPanel: Removed {} from map", elevationName.toStdString());

            // Update checkbox states after removing elevation
            updateElevationCheckboxStates();

        } else {
            // User cancelled - revert checkbox state
            sender->blockSignals(true);
            sender->setChecked(true);
            sender->blockSignals(false);
            spdlog::debug("MapInfoPanel: Elevation removal cancelled by user");
        }
    }
}

} // namespace geck
