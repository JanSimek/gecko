#pragma once

#include <QDialog>
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
#include <memory>

namespace geck {

struct MapObject;
class Object;

/**
 * @brief Dialog for viewing and managing object inventories
 *
 * Displays inventory contents in a tree view with item details:
 * - Item icon, name, type, amount, and PID
 * - Preview panel showing selected item sprite
 * - Context menu for inventory management operations
 */
class InventoryViewerDialog : public QDialog {
    Q_OBJECT

public:
    explicit InventoryViewerDialog(std::shared_ptr<Object> object, QWidget* parent = nullptr);
    ~InventoryViewerDialog() = default;

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
    QHBoxLayout* _buttonLayout;
    QPushButton* _addButton;
    QPushButton* _removeButton;
    QPushButton* _editButton;
    QPushButton* _closeButton;

    // Data
    std::shared_ptr<Object> _object;
    std::shared_ptr<MapObject> _mapObject;

    // Tree widget columns
    enum InventoryColumns {
        COLUMN_ICON = 0,
        COLUMN_NAME = 1,
        COLUMN_TYPE = 2,
        COLUMN_AMOUNT = 3,
        COLUMN_PID = 4,
        COLUMN_COUNT
    };
};

} // namespace geck
