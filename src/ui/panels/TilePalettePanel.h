#pragma once

#include "../common/BasePanel.h"
#include "../common/BasePaletteWidget.h"
#include <QSpinBox>
#include <QPushButton>
#include <vector>
#include <memory>

namespace geck {

class Map;
class Lst;

namespace selection {
    class SelectionManager;
}

/**
 * @brief Widget representing a single tile in the palette
 * Now inherits from BasePaletteWidget to eliminate duplication
 */
class TileWidget : public BasePaletteWidget {
    Q_OBJECT

public:
    explicit TileWidget(int tileIndex, const QPixmap& pixmap, QWidget* parent = nullptr);

    int getTileIndex() const { return getIndex(); }

signals:
    void tileClicked(int tileIndex);

public:
    static constexpr int TILE_SIZE = 64; // Display size for tiles
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
class TilePalettePanel : public BasePanel {
    Q_OBJECT

public:
    enum class PlacementMode {
        UNIFIED_PLACEMENT // Single click = single tile, drag = area fill (like selection mode)
        // Note: SINGLE_PLACEMENT and AREA_FILL merged into unified system
    };


    explicit TilePalettePanel(QWidget* parent = nullptr);
    ~TilePalettePanel() = default;

    // Initialization
    void loadTiles(const Lst* tileList);
    void setMap(Map* map) { _map = map; }
    void setSelectionManager(selection::SelectionManager* selectionManager) { _selectionManager = selectionManager; }

    // Tile selection
    int getSelectedTileIndex() const { return _selectedTileIndex; }
    bool hasSelectedTile() const { return _selectedTileIndex >= 0; }
    void deselectTile();

    // Placement modes 
    PlacementMode getPlacementMode() const { return _placementMode; }
    void setPlacementMode(PlacementMode mode);


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
    void onSearchTextChanged(const QString& text) override;
    void onPaginationPageChanged(int page);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI() override;
    void setupModeControls();
    void setupTileGrid();
    void setupFilterControls();
    void setupPaginationControls();
    void updatePaginationControls();
    void calculatePagination();

    void clearTileSelection();
    void updateTileDisplay();
    int calculateOptimalColumnsPerRow() const;

    // UI Components
    QVBoxLayout* _mainLayout = nullptr;

    // Placement mode info
    QGroupBox* _modeGroup = nullptr;
    QLabel* _placementModeLabel = nullptr;

    QGroupBox* _filterGroup = nullptr;
    QLineEdit* _searchLineEdit = nullptr;
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
    selection::SelectionManager* _selectionManager = nullptr;

    // State
    int _selectedTileIndex = -1;
    PlacementMode _placementMode = PlacementMode::UNIFIED_PLACEMENT;
    int _tilesPerRow = 8;
    int _previousColumnsPerRow = -1; // Cache to avoid unnecessary grid rebuilds
    int _filterStart = 0;
    int _filterEnd = -1;      // -1 means show all
    QString _searchText = ""; // Current search filter text

    // Pagination controls
    QGroupBox* _paginationGroup = nullptr;
    PaginationWidget* _paginationWidget = nullptr;

    // Pagination state
    int _currentPage = 0;
    int _totalPages = 0;
    int _totalFilteredTiles = 0;

    // Constants
    static constexpr int TILES_PER_PAGE = 200; // Tiles to load per page
    static constexpr int DEFAULT_TILES_PER_ROW = 8;
    static constexpr int MAX_TILES_PER_ROW = 20; // Reasonable maximum for very wide panels
};

} // namespace geck