#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <memory>

#include "../editor/Object.h"
#include "../format/map/Tile.h"
#include "../selection/SelectionState.h"

namespace geck {

class Map;

class SelectionPanel : public QWidget {
    Q_OBJECT

public:
    explicit SelectionPanel(QWidget* parent = nullptr);

    void setMap(Map* map);

public slots:
    void selectObject(std::shared_ptr<Object> selectedObject);
    void selectTile(int tileIndex, int elevation, bool isRoof);
    void clearSelection();
    void handleSelectionChanged(const selection::SelectionState& selection, int elevation);

private:
    void setupUI();
    void updateObjectInfo();
    void updateTileInfo();
    void clearObjectInfo();
    void clearTileInfo();
    void loadTilePreview(class Lst* tilesList, uint16_t tileId);
    void showObjectPanel();
    void showTilePanel();

    QVBoxLayout* _mainLayout;
    QScrollArea* _scrollArea;
    QWidget* _contentWidget;
    QVBoxLayout* _contentLayout;
    
    // Stacked widget to switch between object and tile panels
    QStackedWidget* _stackedWidget;
    
    // Object panel widgets
    QWidget* _objectPanelWidget;
    QGroupBox* _objectInfoGroup;
    QLabel* _objectSpriteLabel;
    QLineEdit* _objectNameEdit;
    QLineEdit* _objectTypeEdit;
    QSpinBox* _objectMessageIdSpin;
    QSpinBox* _objectPositionSpin;
    QSpinBox* _objectProtoPidSpin;
    
    // Tile panel widgets
    QWidget* _tilePanelWidget;
    QGroupBox* _tileInfoGroup;
    QLabel* _tilePreviewLabel;
    QSpinBox* _tileIndexSpin;
    QSpinBox* _elevationSpin;
    QSpinBox* _hexXSpin;
    QSpinBox* _hexYSpin;
    QSpinBox* _worldXSpin;
    QSpinBox* _worldYSpin;
    QLineEdit* _tileTypeEdit;
    QSpinBox* _tileIdSpin;
    QLineEdit* _tileNameEdit;
    
    // Current selection state
    std::optional<std::shared_ptr<Object>> _selectedObject;
    int _selectedTileIndex;
    int _selectedElevation;
    bool _isRoofSelected;
    bool _hasTileSelection;
    Map* _map;
};

} // namespace geck