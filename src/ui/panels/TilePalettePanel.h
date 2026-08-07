#pragma once

#include "ui/common/BasePanel.h"
#include "ui/palette/PaletteModel.h"
#include "ui/theme/ThemeManager.h"

#include <QListView>
#include <QPushButton>
#include <QRadioButton>
#include <QSortFilterProxyModel>
#include <QSpinBox>

namespace geck {

class Map;
class Lst;
namespace resource {
    class GameResources;
}

namespace selection {
    class SelectionManager;
}

/**
 * @brief Panel showing every available tile, for selection and placement.
 *
 * A QListView over a PaletteModel: the view builds an item only for the rows it paints, so the
 * whole tile set is one scrollable list rather than pages, and a picked tile can always be
 * highlighted and scrolled to.
 */
class TilePalettePanel : public BasePanel {
    Q_OBJECT

public:
    enum class PlacementMode {
        UNIFIED_PLACEMENT // Single click = single tile, drag = area fill (like selection mode)
        // Note: SINGLE_PLACEMENT and AREA_FILL merged into unified system
    };

    explicit TilePalettePanel(resource::GameResources& resources, QWidget* parent = nullptr);
    ~TilePalettePanel() = default;

    // Initialization
    void loadTiles(const Lst* tileList);
    void setMap(Map* map) { _map = map; }
    void setSelectionManager(selection::SelectionManager* selectionManager) { _selectionManager = selectionManager; }

    // Tile selection
    int getSelectedTileIndex() const { return _selectedTileIndex; }
    bool hasSelectedTile() const { return _selectedTileIndex >= 0; }
    void deselectTile();
    // Eyedropper: select the given tile id on the given layer, clear any active filter so it is
    // visible, and emit tileSelected() so the editor arms tile painting with it ("load the brush").
    void pickTile(int tileId, bool isRoof);

    // Roof/Floor mode
    bool isRoofMode() const { return _isRoofMode; }
    void setRoofMode(bool isRoof);

    // Placement modes
    PlacementMode getPlacementMode() const { return _placementMode; }
    void setPlacementMode(PlacementMode mode);

signals:
    void tileSelected(int tileIndex, bool isRoof);
    void placementModeChanged(PlacementMode mode);
    void placeTileAtPosition(int tileIndex, int position, bool isRoof);
    void fillAreaWithTile(int tileIndex, const QRect& area, bool isRoof);
    void replaceSelectedTiles(int newTileIndex);

public slots:
    void onTileClicked(int tileIndex);
    void onPlacementModeChanged();

private slots:
    void filterTiles();
    void onSearchTextChanged(const QString& text) override;

public:
    static constexpr int TILE_SIZE = 64; // icon size in the list

private:
    void setupUI() override;
    void setupModeControls();
    void setupTileList();
    void setupFilterControls();

    /// Rebuild the model's items from the tile list and the numeric range filter.
    void rebuildItems();
    /// The tile's artwork, or a labelled placeholder when it will not load.
    [[nodiscard]] QPixmap tilePixmap(int tileIndex, const std::string& tileName) const;
    void updateStatusLabel();
    void selectRowForTile(int tileId);
    void clearTileSelection();

    // UI Components
    QVBoxLayout* _mainLayout = nullptr;

    // Placement mode info
    QGroupBox* _modeGroup = nullptr;
    QLabel* _placementModeLabel = nullptr;
    QRadioButton* _floorModeButton = nullptr;
    QRadioButton* _roofModeButton = nullptr;

    QGroupBox* _filterGroup = nullptr;
    QLineEdit* _searchLineEdit = nullptr;
    QSpinBox* _startTileSpinBox = nullptr;
    QSpinBox* _endTileSpinBox = nullptr;
    QPushButton* _showAllButton = nullptr;

    QListView* _tileView = nullptr;
    ui::PaletteModel* _model = nullptr;
    QSortFilterProxyModel* _filter = nullptr;

    QLabel* _statusLabel = nullptr;

    // Data
    resource::GameResources& _resources;
    Map* _map = nullptr;
    const Lst* _tileList = nullptr;
    selection::SelectionManager* _selectionManager = nullptr;

    // State
    int _selectedTileIndex = -1;
    bool _isRoofMode = false; // Default to floor mode
    PlacementMode _placementMode = PlacementMode::UNIFIED_PLACEMENT;
    int _filterStart = 0;
    int _filterEnd = -1; // -1 means show all
};

} // namespace geck
