#include "MapInfoPanel.h"

#include <QHeaderView>
#include <QApplication>
#include <spdlog/spdlog.h>
#include <filesystem>

#include "../format/map/Map.h"
#include "../format/gam/Gam.h"
#include "../format/lst/Lst.h"
#include "../reader/ReaderFactory.h"
#include "../util/ResourceManager.h"

namespace geck {

MapInfoPanel::MapInfoPanel(QWidget* parent)
    : QWidget(parent)
    , _mainLayout(nullptr)
    , _scrollArea(nullptr)
    , _contentWidget(nullptr)
    , _contentLayout(nullptr)
    , _mapHeaderGroup(nullptr)
    , _filenameEdit(nullptr)
    , _elevationsSpin(nullptr)
    , _playerPositionSpin(nullptr)
    , _setPositionButton(nullptr)
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

    setupUI();
}

void MapInfoPanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(5, 5, 5, 5);

    // Create scroll area for content
    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    _contentWidget = new QWidget();
    _contentLayout = new QVBoxLayout(_contentWidget);
    _contentLayout->setContentsMargins(5, 5, 5, 5);

    // Map header group
    _mapHeaderGroup = new QGroupBox("Map Header");
    QFormLayout* headerLayout = new QFormLayout(_mapHeaderGroup);

    _filenameEdit = new QLineEdit();
    _filenameEdit->setReadOnly(true);
    headerLayout->addRow("Filename:", _filenameEdit);

    _elevationsSpin = new QSpinBox();
    _elevationsSpin->setRange(0, 99);
    _elevationsSpin->setReadOnly(true);  // Keep read-only as this depends on actual map data
    _elevationsSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Map elevations:", _elevationsSpin);

    // Player position with button
    QHBoxLayout* positionLayout = new QHBoxLayout();
    _playerPositionSpin = new QSpinBox();
    _playerPositionSpin->setRange(0, 39999);  // Max hex position (200x200 grid)
    _setPositionButton = new QPushButton("📍");  // Crosshair/pin icon
    _setPositionButton->setMaximumWidth(30);
    _setPositionButton->setToolTip("Click to select position on map");
    positionLayout->addWidget(_playerPositionSpin);
    positionLayout->addWidget(_setPositionButton);
    headerLayout->addRow("Player default position:", positionLayout);

    _playerElevationSpin = new QSpinBox();
    _playerElevationSpin->setRange(0, 99);
    headerLayout->addRow("Player default elevation:", _playerElevationSpin);

    _playerOrientationCombo = new QComboBox();
    _playerOrientationCombo->addItems({"North-East", "East", "South-East", "South-West", "West", "North-West"});
    headerLayout->addRow("Player default orientation:", _playerOrientationCombo);

    _globalVarsSpin = new QSpinBox();
    _globalVarsSpin->setRange(0, INT_MAX);
    _globalVarsSpin->setReadOnly(true);  // Keep read-only - depends on actual variables
    _globalVarsSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Global variables #:", _globalVarsSpin);

    _localVarsSpin = new QSpinBox();
    _localVarsSpin->setRange(0, INT_MAX);
    _localVarsSpin->setReadOnly(true);  // Keep read-only - depends on actual variables
    _localVarsSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Local variables #:", _localVarsSpin);

    _mapScriptEdit = new QLineEdit();
    _mapScriptEdit->setReadOnly(true);
    headerLayout->addRow("Map script:", _mapScriptEdit);

    _mapScriptIdSpin = new QSpinBox();
    _mapScriptIdSpin->setRange(-1, INT_MAX);  // -1 means no script
    headerLayout->addRow("Map script ID:", _mapScriptIdSpin);

    _darknessSpin = new QSpinBox();
    _darknessSpin->setRange(0, 10);  // Typical darkness range
    headerLayout->addRow("Darkness:", _darknessSpin);

    _mapIdSpin = new QSpinBox();
    _mapIdSpin->setRange(0, INT_MAX);
    _mapIdSpin->setReadOnly(true);  // Map ID should not be changed
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
    connect(_mapScriptIdSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_darknessSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_timestampSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapInfoPanel::onFieldChanged);
    connect(_savegameCheck, &QCheckBox::toggled, this, &MapInfoPanel::onFieldChanged);

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
    _mapScriptsLabel->setStyleSheet("color: gray; font-style: italic;");
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
        int elevations = mapInfo.tiles.size();

        // Update map header information
        _filenameEdit->setText(QString::fromStdString(mapInfo.header.filename));
        _elevationsSpin->setValue(elevations);
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

        // Update global variables tree
        _globalVarsTree->clear();
        for (const auto& [key, value] : _mvars) {
            QTreeWidgetItem* item = new QTreeWidgetItem(_globalVarsTree);
            item->setText(0, QString::fromStdString(key));
            item->setText(1, QString::number(value));
        }

        // Expand and resize columns
        _globalVarsTree->expandAll();
        _globalVarsTree->resizeColumnToContents(0);
        
        // Update map scripts display
        updateMapScriptsDisplay();

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
        auto gam_filename = _map->filename().substr(0, _map->filename().find(".")) + ".gam";
        auto gam_filepath = std::filesystem::path("maps") / gam_filename;

        if (!std::filesystem::exists(gam_filepath)) {
            spdlog::debug("GAM file not found: {}", gam_filepath.string());
            return;
        }

        auto gam_file = ResourceManager::getInstance().loadResource<Gam>(gam_filepath);

        if (gam_file) {
            // Load global variables
            for (uint32_t index = 0; index < _map->getMapFile().header.num_global_vars; index++) {
                _mvars.emplace(gam_file->mvarKey(index), gam_file->mvarValue(index));
            }

            // Load map script name
            int map_script_id = _map->getMapFile().header.script_id;
            if (map_script_id > 0) {
                // Try different possible locations for scripts.lst
                std::vector<std::string> possiblePaths = {
                    "scripts/scripts.lst",
                    "scripts.lst",
                    "data/scripts.lst",
                    "text/english/game/scripts.lst"
                };
                
                bool scriptFound = false;
                for (const auto& path : possiblePaths) {
                    auto scripts = ResourceManager::getInstance().loadResource<Lst>(path);
                    if (scripts) {
                        try {
                            _mapScriptName = scripts->at(map_script_id - 1); // script id starts at 1
                            scriptFound = true;
                            spdlog::debug("Found scripts.lst at: {}", path);
                            break;
                        } catch (const std::exception& e) {
                            spdlog::warn("Failed to get script name for ID {} from {}: {}", map_script_id, path, e.what());
                        }
                    }
                }
                
                if (!scriptFound) {
                    _mapScriptName = QString("Script ID %1 (scripts.lst not found)").arg(map_script_id).toStdString();
                    spdlog::warn("Could not load scripts.lst from any known location for script ID {}", map_script_id);
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("Error loading script vars: {}", e.what());
        _mapScriptName = "Error loading script info";
    }
}

void MapInfoPanel::clearMapInfo() {
    _filenameEdit->clear();
    _filenameEdit->setPlaceholderText("No map loaded");

    _elevationsSpin->setValue(0);
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
        mapInfo.header.flags |= 0x1;  // Set bit 0
    } else {
        mapInfo.header.flags &= ~0x1; // Clear bit 0
    }
    
    // Emit specific signals for position and elevation changes
    QObject* sender = QObject::sender();
    if (sender == _playerPositionSpin) {
        emit playerPositionChanged(_playerPositionSpin->value());
    } else if (sender == _playerElevationSpin) {
        emit playerElevationChanged(_playerElevationSpin->value());
    } else if (sender == _mapScriptIdSpin) {
        emit mapScriptIdChanged(_mapScriptIdSpin->value());
    } else if (sender == _darknessSpin) {
        emit darknessChanged(_darknessSpin->value());
    } else if (sender == _timestampSpin) {
        emit timestampChanged(_timestampSpin->value());
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
    spdlog::debug("MapInfoPanel: Player orientation changed to {}", index);
}

void MapInfoPanel::onSelectPositionClicked() {
    emit selectPlayerPositionRequested();
    spdlog::debug("MapInfoPanel: Player position selection requested");
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
    spdlog::debug("MapInfoPanel: Player position set to hex {}", hexPosition);
}

void MapInfoPanel::updateMapScriptsDisplay() {
    if (!_map) {
        _mapScriptsLabel->setText("No map loaded");
        _mapScriptsLabel->setStyleSheet("color: gray; font-style: italic;");
        return;
    }

    try {
        auto& mapInfo = _map->getMapFile();
        QString scriptsInfo;
        
        // Display basic script information
        if (mapInfo.header.script_id > 0) {
            scriptsInfo += QString("Map Script ID: %1\n").arg(mapInfo.header.script_id);
            scriptsInfo += QString("Map Script Name: %1\n").arg(QString::fromStdString(_mapScriptName));
        } else if (mapInfo.header.script_id == -1) {
            scriptsInfo += "No map script assigned\n";
        } else {
            scriptsInfo += QString("Invalid script ID: %1\n").arg(mapInfo.header.script_id);
        }
        
        // Count scripts in each section and display details
        int totalScripts = 0;
        bool hasObjectScripts = false;
        
        for (int i = 0; i < Map::SCRIPT_SECTIONS; i++) {
            int sectionCount = mapInfo.scripts_in_section[i];
            if (sectionCount > 0) {
                hasObjectScripts = true;
                scriptsInfo += QString("Section %1: %2 scripts\n").arg(i).arg(sectionCount);
                
                // Show some details about scripts in this section
                const auto& scripts = mapInfo.map_scripts[i];
                totalScripts += scripts.size(); // Use actual script vector size
                
                for (size_t j = 0; j < std::min(static_cast<size_t>(3), scripts.size()); j++) {
                    const auto& script = scripts[j];
                    auto scriptType = MapScript::fromPid(script.pid);
                    scriptsInfo += QString("  - Script PID: %1, Type: %2, ID: %3\n")
                        .arg(script.pid)
                        .arg(QString::fromStdString(std::string(MapScript::toString(scriptType))))
                        .arg(script.script_id);
                }
                if (scripts.size() > 3) {
                    scriptsInfo += QString("  - ... and %1 more\n").arg(scripts.size() - 3);
                }
            }
        }
        
        if (!hasObjectScripts) {
            scriptsInfo += "No object scripts found";
        } else {
            scriptsInfo += QString("\nTotal object scripts: %1").arg(totalScripts);
        }
        
        _mapScriptsLabel->setText(scriptsInfo.trimmed());
        _mapScriptsLabel->setStyleSheet("color: black; font-family: monospace;"); // Remove gray styling when we have data
        
    } catch (const std::exception& e) {
        _mapScriptsLabel->setText(QString("Error loading script information: %1").arg(e.what()));
        _mapScriptsLabel->setStyleSheet("color: red; font-style: italic;");
        spdlog::error("Error updating map scripts display: {}", e.what());
    }
}

} // namespace geck