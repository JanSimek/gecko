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
#include <unordered_map>

namespace geck {

class Map;
namespace resource {
    class GameResources;
}

/// @brief Qt6 widget for displaying properties from the current MAP file.
class MapInfoPanel : public QWidget {
    Q_OBJECT

public:
    explicit MapInfoPanel(resource::GameResources& resources, QWidget* parent = nullptr);

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
    /// Emitted after a bulk map operation (clear/copy elevation) mutates the
    /// model, so the editor can reload sprites for the affected elevation.
    void mapContentChanged(int elevation);

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
    void updateElevationCheckboxStates();
    void setElevationCheckboxesBlocked(bool blocked);

    QVBoxLayout* _mainLayout;
    QScrollArea* _scrollArea;
    QWidget* _contentWidget;
    QVBoxLayout* _contentLayout;

    // Map header group
    QGroupBox* _mapHeaderGroup;
    QLineEdit* _filenameEdit;
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
    Map* _map;
    std::string _mapScriptName;
    std::unordered_map<std::string, uint32_t> _mvars;
};

} // namespace geck
