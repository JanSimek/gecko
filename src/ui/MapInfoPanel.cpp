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
    headerLayout->addRow("Filename:", _filenameEdit);

    _elevationsSpin = new QSpinBox();
    _elevationsSpin->setRange(ELEVATION_1, ELEVATION_3);
    _elevationsSpin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    headerLayout->addRow("Map elevations:", _elevationsSpin);

    // Player position with button
    QHBoxLayout* positionLayout = new QHBoxLayout();
    _playerPositionSpin = new QSpinBox();
    _playerPositionSpin->setRange(0, 39999);  // Max hex position (200x200 grid)
    _setPositionButton = new QPushButton();
    _setPositionButton->setIcon(QIcon(":/icons/actions/map-pin.svg"));
    _setPositionButton->setMaximumWidth(30);
    _setPositionButton->setToolTip("Click to select position on map");
    _centerViewButton = new QPushButton();
    _centerViewButton->setIcon(QIcon(":/icons/actions/target-arrow.svg"));
    _centerViewButton->setMaximumWidth(30);
    _centerViewButton->setToolTip("Center view on player position");
    positionLayout->addWidget(_playerPositionSpin);
    positionLayout->addWidget(_setPositionButton);
    positionLayout->addWidget(_centerViewButton);
    headerLayout->addRow("Player default position:", positionLayout);

    _playerElevationSpin = new QSpinBox();
    _playerElevationSpin->setRange(ELEVATION_1, ELEVATION_3);
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
    _darknessSpin->setReadOnly(true);  // unused
    _darknessSpin->setRange(0, 10);
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
    connect(_centerViewButton, &QPushButton::clicked, this, &MapInfoPanel::onCenterViewClicked);
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

        int elevations = 0;
        if ((mapInfo.header.flags & 0x2) == 0) {
            spdlog::debug("Map has elevation at level 1");
            elevations++;
        }
        if ((mapInfo.header.flags & 0x4) == 0) {
            spdlog::debug("Map has elevation at level 2");
            elevations++;
        }
        if ((mapInfo.header.flags & 0x8) == 0) {
            spdlog::debug("Map has elevation at level 3");
            elevations++;
        }

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

        // Update global variables tree with enhanced display
        _globalVarsTree->clear();
        
        if (_mvars.empty() && mapInfo.header.num_global_vars > 0) {
            // Show placeholder when GAM file couldn't be loaded but header indicates variables exist
            QTreeWidgetItem* placeholderItem = new QTreeWidgetItem(_globalVarsTree);
            placeholderItem->setText(0, QString("⚠️ %1 global variables expected").arg(mapInfo.header.num_global_vars));
            placeholderItem->setText(1, "GAM file not loaded");
            placeholderItem->setForeground(0, QBrush(QColor(245, 124, 0))); // Orange color for warning
            placeholderItem->setForeground(1, QBrush(QColor(245, 124, 0)));
        } else if (_mvars.empty() && mapInfo.header.num_global_vars == 0) {
            // Show info message when no variables are expected
            QTreeWidgetItem* infoItem = new QTreeWidgetItem(_globalVarsTree);
            infoItem->setText(0, "No global variables defined");
            infoItem->setText(1, "(Map header indicates 0 variables)");
            infoItem->setForeground(0, QBrush(QColor(128, 128, 128))); // Gray for info
            infoItem->setForeground(1, QBrush(QColor(128, 128, 128)));
        } else {
            // Display actual loaded variables
            for (const auto& [key, value] : _mvars) {
                QTreeWidgetItem* item = new QTreeWidgetItem(_globalVarsTree);
                item->setText(0, QString::fromStdString(key));
                item->setText(1, QString::number(value));
                
                // Use green color to indicate successfully loaded data
                item->setForeground(0, QBrush(QColor(56, 142, 60))); // Green for success
                item->setForeground(1, QBrush(QColor(56, 142, 60)));
            }
            
            // Add summary item if variables were loaded successfully
            if (!_mvars.empty()) {
                QTreeWidgetItem* summaryItem = new QTreeWidgetItem(_globalVarsTree);
                summaryItem->setText(0, QString("✅ Total: %1 variables loaded").arg(_mvars.size()));
                summaryItem->setText(1, "From GAM file");
                summaryItem->setForeground(0, QBrush(QColor(56, 142, 60)));
                summaryItem->setForeground(1, QBrush(QColor(56, 142, 60)));
            }
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
        // Extract base filename without extension
        std::string mapFilename = _map->filename();
        std::string baseName = mapFilename.substr(0, mapFilename.find("."));
        std::string gam_filename = baseName + ".gam";
        
        // Try different possible locations for GAM files in VFS
        std::vector<std::string> possibleGamPaths = {
            gam_filename,                    // Root directory
            "maps/" + gam_filename,         // maps subdirectory  
            "data/" + gam_filename,         // data subdirectory
            "data/maps/" + gam_filename     // data/maps subdirectory
        };
        
        Gam* gam_file = nullptr;
        std::string foundGamPath;
        
        // Try to find and load GAM file from VFS
        for (const auto& path : possibleGamPaths) {
            if (ResourceManager::getInstance().fileExistsInVFS(path)) {
                gam_file = ResourceManager::getInstance().loadResource<Gam>(path);
                if (gam_file) {
                    foundGamPath = path;
                    spdlog::debug("GAM file loaded from VFS: {}", path);
                    break;
                }
            }
        }
        
        if (!gam_file) {
            spdlog::warn("GAM file '{}' not found in VFS at any expected location:", gam_filename);
            for (const auto& path : possibleGamPaths) {
                spdlog::warn("  - Tried: {}", path);
            }
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
                auto scripts = ResourceManager::getInstance().loadResource<Lst>("scripts/scripts.lst");
                const auto& scriptList = scripts->list();
                if (map_script_id <= static_cast<int>(scriptList.size()) && map_script_id >= 1) {
                    _mapScriptName = scripts->at(map_script_id - 1); // script id starts at 1
                    spdlog::debug("Script name '{}' found for ID {} in: {}", _mapScriptName, map_script_id, "scripts/scripts.lst");
                } else {
                    _mapScriptName = "invalid script index";
                    spdlog::warn("Script ID {} out of bounds for scripts.lst size {} in: {}", map_script_id, scriptList.size(), "scripts/scripts.lst");
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
                    case 0: sectionName = "System"; break;
                    case 1: sectionName = "Spatial"; break;
                    case 2: sectionName = "Timer"; break;
                    case 3: sectionName = "Item"; break;
                    case 4: sectionName = "Critter"; break;
                    default: sectionName = QString("Section %1").arg(i); break;
                }
                
                scriptsInfo += QString("%1 Scripts: %2\n").arg(sectionName).arg(sectionCount);
                
                // Show some details about scripts in this section
                const auto& scripts = mapInfo.map_scripts[i];
                int actualScriptCount = static_cast<int>(scripts.size());
                totalScripts += actualScriptCount;
                
                // Show discrepancy if header count doesn't match actual scripts
                if (sectionCount != actualScriptCount) {
                    scriptsInfo += QString("  ⚠️ Header says %1, but found %2 actual scripts\n")
                        .arg(sectionCount).arg(actualScriptCount);
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
            _mapScriptsLabel->setStyleSheet("color: #D32F2F; font-family: monospace;"); // Red for errors
        } else if (scriptsInfo.contains("⚠️")) {
            _mapScriptsLabel->setStyleSheet("color: #F57C00; font-family: monospace;"); // Orange for warnings
        } else {
            _mapScriptsLabel->setStyleSheet("color: black; font-family: monospace;"); // Black for normal data
        }
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("❌ Error loading script information:\n%1").arg(e.what());
        _mapScriptsLabel->setText(errorMsg);
        _mapScriptsLabel->setStyleSheet("color: #D32F2F; font-style: italic; font-family: monospace;");
        spdlog::error("Error updating map scripts display: {}", e.what());
    }
}

} // namespace geck