#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QGroupBox>
#include <QScrollArea>
#include <memory>

#include "../format/map/Tile.h"

namespace geck {

class Map;

class SelectedTilePanel : public QWidget {
    Q_OBJECT

public:
    explicit SelectedTilePanel(QWidget* parent = nullptr);

    void setMap(Map* map);

public slots:
    void selectTile(int tileIndex, int elevation, bool isRoof);
    void clearSelection();

private:
    void setupUI();
    void updateTileInfo();
    void clearTileInfo();
    void loadTilePreview(class Lst* tilesList, uint16_t tileId);

    QVBoxLayout* _mainLayout;
    QScrollArea* _scrollArea;
    QWidget* _contentWidget;
    QVBoxLayout* _contentLayout;
    
    // Tile info widgets
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
    int _selectedTileIndex;
    int _selectedElevation;
    bool _isRoofSelected;
    bool _hasSelection;
    Map* _map;
};

} // namespace geck