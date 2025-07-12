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
#include <unordered_map>

namespace geck {

class Map;

/**
 * Qt6 widget for displaying properties from the current MAP file
 */
class Qt6MapInfoPanel : public QWidget {
    Q_OBJECT

public:
    explicit Qt6MapInfoPanel(QWidget* parent = nullptr);
    
    void setMap(Map* map);

private:
    void setupUI();
    void updateMapInfo();
    void loadScriptVars();
    void clearMapInfo();

    QVBoxLayout* _mainLayout;
    QScrollArea* _scrollArea;
    QWidget* _contentWidget;
    QVBoxLayout* _contentLayout;
    
    // Map header group
    QGroupBox* _mapHeaderGroup;
    QLineEdit* _filenameEdit;
    QSpinBox* _elevationsSpin;
    QSpinBox* _playerPositionSpin;
    QSpinBox* _playerElevationSpin;
    QSpinBox* _playerOrientationSpin;
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
    
    Map* _map;
    std::string _mapScriptName;
    std::unordered_map<std::string, uint32_t> _mvars;
};

} // namespace geck