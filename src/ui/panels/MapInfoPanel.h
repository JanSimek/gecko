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
#include <memory>
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
    void onSaveNamesClicked();

private:
    void setupUI();
    void updateMapInfo();
    void loadScriptVars();
    void clearMapInfo();
    void updateMapScriptsDisplay();
    void updateMapNameDisplay();
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
    QPushButton* _saveNamesButton;
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

    // Map scripts group (placeholder)
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
};

} // namespace geck
