#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <memory>

namespace geck {

struct MapObject;
class Object;
namespace resource {
class GameResources;
}

/**
 * @brief Dockable panel for viewing and managing object inventories
 *
 * Features:
 * - Real-time updates when objects are selected
 * - Item icons with amounts shown in the table
 * - Enhanced visual selection highlighting
 * - Drag & drop support for inventory management
 * - Auto-hide when no inventory objects are selected
 */
class InventoryPanel : public QWidget {
    Q_OBJECT

public:
    explicit InventoryPanel(resource::GameResources& resources, QWidget* parent = nullptr);
    ~InventoryPanel() = default;

    // Update panel when object selection changes
    void setCurrentObject(std::shared_ptr<Object> object);
    void clearInventory();

    // Panel state management
    bool hasValidInventory() const;

signals:
    void inventoryChanged(); // Emitted when inventory is modified

private slots:
    void onItemSelectionChanged();
    void onAddItemClicked();
    void onRemoveItemClicked();
    void onEditItemClicked();
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);

private:
    void setupUI();
    void populateInventoryTree();
    void updateItemPreview(QTreeWidgetItem* item);
    void updateStatusLabel();
    void clearPreview();

    QPixmap getItemIconWithQuantity(const MapObject& item) const;

    // Custom tree widget item for enhanced selection
    void highlightSelectedItem(QTreeWidgetItem* item);
    void clearItemHighlight();

    // UI Components
    QVBoxLayout* _mainLayout;
    QSplitter* _splitter;

    // Left panel - Inventory list
    QWidget* _leftPanel;
    QVBoxLayout* _leftLayout;
    QStackedWidget* _inventoryViewStack;
    QTreeWidget* _inventoryTree;
    QLabel* _emptyInventoryLabel;
    QLabel* _statusLabel;

    // Right panel - Item preview and actions
    QWidget* _rightPanel;
    QVBoxLayout* _rightLayout;
    QGroupBox* _previewGroup;
    QLabel* _previewLabel;
    QFormLayout* _previewFormLayout;
    QLabel* _previewNameLabel;
    QLabel* _previewTypeLabel;
    QLabel* _previewAmountLabel;
    QLabel* _previewPidLabel;

    // Action buttons
    QWidget* _buttonPanel;
    QVBoxLayout* _buttonLayout;
    QPushButton* _addButton;
    QPushButton* _removeButton;
    QPushButton* _editButton;

    // Data
    resource::GameResources& _resources;
    std::shared_ptr<Object> _object;
    std::shared_ptr<MapObject> _mapObject;
    QTreeWidgetItem* _currentHighlightedItem;

    // Tree widget columns
    enum InventoryColumns {
        COLUMN_ICON = 0,
        COLUMN_NAME = 1,
        COLUMN_TYPE = 2,
        COLUMN_AMOUNT = 3,
        COLUMN_PID = 4,
        COLUMN_COUNT
    };

    // Visual styling
    static const int ICON_SIZE;
};

} // namespace geck
