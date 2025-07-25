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
#include <QLineEdit>
#include <vector>
#include <memory>
#include "PaginationWidget.h"

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
        UNIFIED_PLACEMENT // Single click = single tile, drag = area fill (like selection mode)
        // Note: SINGLE_PLACEMENT and AREA_FILL merged into unified system
        // Note: REPLACE_SELECTED removed - automatic replacement when tiles are selected
    };

    enum class InteractionMode {
        SELECTION,    // Normal selection mode (default editor behavior)
        TILE_PAINTING // Tile painting mode (place tiles from palette)
    };

    explicit TilePalettePanel(QWidget* parent = nullptr);
    ~TilePalettePanel() = default;

    // Initialization
    void loadTiles(const Lst* tileList);
    void setMap(Map* map) { _map = map; }

    // Tile selection
    int getSelectedTileIndex() const { return _selectedTileIndex; }
    bool hasSelectedTile() const { return _selectedTileIndex >= 0; }

    // Interaction modes
    InteractionMode getInteractionMode() const { return _interactionMode; }
    void setInteractionMode(InteractionMode mode);

    // Placement modes (only relevant in TILE_PAINTING interaction mode)
    PlacementMode getPlacementMode() const { return _placementMode; }
    void setPlacementMode(PlacementMode mode);

    // Note: Target selection removed - tiles are replaced based on what's actually selected

signals:
    void tileSelected(int tileIndex);
    void interactionModeChanged(InteractionMode mode);
    void placementModeChanged(PlacementMode mode);
    void placeTileAtPosition(int tileIndex, int position, bool isRoof);
    void fillAreaWithTile(int tileIndex, const QRect& area, bool isRoof);
    void replaceSelectedTiles(int newTileIndex);

public slots:
    void onTileClicked(int tileIndex);
    void onInteractionModeChanged();
    void onPlacementModeChanged();

private slots:
    void updateTileGrid();
    void filterTiles();
    void onSearchTextChanged(const QString& text);
    void onPaginationPageChanged(int page);

private:
    void setupUI();
    void setupInteractionModeControls();
    void setupModeControls();
    void setupTileGrid();
    void setupFilterControls();
    void setupPaginationControls();
    void updatePaginationControls();
    void calculatePagination();

    void clearTileSelection();
    void updateTileDisplay();

    // UI Components
    QVBoxLayout* _mainLayout = nullptr;

    // Interaction mode controls
    QGroupBox* _interactionGroup = nullptr;
    QButtonGroup* _interactionButtonGroup = nullptr;
    QPushButton* _selectionModeButton = nullptr;
    QPushButton* _tilePaintingModeButton = nullptr;

    // Placement mode info (shown only in tile painting mode)
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

    // State
    int _selectedTileIndex = -1;
    InteractionMode _interactionMode = InteractionMode::SELECTION;
    PlacementMode _placementMode = PlacementMode::UNIFIED_PLACEMENT;
    int _tilesPerRow = 8;
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
};

} // namespace geck