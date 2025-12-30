#pragma once

#include "../common/GridPalettePanel.h"
#include "../common/BasePaletteWidget.h"
#include "../UIConstants.h"
#include <QTabWidget>
#include <QDrag>
#include <QMimeData>
#include <vector>
#include <memory>
#include <unordered_map>

namespace geck {

class Map;
class Lst;
class Pro;

// Forward declaration
class ObjectPalettePanel;

// Object category enum - moved outside class for easier access
enum class ObjectCategory {
    ITEMS,    // Weapons, armor, consumables, etc.
    SCENERY,  // Furniture, decorations, interactive objects
    CRITTERS, // NPCs, monsters, characters
    WALLS,    // Wall segments and structural elements
    MISC      // Miscellaneous objects
};

} // namespace geck

// Hash function for ObjectCategory to use in unordered_map
template<>
struct std::hash<geck::ObjectCategory> {
    std::size_t operator()(geck::ObjectCategory c) const noexcept {
        return static_cast<std::size_t>(c);
    }
};

namespace geck {

/**
 * @brief Information about a loaded object for the palette
 */
struct ObjectInfo {
    QString proFileName; // Original .pro filename from LST
    QString displayName; // Human-readable name for display
    const Pro* pro;      // Raw pointer to PRO file (managed by ResourceManager)
    QString frmPath;     // Path to FRM file for thumbnail
    int listIndex;       // Index in the category list

    ObjectInfo(const QString& fileName, int index)
        : proFileName(fileName)
        , pro(nullptr)
        , listIndex(index) { }
};

/**
 * @brief Widget representing a single object in the palette
 * Now inherits from BasePaletteWidget to eliminate duplication
 */
class ObjectWidget : public BasePaletteWidget {
    Q_OBJECT

public:
    explicit ObjectWidget(int objectIndex, const ObjectInfo* objectInfo, const QPixmap& pixmap, ObjectCategory category, QWidget* parent = nullptr);

    int getObjectIndex() const { return getIndex(); }
    const ObjectInfo* getObjectInfo() const { return _objectInfo; }
    ObjectCategory getCategory() const { return _category; }

signals:
    void objectClicked(int objectIndex);

protected:
    void mouseMoveEvent(QMouseEvent* event) override;

public:
    static constexpr int OBJECT_SIZE = 64; // Display size for objects

private:
    const ObjectInfo* _objectInfo;
    ObjectCategory _category;
};

/**
 * @brief Panel showing all available objects organized by category
 *
 * Features:
 * - Tabbed display by object type (Items, Scenery, Critters, Walls, Misc)
 * - Grid display of objects with Qt pixmaps
 * - Object selection for placement
 * - Search functionality within categories
 * - Single object placement mode
 */
class ObjectPalettePanel : public GridPalettePanel {
    Q_OBJECT

public:
    explicit ObjectPalettePanel(QWidget* parent = nullptr);
    ~ObjectPalettePanel();

    // Initialization
    void loadObjects();
    void setMap(Map* map) { _map = map; }

    // Object selection
    int getSelectedObjectIndex() const { return _selectedObjectIndex; }
    ObjectCategory getCurrentCategory() const { return _currentCategory; }
    bool hasSelectedObject() const { return _selectedObjectIndex >= 0; }

    // Access to object info for drag and drop
    const ObjectInfo* getObjectInfo(int objectIndex, ObjectCategory category) const;

signals:
    void objectSelected(int objectIndex, ObjectCategory category);
    void placeObjectRequested(int objectIndex, ObjectCategory category);

public slots:
    void onObjectClicked(int objectIndex);
    void onCategoryChanged(int tabIndex);
    void onSearchTextChanged(const QString& text) override;

private slots:
    void updateObjectGrid();
    void calculatePagination();

protected:
    void resizeEvent(QResizeEvent* event) override;

    // GridPalettePanel overrides
    int getDefaultColumnsPerRow() const override {
        return ui::constants::palette::DEFAULT_OBJECTS_PER_ROW;
    }
    void updateGrid() override { updateObjectGrid(); }

private:
    void setupUI() override;
    void setupCategoryTabs();
    void setupSearchControls();
    void setupObjectGrid();
    void setupPaginationControls();

    void loadCategoryObjects(ObjectCategory category);
    QPixmap createObjectThumbnail(const ObjectInfo* objectInfo, ObjectCategory category);
    QString getCategoryPath(ObjectCategory category) const;
    QString getCategoryDisplayName(ObjectCategory category) const;

    std::vector<std::unique_ptr<ObjectInfo>>& getObjectList(ObjectCategory category);
    const std::vector<std::unique_ptr<ObjectInfo>>& getObjectList(ObjectCategory category) const;

    void clearObjectSelection();

    // UI Components
    QVBoxLayout* _mainLayout = nullptr;

    // Category tabs
    QTabWidget* _categoryTabs = nullptr;

    // Search controls
    QGroupBox* _searchGroup = nullptr;

    // Note: _scrollArea, _gridWidget, _gridLayout, _paginationGroup, _paginationWidget,
    // _currentPage, _totalPages, _totalFilteredItems, _previousColumnsPerRow
    // are inherited from GridPalettePanel

    QLabel* _statusLabel = nullptr;

    // Data
    Map* _map = nullptr;
    std::vector<std::unique_ptr<ObjectWidget>> _objectWidgets;

    // State
    int _selectedObjectIndex = -1;
    ObjectCategory _currentCategory = ObjectCategory::ITEMS;
    QString _searchText = ""; // Current search filter text
    int _objectsPerRow = 6;

    // Object lists by category
    std::unordered_map<ObjectCategory, std::vector<std::unique_ptr<ObjectInfo>>> _objectsByCategory;

};

} // namespace geck