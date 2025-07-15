#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QGroupBox>
#include <QButtonGroup>
#include <vector>
#include <memory>

namespace geck {

class Map;
class Lst;

/**
 * @brief Widget representing a single tile in the palette
 */
class TileWidget : public QLabel {
    Q_OBJECT

public:
    explicit TileWidget(int tileIndex, const QPixmap& pixmap, QWidget* parent = nullptr);
    
    int getTileIndex() const { return _tileIndex; }
    bool isSelected() const { return _selected; }
    void setSelected(bool selected);

signals:
    void tileClicked(int tileIndex);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

public:
    static constexpr int TILE_SIZE = 64; // Display size for tiles

private:
    int _tileIndex;
    bool _selected = false;
};

/**
 * @brief Panel showing all available tiles in a grid layout
 * 
 * Features:
 * - Grid display of all tiles with Qt pixmaps
 * - Tile selection for placement/replacement
 * - Single tile placement mode
 * - Area fill mode with selection box
 * - Replace selected tiles mode
 */
class TilePalettePanel : public QWidget {
    Q_OBJECT

public:
    enum class PlacementMode {
        SINGLE_PLACEMENT,  // Click to place one tile
        AREA_FILL,         // Draw selection box to fill area
        REPLACE_SELECTED   // Replace currently selected tiles
    };

    explicit TilePalettePanel(QWidget* parent = nullptr);
    ~TilePalettePanel() = default;

    // Initialization
    void loadTiles(const Lst* tileList);
    void setMap(Map* map) { _map = map; }

    // Tile selection
    int getSelectedTileIndex() const { return _selectedTileIndex; }
    bool hasSelectedTile() const { return _selectedTileIndex >= 0; }
    
    // Placement modes
    PlacementMode getPlacementMode() const { return _placementMode; }
    void setPlacementMode(PlacementMode mode);
    
    // Note: Target selection removed - tiles are replaced based on what's actually selected

signals:
    void tileSelected(int tileIndex);
    void placementModeChanged(PlacementMode mode);
    void placeTileAtPosition(int tileIndex, int position, bool isRoof);
    void fillAreaWithTile(int tileIndex, const QRect& area, bool isRoof);
    void replaceSelectedTiles(int newTileIndex);

public slots:
    void onTileClicked(int tileIndex);
    void onPlacementModeChanged();

private slots:
    void updateTileGrid();
    void filterTiles();

private:
    void setupUI();
    void setupModeControls();
    void setupTileGrid();
    void setupFilterControls();
    
    void clearTileSelection();
    void updateTileDisplay();
    
    // UI Components
    QVBoxLayout* _mainLayout = nullptr;
    QGroupBox* _modeGroup = nullptr;
    QButtonGroup* _modeButtonGroup = nullptr;
    QPushButton* _singlePlacementButton = nullptr;
    QPushButton* _areaFillButton = nullptr;
    QPushButton* _replaceSelectedButton = nullptr;
    
    QGroupBox* _filterGroup = nullptr;
    QSpinBox* _startTileSpinBox = nullptr;
    QSpinBox* _endTileSpinBox = nullptr;
    QPushButton* _showAllButton = nullptr;
    
    // Target controls removed - tiles are replaced based on what's actually selected
    
    QScrollArea* _scrollArea = nullptr;
    QWidget* _tileGridWidget = nullptr;
    QGridLayout* _tileGridLayout = nullptr;
    
    QLabel* _statusLabel = nullptr;
    
    // Data
    Map* _map = nullptr;
    const Lst* _tileList = nullptr;
    std::vector<std::unique_ptr<TileWidget>> _tileWidgets;
    
    // State
    int _selectedTileIndex = -1;
    PlacementMode _placementMode = PlacementMode::SINGLE_PLACEMENT;
    int _tilesPerRow = 8;
    int _filterStart = 0;
    int _filterEnd = -1; // -1 means show all
    
    // Constants
    static constexpr int MAX_TILES_TO_LOAD = 1000; // Prevent UI slowdown
    static constexpr int DEFAULT_TILES_PER_ROW = 8;
};

} // namespace geck