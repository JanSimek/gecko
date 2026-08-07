#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QComboBox>
#include <QPushButton>
#include "format/gam/Gam.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace geck {

class Map;
class Settings;
namespace resource {
    class GameResources;
    class MapNameResolver;
}

/// @brief Qt6 widget for displaying properties from the current MAP file.
class MapInfoPanel : public QWidget {
    Q_OBJECT

public:
    explicit MapInfoPanel(resource::GameResources& resources, std::shared_ptr<Settings> settings, QWidget* parent = nullptr);
    ~MapInfoPanel() override; // out-of-line for the unique_ptr<MapNameResolver> member

    void setMap(Map* map);
    void setPlayerPosition(int hexPosition, int elevation);

    /// Refresh the "Map Edges" group from the editor's current edge state (driven by MainWindow, which
    /// owns the current elevation and the selected zone). `version` is 0 when the map has no `.edg`,
    /// else 1 or 2; `clip` holds the four v2 clip flags (left,top,right,bottom) for the current
    /// elevation. Programmatic; does not emit the edge-edit signals.
    void setMapEdgeState(bool hasMap, int version, int zoneCount, bool hasSelectedZone,
        const std::array<bool, 4>& clip);

    /// Write any pending Map name / Lookup name edits to the writable copy of map.msg / maps.txt.
    /// Called when the map is saved (the panel itself only marks the map modified on edit). A no-op
    /// when nothing was edited; surfaces a warning (and writes nothing) if maps.txt would be invalid.
    void persistMapNames();

    /// Write any pending global-variable value edits to the writable copy of the map's `.gam`
    /// (MAP_GLOBAL_VARS). Called when the map is saved (the panel itself only marks the map modified on
    /// edit). A no-op when nothing was edited; surfaces a warning if there's no writable Data Path.
    void persistMapVars();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void playerPositionChanged(int position);
    void playerElevationChanged(int elevation);
    void playerOrientationChanged(int orientation);
    void selectPlayerPositionRequested();
    void centerViewOnPlayerPositionRequested();
    void mapScriptIdChanged(int scriptId);
    /// "Edit Source..." on the map-script row: open the SSL source of the script at this
    /// 0-based scripts.lst program index (the header's 1-based script_id minus one).
    void mapScriptSourceEditRequested(int programIndex);
    void darknessChanged(int darkness);
    void timestampChanged(int timestamp);
    void mapNamesChanged();     // a Map name / Lookup name field was edited -> mark the map modified
    void mapVariablesChanged(); // a global variable value was edited -> mark the map modified
    void elevationAdded(int elevation);
    void elevationRemoved(int elevation);
    /// Bulk map operations, routed to the editor's ObjectCommandController so they
    /// are undoable. The confirmation dialog lives here in the panel.
    void clearElevationRequested(int elevation);
    void copyElevationRequested(int fromElevation, int toElevation);
    void addSpatialScriptRequested();
    /// Map-edge (.edg) editing, routed to EditorWidget's EdgeEditService (undoable). They act on the
    /// current elevation / selected zone, which the editor owns. `side` is 0=left,1=top,2=right,3=bottom.
    void addEdgeZoneRequested();
    void deleteEdgeZoneRequested();
    void upgradeEdgeVersion2Requested();
    void edgeClipToggled(int side);
    void resetEdgeSquareRequested();

private slots:
    void onFieldChanged();
    void onOrientationChanged(int index);
    void onSelectPositionClicked();
    void onCenterViewClicked();
    void onElevationCheckboxChanged();
    void onClearElevationClicked();
    void onCopyElevationClicked();
    void onAddSpatialScriptClicked();
    void onAddEdgeZoneClicked();
    void onDeleteEdgeZoneClicked();
    void onUpgradeEdgeClicked();
    void onResetEdgeSquareClicked();
    void onGlobalVarChanged(QTreeWidgetItem* item, int column);
    void onAddGlobalVar();
    void onRemoveGlobalVar();
    void onGlobalVarSelectionChanged();

private:
    void setupUI();
    void updateMapInfo();
    void loadScriptVars();
    // (Re)build the global-variables tree from _mvars; guarded by _suppressVarEdit so the setText calls
    // don't fire onGlobalVarChanged. Shared by the initial map load and the add/remove handlers.
    void populateGlobalVars();
    void updateGlobalVarButtons(); // enable Remove only for a real variable row; gate Add on a loaded .gam
    void clearMapInfo();
    void updateMapNameDisplay();
    void updateOverlayHint(); // show/hide the "extracted to <path>" hint based on the writable copy
    // Write the gathered name edits to `writableRoot` (a writable Data Path) via resource::saveMapNames,
    // then re-mount + rebuild the resolver + refresh. Split out of persistMapNames for the complexity budget.
    void writeNameEdits(const std::filesystem::path& writableRoot, int index, int elevation,
        const std::optional<std::string>& lookup, const std::optional<std::string>& display);
    void updateElevationCheckboxStates();
    void setElevationCheckboxesBlocked(bool blocked);

    QVBoxLayout* _mainLayout;
    QScrollArea* _scrollArea;
    QWidget* _contentWidget;
    QVBoxLayout* _contentLayout;

    // Map header group
    QGroupBox* _mapHeaderGroup;
    QLineEdit* _filenameEdit;
    QLineEdit* _displayNameEdit; // editable map.msg display name (per current elevation)
    QLineEdit* _lookupNameEdit;  // editable maps.txt lookup_name
    QLabel* _overlayHintLabel;   // shown once the names are extracted to the writable copy; states the path
    QCheckBox* _elevation1Check;
    QCheckBox* _elevation2Check;
    QCheckBox* _elevation3Check;
    QSpinBox* _playerPositionSpin;
    QPushButton* _setPositionButton;
    QPushButton* _centerViewButton;
    QSpinBox* _playerElevationSpin;
    QComboBox* _playerOrientationCombo;
    QSpinBox* _globalVarsSpin;
    QSpinBox* _localVarsSpin;
    QLineEdit* _mapScriptEdit;
    QPushButton* _editMapScriptButton = nullptr;
    QSpinBox* _mapScriptIdSpin;
    QSpinBox* _darknessSpin;
    QSpinBox* _mapIdSpin;
    QSpinBox* _timestampSpin;
    QCheckBox* _savegameCheck;

    // Global variables group
    QGroupBox* _globalVarsGroup;
    QTreeWidget* _globalVarsTree;
    QLineEdit* _newGlobalVarNameEdit; // name for a variable to add (appended as the last map global)
    QPushButton* _addGlobalVarButton;
    QPushButton* _removeGlobalVarButton;
    QLabel* _globalVarsSummaryLabel; // "Total: N variables from <map>.gam", shown under the tree

    // Map operations group (clear / copy elevation)
    QGroupBox* _mapOperationsGroup;
    QComboBox* _clearElevationCombo;
    QComboBox* _copyFromCombo;
    QComboBox* _copyToCombo;

    // Map edges group (.edg zones + v2 clip). State is pushed in by MainWindow via setMapEdgeState;
    // _suppressEdgeEdit guards the programmatic checkbox updates from firing edgeClipToggled.
    QGroupBox* _mapEdgesGroup = nullptr;
    QLabel* _edgeZoneCountLabel = nullptr;
    QPushButton* _addEdgeZoneButton = nullptr;
    QPushButton* _deleteEdgeZoneButton = nullptr;
    QPushButton* _upgradeEdgeButton = nullptr;
    QPushButton* _resetEdgeSquareButton = nullptr;
    std::array<QCheckBox*, 4> _edgeClipChecks{}; // 0=left,1=top,2=right,3=bottom
    bool _suppressEdgeEdit = false;

    resource::GameResources& _resources;
    std::shared_ptr<Settings> _settings;                  // for the writable data root used when editing names
    std::unique_ptr<resource::MapNameResolver> _mapNames; // built lazily; reads maps.txt/map.msg once
    Map* _map;
    std::string _mapScriptName;
    // The global-variable {name, value} rows the tree is built from, taken straight from the map's .gam
    // MAP_GLOBAL_VARS in file order: the i-th entry is the i-th MAP_GLOBAL_VARS variable. (For a BASE map
    // the engine re-reads these from the .gam, ignoring the .map's blocks, so the .gam is the source of
    // truth and where edits are written back.)
    std::vector<std::pair<std::string, int>> _mvars;

    // The map's `.gam` parsed losslessly, so a global-variable value can be edited and the file written
    // back byte-for-byte except that one value. Loaded alongside the names (nullopt when the map has no
    // .gam); `_gamPath` is the VFS path it came from so persistMapVars() can write the same relative path.
    std::optional<Gam> _gamDoc;
    std::string _gamPath;
    bool _globalVarsEdited = false; // a MAP_GLOBAL_VARS value was edited -> persistMapVars() writes the .gam

    // True while updateMapInfo() populates the widgets from the map, so their change signals don't write
    // a half-updated widget set back over the map (see onFieldChanged).
    bool _suppressFieldChanged = false;

    // True while the global-variables tree is (re)populated, so the setText calls during populate don't
    // fire onGlobalVarChanged as if the user had edited a value (mirror of _suppressFieldChanged).
    bool _suppressVarEdit = false;
};

} // namespace geck
