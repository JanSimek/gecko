#include "Qt6MapInfoPanel.h"

#include <QHeaderView>
#include <QApplication>
#include <spdlog/spdlog.h>
#include <filesystem>

#include "../format/map/Map.h"
#include "../format/gam/Gam.h"
#include "../format/lst/Lst.h"
#include "../reader/gam/GamReader.h"
#include "../reader/lst/LstReader.h"
#include "../util/ResourceManager.h"

namespace geck {

Qt6MapInfoPanel::Qt6MapInfoPanel(QWidget* parent)
    : QWidget(parent)
    , _mainLayout(nullptr)
    , _scrollArea(nullptr)
    , _contentWidget(nullptr)
    , _contentLayout(nullptr)
    , _mapHeaderGroup(nullptr)
    , _filenameEdit(nullptr)
    , _elevationsSpin(nullptr)
    , _playerPositionSpin(nullptr)
    , _playerElevationSpin(nullptr)
    , _playerOrientationSpin(nullptr)
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

void Qt6MapInfoPanel::setupUI() {
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
    _elevationsSpin->setReadOnly(true);
    _elevationsSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Map elevations:", _elevationsSpin);
    
    _playerPositionSpin = new QSpinBox();
    _playerPositionSpin->setRange(0, INT_MAX);
    _playerPositionSpin->setReadOnly(true);
    _playerPositionSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Player default position:", _playerPositionSpin);
    
    _playerElevationSpin = new QSpinBox();
    _playerElevationSpin->setRange(0, 99);
    _playerElevationSpin->setReadOnly(true);
    _playerElevationSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Player default elevation:", _playerElevationSpin);
    
    _playerOrientationSpin = new QSpinBox();
    _playerOrientationSpin->setRange(0, 5);
    _playerOrientationSpin->setReadOnly(true);
    _playerOrientationSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Player default orientation:", _playerOrientationSpin);
    
    _globalVarsSpin = new QSpinBox();
    _globalVarsSpin->setRange(0, INT_MAX);
    _globalVarsSpin->setReadOnly(true);
    _globalVarsSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Global variables #:", _globalVarsSpin);
    
    _localVarsSpin = new QSpinBox();
    _localVarsSpin->setRange(0, INT_MAX);
    _localVarsSpin->setReadOnly(true);
    _localVarsSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Local variables #:", _localVarsSpin);
    
    _mapScriptEdit = new QLineEdit();
    _mapScriptEdit->setReadOnly(true);
    headerLayout->addRow("Map script:", _mapScriptEdit);
    
    _mapScriptIdSpin = new QSpinBox();
    _mapScriptIdSpin->setRange(0, INT_MAX);
    _mapScriptIdSpin->setReadOnly(true);
    _mapScriptIdSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Map script ID:", _mapScriptIdSpin);
    
    _darknessSpin = new QSpinBox();
    _darknessSpin->setRange(0, INT_MAX);
    _darknessSpin->setReadOnly(true);
    _darknessSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Darkness:", _darknessSpin);
    
    _mapIdSpin = new QSpinBox();
    _mapIdSpin->setRange(0, INT_MAX);
    _mapIdSpin->setReadOnly(true);
    _mapIdSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Map ID:", _mapIdSpin);
    
    _timestampSpin = new QSpinBox();
    _timestampSpin->setRange(0, INT_MAX);
    _timestampSpin->setReadOnly(true);
    _timestampSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    headerLayout->addRow("Timestamp:", _timestampSpin);
    
    _savegameCheck = new QCheckBox("Save game map");
    _savegameCheck->setEnabled(false);
    headerLayout->addRow(_savegameCheck);
    
    _contentLayout->addWidget(_mapHeaderGroup);
    
    // Global variables group
    _globalVarsGroup = new QGroupBox("Map Global Variables");
    QVBoxLayout* varsLayout = new QVBoxLayout(_globalVarsGroup);
    
    _globalVarsTree = new QTreeWidget();
    _globalVarsTree->setHeaderLabels({"Variable", "Value"});
    _globalVarsTree->header()->setStretchLastSection(true);
    _globalVarsTree->setAlternatingRowColors(true);
    _globalVarsTree->setRootIsDecorated(false);
    varsLayout->addWidget(_globalVarsTree);
    
    _contentLayout->addWidget(_globalVarsGroup);
    
    // Map scripts group (placeholder)
    _mapScriptsGroup = new QGroupBox("Map Scripts");
    QVBoxLayout* scriptsLayout = new QVBoxLayout(_mapScriptsGroup);
    
    _mapScriptsLabel = new QLabel("Script information will be displayed here");
    _mapScriptsLabel->setStyleSheet("color: gray; font-style: italic;");
    scriptsLayout->addWidget(_mapScriptsLabel);
    
    _contentLayout->addWidget(_mapScriptsGroup);
    
    _contentLayout->addStretch(); // Add stretch to push content to top
    
    _scrollArea->setWidget(_contentWidget);
    _mainLayout->addWidget(_scrollArea);
    
    // Initially clear the display
    clearMapInfo();
}

void Qt6MapInfoPanel::setMap(Map* map) {
    _map = map;
    if (_map) {
        updateMapInfo();
        spdlog::debug("Qt6MapInfoPanel: Map set and info updated");
    } else {
        clearMapInfo();
        spdlog::debug("Qt6MapInfoPanel: Map cleared");
    }
}

void Qt6MapInfoPanel::updateMapInfo() {
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
        _playerOrientationSpin->setValue(static_cast<int>(mapInfo.header.player_default_orientation));
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
        
    } catch (const std::exception& e) {
        spdlog::error("Error updating map info: {}", e.what());
        clearMapInfo();
    }
}

void Qt6MapInfoPanel::loadScriptVars() {
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
        
        GamReader gam_reader{};
        auto gam_file = ResourceManager::getInstance().loadResource(gam_filepath, gam_reader);
        
        if (gam_file) {
            // Load global variables
            for (int index = 0; index < _map->getMapFile().header.num_global_vars; index++) {
                _mvars.emplace(gam_file->mvarKey(index), gam_file->mvarValue(index));
            }
            
            // Load map script name
            int map_script_id = _map->getMapFile().header.script_id;
            if (map_script_id > 0) {
                LstReader lst_reader{};
                auto scripts = ResourceManager::getInstance().loadResource("scripts/scripts.lst", lst_reader);
                if (scripts) {
                    try {
                        _mapScriptName = scripts->at(map_script_id - 1); // script id starts at 1
                    } catch (const std::exception& e) {
                        spdlog::warn("Failed to get script name for ID {}: {}", map_script_id, e.what());
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("Error loading script vars: {}", e.what());
    }
}

void Qt6MapInfoPanel::clearMapInfo() {
    _filenameEdit->clear();
    _filenameEdit->setPlaceholderText("No map loaded");
    
    _elevationsSpin->setValue(0);
    _playerPositionSpin->setValue(0);
    _playerElevationSpin->setValue(0);
    _playerOrientationSpin->setValue(0);
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
}

} // namespace geck