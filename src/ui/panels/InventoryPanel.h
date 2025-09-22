#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QPainter>
#include <memory>

namespace geck {

struct MapObject;
class Object;

/**
 * @brief Dockable panel for viewing and managing object inventories
 *
 * Features:
 * - Real-time updates when objects are selected
 * - Item icons with quantity overlays
 * - Enhanced visual selection highlighting
 * - Drag & drop support for inventory management
 * - Auto-hide when no inventory objects are selected
 */
class InventoryPanel : public QWidget {
    Q_OBJECT

public:
    explicit InventoryPanel(QWidget* parent = nullptr);
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

    // Enhanced icon loading with quantity overlays
    QPixmap getItemIconWithQuantity(uint32_t pid, int amount) const;
    QPixmap addQuantityOverlay(const QPixmap& baseIcon, int amount) const;

    // Helper methods for item management
    QString getItemName(uint32_t pid) const;
    QString getItemTypeName(uint32_t pid) const;
    QPixmap getItemIcon(uint32_t pid) const;

    // Custom tree widget item for enhanced selection
    void highlightSelectedItem(QTreeWidgetItem* item);
    void clearItemHighlight();

    // UI Components
    QVBoxLayout* _mainLayout;
    QSplitter* _splitter;

    // Left panel - Inventory list
    QWidget* _leftPanel;
    QVBoxLayout* _leftLayout;
    QTreeWidget* _inventoryTree;
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
    static const QColor HIGHLIGHT_COLOR;
    static const int ICON_SIZE;
    static const int MAX_QUANTITY_DISPLAY;
};

} // namespace geck