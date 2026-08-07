#pragma once

#include "ui/common/BasePanel.h"
#include "ui/palette/PaletteModel.h"
#include "ui/theme/ThemeManager.h"
#include "ui/palette/PaletteView.h"
#include <QSortFilterProxyModel>
#include <QTabWidget>
#include <QDrag>
#include <QMimeData>
#include <vector>
#include <memory>
#include <optional>
#include <utility>
#include <unordered_map>

namespace geck {

class Map;
class Lst;
class Pro;
namespace resource {
    class GameResources;
}

class ObjectPalettePanel;

// Defined outside the class so it can be used as an unordered_map key.
enum class ObjectCategory {
    ITEMS,    // Weapons, armor, consumables, etc.
    SCENERY,  // Furniture, decorations, interactive objects
    CRITTERS, // NPCs, monsters, characters
    WALLS,    // Wall segments and structural elements
    MISC      // Miscellaneous objects
};

} // namespace geck

// Hash function for ObjectCategory to use in unordered_map
template <>
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
    const Pro* pro;      // Raw pointer to PRO file (managed by ResourceRepository cache)
    QString frmPath;     // Path to FRM file for thumbnail
    int listIndex;       // Index in the category list

    ObjectInfo(const QString& fileName, int index)
        : proFileName(fileName)
        , pro(nullptr)
        , listIndex(index) { }
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
class ObjectPalettePanel : public BasePanel {
    Q_OBJECT

public:
    explicit ObjectPalettePanel(resource::GameResources& resources, QWidget* parent = nullptr);
    ~ObjectPalettePanel();

    // Initialization
    void loadObjects();
    void setMap(Map* map) { _map = map; }

    // Object selection
    int getSelectedObjectIndex() const { return _selectedObjectIndex; }
    ObjectCategory getCurrentCategory() const { return _currentCategory; }
    bool hasSelectedObject() const { return _selectedObjectIndex >= 0; }

    // Eyedropper: switch to the proto's category tab and filter to it so it is revealed and selected.
    // Returns the proto's {list index, category} — the caller uses it to arm click-to-place — or
    // std::nullopt if the PID's type is not shown here (e.g. a tile) or has no palette entry.
    std::optional<std::pair<int, ObjectCategory>> revealProto(uint32_t pid);

    // Access to object info for drag and drop
    const ObjectInfo* getObjectInfo(int objectIndex, ObjectCategory category) const;

signals:
    void objectSelected(int objectIndex, ObjectCategory category);
    void placeObjectRequested(int objectIndex, ObjectCategory category);

public slots:
    void onObjectClicked(int objectIndex);
    void onCategoryChanged(int tabIndex);
    void onSearchTextChanged(const QString& text) override;

public:
    static constexpr int OBJECT_SIZE = 64; // icon size in the grid

private:
    void setupUI() override;
    void setupCategoryTabs();
    void setupSearchControls();
    void setupObjectView();

    /// The engine's own name for a proto, or the .pro file name when it has none.
    [[nodiscard]] QString protoDisplayName(const Pro& pro, const QString& proFileName) const;
    /// Feed the model the current category's objects.
    void rebuildItems();
    void updateStatusLabel();
    void selectRowForObject(int objectIndex);

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

    ui::PaletteView* _objectView = nullptr;
    ui::PaletteModel* _model = nullptr;
    QSortFilterProxyModel* _filter = nullptr;

    QLabel* _statusLabel = nullptr;

    // Data
    resource::GameResources& _resources;
    Map* _map = nullptr;

    // State
    int _selectedObjectIndex = -1;
    ObjectCategory _currentCategory = ObjectCategory::ITEMS;

    // Object lists by category
    std::unordered_map<ObjectCategory, std::vector<std::unique_ptr<ObjectInfo>>> _objectsByCategory;
};

} // namespace geck
