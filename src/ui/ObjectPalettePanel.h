#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QButtonGroup>
#include <QLineEdit>
#include <QTabWidget>
#include <QSpinBox>
#include <QDrag>
#include <QMimeData>
#include <QResizeEvent>
#include <vector>
#include <memory>
#include "PaginationWidget.h"

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
 */
class ObjectWidget : public QLabel {
    Q_OBJECT

public:
    explicit ObjectWidget(int objectIndex, const ObjectInfo* objectInfo, const QPixmap& pixmap, ObjectCategory category, QWidget* parent = nullptr);

    int getObjectIndex() const { return _objectIndex; }
    const ObjectInfo* getObjectInfo() const { return _objectInfo; }
    ObjectCategory getCategory() const { return _category; }
    bool isSelected() const { return _selected; }
    void setSelected(bool selected);

signals:
    void objectClicked(int objectIndex);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

public:
    static constexpr int OBJECT_SIZE = 64; // Display size for objects

private:
    int _objectIndex;
    const ObjectInfo* _objectInfo;
    ObjectCategory _category;
    bool _selected = false;
    QPoint _dragStartPosition;
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
class ObjectPalettePanel : public QWidget {
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
    void onSearchTextChanged(const QString& text);

    // Pagination navigation
    void onPaginationPageChanged(int page);

private slots:
    void updateObjectGrid();
    void calculatePagination();
    void updatePaginationControls();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    void setupCategoryTabs();
    void setupSearchControls();
    void setupObjectGrid();
    void setupPaginationControls();

    void loadCategoryObjects(ObjectCategory category);
    QPixmap createObjectThumbnail(const ObjectInfo* objectInfo, ObjectCategory category);
    QPixmap createFrameThumbnail(const class Frame& frame, const class Pal* palette = nullptr);
    QString getCategoryPath(ObjectCategory category) const;
    QString getCategoryDisplayName(ObjectCategory category) const;

    void clearObjectSelection();
    int calculateOptimalColumnsPerRow() const;

    // UI Components
    QVBoxLayout* _mainLayout = nullptr;

    // Category tabs
    QTabWidget* _categoryTabs = nullptr;

    // Search controls
    QGroupBox* _searchGroup = nullptr;
    QLineEdit* _searchLineEdit = nullptr;

    // Pagination controls
    QGroupBox* _paginationGroup = nullptr;
    PaginationWidget* _paginationWidget = nullptr;

    // Object grid for current category
    QScrollArea* _scrollArea = nullptr;
    QWidget* _objectGridWidget = nullptr;
    QGridLayout* _objectGridLayout = nullptr;

    QLabel* _statusLabel = nullptr;

    // Data
    Map* _map = nullptr;
    std::vector<std::unique_ptr<ObjectWidget>> _objectWidgets;

    // State
    int _selectedObjectIndex = -1;
    ObjectCategory _currentCategory = ObjectCategory::ITEMS;
    QString _searchText = ""; // Current search filter text
    int _objectsPerRow = 6;
    int _previousColumnsPerRow = -1; // Cache to avoid unnecessary grid rebuilds

    // Pagination state
    int _currentPage = 0;
    int _totalPages = 0;
    int _totalFilteredObjects = 0;

    // Object lists for each category
    std::vector<std::unique_ptr<ObjectInfo>> _itemsList;
    std::vector<std::unique_ptr<ObjectInfo>> _sceneryList;
    std::vector<std::unique_ptr<ObjectInfo>> _crittersList;
    std::vector<std::unique_ptr<ObjectInfo>> _wallsList;
    std::vector<std::unique_ptr<ObjectInfo>> _miscList;

    // Constants
    static constexpr int OBJECTS_PER_PAGE = 200; // Objects to load per page
    static constexpr int DEFAULT_OBJECTS_PER_ROW = 6;
    static constexpr int MAX_OBJECTS_PER_ROW = 20; // Reasonable maximum for very wide panels
};

} // namespace geck