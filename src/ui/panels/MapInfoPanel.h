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
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

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

    /// Write any pending Map name / Lookup name edits to the writable copy of map.msg / maps.txt.
    /// Called when the map is saved (the panel itself only marks the map modified on edit). A no-op
    /// when nothing was edited; surfaces a warning (and writes nothing) if maps.txt would be invalid.
    void persistMapNames();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void playerPositionChanged(int position);
    void playerElevationChanged(int elevation);
    void playerOrientationChanged(int orientation);
    void selectPlayerPositionRequested();
    void centerViewOnPlayerPositionRequested();
    void mapScriptIdChanged(int scriptId);
    void darknessChanged(int darkness);
    void timestampChanged(int timestamp);
    void mapNamesChanged(); // a Map name / Lookup name field was edited -> mark the map modified
    void elevationAdded(int elevation);
    void elevationRemoved(int elevation);
    /// Bulk map operations, routed to the editor's ObjectCommandController so they
    /// are undoable. The confirmation dialog lives here in the panel.
    void clearElevationRequested(int elevation);
    void copyElevationRequested(int fromElevation, int toElevation);
    void addSpatialScriptRequested(int programIndex, int tile, int elevation, int radius);

private slots:
    void onFieldChanged();
    void onOrientationChanged(int index);
    void onSelectPositionClicked();
    void onCenterViewClicked();
    void onElevationCheckboxChanged();
    void onClearElevationClicked();
    void onCopyElevationClicked();
    void onAddSpatialScriptClicked();

private:
    void setupUI();
    void updateMapInfo();
    void loadScriptVars();
    void clearMapInfo();
    void updateMapScriptsDisplay();
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
    QSpinBox* _mapScriptIdSpin;
    QSpinBox* _darknessSpin;
    QSpinBox* _mapIdSpin;
    QSpinBox* _timestampSpin;
    QCheckBox* _savegameCheck;

    // Global variables group
    QGroupBox* _globalVarsGroup;
    QTreeWidget* _globalVarsTree;

    // Map scripts group: concise counts-only summary; the full list lives in the Scripts panel.
    QGroupBox* _mapScriptsGroup;
    QLabel* _mapScriptsLabel;

    // Map operations group (clear / copy elevation)
    QGroupBox* _mapOperationsGroup;
    QComboBox* _clearElevationCombo;
    QComboBox* _copyFromCombo;
    QComboBox* _copyToCombo;

    resource::GameResources& _resources;
    std::shared_ptr<Settings> _settings;                  // for the writable data root used when editing names
    std::unique_ptr<resource::MapNameResolver> _mapNames; // built lazily; reads maps.txt/map.msg once
    Map* _map;
    std::string _mapScriptName;
    std::unordered_map<std::string, uint32_t> _mvars;

    // True while updateMapInfo() populates the widgets from the map, so their change signals don't write
    // a half-updated widget set back over the map (see onFieldChanged).
    bool _suppressFieldChanged = false;
};

} // namespace geck
