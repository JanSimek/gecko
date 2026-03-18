#include "InventoryViewerDialog.h"
#include "../../editor/Object.h"
#include "../../format/map/MapObject.h"
#include "../common/InventoryItemUiHelper.h"
#include "../theme/ThemeManager.h"
#include "../UIConstants.h"

#include <QApplication>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <spdlog/spdlog.h>

namespace geck {

InventoryViewerDialog::InventoryViewerDialog(resource::GameResources& resources, std::shared_ptr<Object> object, QWidget* parent)
    : InventoryViewerDialog(resources, object ? object->getMapObjectPtr() : nullptr, parent) {
}

InventoryViewerDialog::InventoryViewerDialog(resource::GameResources& resources, std::shared_ptr<MapObject> mapObject, QWidget* parent)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _splitter(nullptr)
    , _leftPanel(nullptr)
    , _leftLayout(nullptr)
    , _inventoryViewStack(nullptr)
    , _inventoryTree(nullptr)
    , _emptyInventoryLabel(nullptr)
    , _statusLabel(nullptr)
    , _rightPanel(nullptr)
    , _rightLayout(nullptr)
    , _previewGroup(nullptr)
    , _previewLabel(nullptr)
    , _previewFormLayout(nullptr)
    , _previewNameLabel(nullptr)
    , _previewTypeLabel(nullptr)
    , _previewAmountLabel(nullptr)
    , _previewPidLabel(nullptr)
    , _buttonPanel(nullptr)
    , _buttonLayout(nullptr)
    , _addButton(nullptr)
    , _removeButton(nullptr)
    , _editButton(nullptr)
    , _closeButton(nullptr)
    , _resources(resources)
    , _mapObject(std::move(mapObject)) {

    spdlog::debug("InventoryViewerDialog: Constructor called");

    if (!_mapObject) {
        spdlog::error("InventoryViewerDialog: Invalid map object provided");
        reject();
        return;
    }

    spdlog::debug("InventoryViewerDialog: Setting up UI for object with {} inventory items",
        _mapObject->objects_in_inventory);

    setupUI();
    populateInventoryTree();
    updateStatusLabel();
}

void InventoryViewerDialog::setupUI() {
    setWindowTitle("Inventory Viewer");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
    resize(ui::constants::dialog_sizes::LARGE_WIDTH, ui::constants::dialog_sizes::LARGE_HEIGHT);

    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(8, 8, 8, 4);           // Reduce bottom margin
    _mainLayout->setSpacing(ui::constants::SPACING_TIGHT); // Reduce spacing between splitter and buttons

    // Create splitter for left/right panels
    _splitter = new QSplitter(Qt::Horizontal);

    // === LEFT PANEL: Inventory Tree ===
    _leftPanel = new QWidget();
    _leftLayout = new QVBoxLayout(_leftPanel);
    _leftLayout->setContentsMargins(0, 0, 0, 0);
    _leftLayout->setSpacing(ui::constants::SPACING_TIGHT);

    // Inventory tree widget
    _inventoryTree = new QTreeWidget();
    _inventoryTree->setColumnCount(COLUMN_COUNT);

    QStringList headers = { "Icon", "Name", "Type", "Amount", "PID" };
    _inventoryTree->setHeaderLabels(headers);

    // Configure columns
    _inventoryTree->setColumnWidth(COLUMN_ICON, ui::constants::column_widths::ICON);
    _inventoryTree->setColumnWidth(COLUMN_NAME, ui::constants::column_widths::NAME_WIDE);
    _inventoryTree->setColumnWidth(COLUMN_TYPE, ui::constants::column_widths::TYPE_WIDE);
    _inventoryTree->setColumnWidth(COLUMN_AMOUNT, ui::constants::column_widths::AMOUNT_WIDE);
    _inventoryTree->setColumnWidth(COLUMN_PID, ui::constants::column_widths::PID);

    // Set uniform row height to accommodate larger icons
    _inventoryTree->setIconSize(QSize(ui::constants::sizes::ICON_SIZE_LARGE, ui::constants::sizes::ICON_SIZE_LARGE));
    _inventoryTree->header()->setDefaultSectionSize(70); // Minimum height for rows

    _inventoryTree->setSortingEnabled(false);
    _inventoryTree->setAlternatingRowColors(true);
    _inventoryTree->setSelectionMode(QAbstractItemView::SingleSelection);
    _inventoryTree->setSelectionBehavior(QAbstractItemView::SelectRows);

    _emptyInventoryLabel = new QLabel("No inventory items");
    _emptyInventoryLabel->setAlignment(Qt::AlignCenter);
    _emptyInventoryLabel->setStyleSheet(ui::theme::styles::smallLabel());

    _inventoryViewStack = new QStackedWidget();
    _inventoryViewStack->addWidget(_inventoryTree);
    _inventoryViewStack->addWidget(_emptyInventoryLabel);

    // Connect signals
    connect(_inventoryTree, &QTreeWidget::itemSelectionChanged,
        this, &InventoryViewerDialog::onItemSelectionChanged);
    connect(_inventoryTree, &QTreeWidget::itemDoubleClicked,
        this, &InventoryViewerDialog::onItemDoubleClicked);

    _leftLayout->addWidget(_inventoryViewStack);

    // Status label
    _statusLabel = new QLabel("0 items total");
    _statusLabel->setStyleSheet(ui::theme::styles::smallLabel());
    _leftLayout->addWidget(_statusLabel);

    // === RIGHT PANEL: Preview and Actions ===
    _rightPanel = new QWidget();
    _rightLayout = new QVBoxLayout(_rightPanel);
    _rightLayout->setContentsMargins(0, 0, 0, 0);
    _rightLayout->setSpacing(ui::constants::SPACING_NORMAL);

    // Preview group
    _previewGroup = new QGroupBox("Item Preview");
    _previewFormLayout = new QFormLayout(_previewGroup);
    _previewFormLayout->setContentsMargins(ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN_VERTICAL, ui::constants::GROUP_MARGIN, ui::constants::GROUP_MARGIN);
    _previewFormLayout->setSpacing(ui::constants::SPACING_FORM);

    // Item sprite preview
    _previewLabel = new QLabel("No item selected");
    _previewLabel->setAlignment(Qt::AlignCenter);
    _previewLabel->setMinimumSize(ui::constants::sizes::PREVIEW_TILE, ui::constants::sizes::PREVIEW_TILE);
    _previewLabel->setMaximumSize(ui::constants::sizes::PREVIEW_TILE, ui::constants::sizes::PREVIEW_TILE);
    _previewLabel->setScaledContents(false);
    _previewLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _previewLabel->setStyleSheet(ui::theme::styles::previewArea());
    _previewFormLayout->addRow("Sprite:", _previewLabel);

    // Item details
    _previewNameLabel = new QLabel("—");
    _previewFormLayout->addRow("Name:", _previewNameLabel);

    _previewTypeLabel = new QLabel("—");
    _previewFormLayout->addRow("Type:", _previewTypeLabel);

    _previewAmountLabel = new QLabel("—");
    _previewFormLayout->addRow("Amount:", _previewAmountLabel);

    _previewPidLabel = new QLabel("—");
    _previewFormLayout->addRow("PID:", _previewPidLabel);

    _rightLayout->addWidget(_previewGroup);
    _rightLayout->addStretch(); // Keep preview at top of right panel

    // Add panels to splitter
    _splitter->addWidget(_leftPanel);
    _splitter->addWidget(_rightPanel);
    _splitter->setSizes({ 500, 300 }); // 70/30 split
    _mainLayout->addWidget(_splitter);

    // === BUTTON PANEL ===
    _buttonPanel = new QWidget();
    _buttonLayout = new QHBoxLayout(_buttonPanel);
    _buttonLayout->setContentsMargins(0, 4, 0, 0); // Minimal margins - small top margin only
    _buttonLayout->setSpacing(ui::constants::SPACING_NORMAL);

    // Action buttons
    _addButton = new QPushButton("Add Item...");
    _addButton->setEnabled(true);
    _addButton->setToolTip("Add new item to inventory");
    connect(_addButton, &QPushButton::clicked, this, &InventoryViewerDialog::onAddItemClicked);
    _buttonLayout->addWidget(_addButton);

    _removeButton = new QPushButton("Remove Item");
    _removeButton->setEnabled(false); // Disabled until item is selected
    _removeButton->setToolTip("Remove selected item from inventory");
    connect(_removeButton, &QPushButton::clicked, this, &InventoryViewerDialog::onRemoveItemClicked);
    _buttonLayout->addWidget(_removeButton);

    _editButton = new QPushButton("Edit Properties...");
    _editButton->setEnabled(false);
    _editButton->setToolTip("Edit selected item properties (Coming Soon)");
    connect(_editButton, &QPushButton::clicked, this, &InventoryViewerDialog::onEditItemClicked);
    _buttonLayout->addWidget(_editButton);

    _buttonLayout->addStretch();

    _closeButton = new QPushButton("Close");
    connect(_closeButton, &QPushButton::clicked, [this]() {
        spdlog::debug("InventoryViewerDialog: Close button clicked");
        accept();
    });
    _buttonLayout->addWidget(_closeButton);

    _mainLayout->addWidget(_buttonPanel);
}

void InventoryViewerDialog::populateInventoryTree() {
    _inventoryTree->clear();
    _removeButton->setEnabled(false);
    _editButton->setEnabled(false);

    if (!_mapObject || _mapObject->inventory.empty()) {
        _inventoryViewStack->setCurrentWidget(_emptyInventoryLabel);
        clearPreview();
        return;
    }

    for (size_t i = 0; i < _mapObject->inventory.size(); ++i) {
        const auto& item = _mapObject->inventory[i];
        if (!item)
            continue;

        QTreeWidgetItem* treeItem = new QTreeWidgetItem(_inventoryTree);
        const auto details = ui::inventory::describeItem(_resources, item->pro_pid);
        const uint32_t displayAmount = ui::inventory::displayAmount(_resources, *item);

        // Store inventory index in item data
        treeItem->setData(COLUMN_NAME, Qt::UserRole, static_cast<int>(i));

        QPixmap icon = ui::inventory::loadItemIcon(_resources, item->pro_pid, ui::constants::sizes::ICON_SIZE_LARGE);
        if (!icon.isNull()) {
            treeItem->setIcon(COLUMN_ICON, QIcon(icon));
        }

        // Set item details
        treeItem->setText(COLUMN_NAME, details.name);
        treeItem->setText(COLUMN_TYPE, details.typeName);
        treeItem->setText(COLUMN_AMOUNT, QString::number(displayAmount));
        treeItem->setText(COLUMN_PID, details.pidText);

        _inventoryTree->addTopLevelItem(treeItem);
    }

    if (_inventoryTree->topLevelItemCount() == 0) {
        _inventoryViewStack->setCurrentWidget(_emptyInventoryLabel);
        clearPreview();
        return;
    }

    _inventoryViewStack->setCurrentWidget(_inventoryTree);

    // Adjust column widths to content
    for (int i = 0; i < COLUMN_COUNT; ++i) {
        _inventoryTree->resizeColumnToContents(i);
    }
}

void InventoryViewerDialog::onItemSelectionChanged() {
    QList<QTreeWidgetItem*> selected = _inventoryTree->selectedItems();

    if (selected.isEmpty()) {
        clearPreview();
        _removeButton->setEnabled(false);
        _editButton->setEnabled(false);
        return;
    }

    QTreeWidgetItem* item = selected.first();
    updateItemPreview(item);

    _removeButton->setEnabled(true);
    _editButton->setEnabled(false);
}

void InventoryViewerDialog::updateItemPreview(QTreeWidgetItem* item) {
    if (!item || !_mapObject) {
        clearPreview();
        return;
    }

    int inventoryIndex = item->data(COLUMN_NAME, Qt::UserRole).toInt();
    if (inventoryIndex < 0 || inventoryIndex >= static_cast<int>(_mapObject->inventory.size())) {
        clearPreview();
        return;
    }

    const auto& mapItem = _mapObject->inventory[inventoryIndex];
    if (!mapItem) {
        clearPreview();
        return;
    }

    const auto details = ui::inventory::describeItem(_resources, mapItem->pro_pid);
    const uint32_t displayAmount = ui::inventory::displayAmount(_resources, *mapItem);

    // Update preview sprite
    QPixmap sprite = ui::inventory::loadItemIcon(_resources, mapItem->pro_pid);
    if (!sprite.isNull()) {
        // Scale sprite to fit preview area while maintaining aspect ratio
        sprite = sprite.scaled(_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        _previewLabel->setPixmap(sprite);
    } else {
        _previewLabel->setText("No sprite");
    }

    // Update item details
    _previewNameLabel->setText(details.name);
    _previewTypeLabel->setText(details.typeName);
    _previewAmountLabel->setText(QString::number(displayAmount));
    _previewPidLabel->setText(details.pidText);
}

void InventoryViewerDialog::clearPreview() {
    _previewLabel->setText("No item selected");
    _previewLabel->setPixmap(QPixmap());
    _previewNameLabel->setText("—");
    _previewTypeLabel->setText("—");
    _previewAmountLabel->setText("—");
    _previewPidLabel->setText("—");
}

void InventoryViewerDialog::updateStatusLabel() {
    if (!_mapObject) {
        _statusLabel->setText("No inventory data");
        return;
    }

    if (_mapObject->inventory.empty()) {
        _statusLabel->setText("No inventory items");
        return;
    }

    QString statusText = QString("%1 item%2 total")
                             .arg(_mapObject->objects_in_inventory)
                             .arg(_mapObject->objects_in_inventory != 1 ? "s" : "");

    if (_mapObject->max_inventory_size > 0) {
        statusText += QString(" (max %1)").arg(_mapObject->max_inventory_size);
    }

    _statusLabel->setText(statusText);
}

void InventoryViewerDialog::onAddItemClicked() {
    bool ok;
    QString pidText = QInputDialog::getText(this, "Add Item", "Enter item PID (hex or decimal):", QLineEdit::Normal, "", &ok);

    if (!ok || pidText.isEmpty()) {
        return;
    }

    // Parse PID (support both hex and decimal)
    uint32_t pid = 0;
    if (pidText.startsWith("0x") || pidText.startsWith("0X")) {
        pid = pidText.mid(2).toUInt(&ok, 16);
    } else {
        pid = pidText.toUInt(&ok, 10);
        if (!ok) {
            // Try hex if decimal fails
            pid = pidText.toUInt(&ok, 16);
        }
    }

    if (!ok || pid == 0) {
        QMessageBox::warning(this, "Invalid PID", "Please enter a valid PID in decimal or hex format (e.g., 16777216 or 0x01000000)");
        return;
    }

    // Get amount
    int amount = QInputDialog::getInt(this, "Add Item", "Enter amount:", 1, 1, 99999, 1, &ok);
    if (!ok) {
        return;
    }

    if (!ui::inventory::itemExists(_resources, pid)) {
        QMessageBox::warning(this, "Invalid Item", QString("Item with PID 0x%1 not found in game data.").arg(pid, 8, 16, QChar('0')));
        return;
    }

    try {
        auto newItem = ui::inventory::createMapInventoryItem(_resources, pid, amount);
        // Add to the container's inventory
        _mapObject->inventory.push_back(std::move(newItem));
        _mapObject->objects_in_inventory = static_cast<uint32_t>(_mapObject->inventory.size());

        // Refresh the display
        populateInventoryTree();
        updateStatusLabel();

        spdlog::info("InventoryViewerDialog: Added item PID 0x{:08X} with amount {} to inventory", pid, amount);

    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", QString("Failed to add item: %1").arg(e.what()));
        spdlog::error("InventoryViewerDialog::onAddItemClicked: Error adding item PID {}: {}", pid, e.what());
    }
}

void InventoryViewerDialog::onRemoveItemClicked() {
    QList<QTreeWidgetItem*> selected = _inventoryTree->selectedItems();

    if (selected.isEmpty()) {
        QMessageBox::information(this, "No Selection", "Please select an item to remove.");
        return;
    }

    QTreeWidgetItem* item = selected.first();
    int inventoryIndex = item->data(COLUMN_NAME, Qt::UserRole).toInt();

    if (inventoryIndex < 0 || inventoryIndex >= static_cast<int>(_mapObject->inventory.size())) {
        QMessageBox::warning(this, "Error", "Invalid inventory item selected.");
        return;
    }

    const auto& mapItem = _mapObject->inventory[inventoryIndex];
    const auto details = ui::inventory::describeItem(_resources, mapItem->pro_pid);
    const uint32_t displayAmount = ui::inventory::displayAmount(_resources, *mapItem);

    int result = QMessageBox::question(this, "Remove Item",
        QString("Remove %1 x %2 from inventory?").arg(displayAmount).arg(details.name),
        QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) {
        return;
    }

    try {
        uint32_t removedPid = mapItem->pro_pid;
        _mapObject->inventory.erase(_mapObject->inventory.begin() + inventoryIndex);
        _mapObject->objects_in_inventory = static_cast<uint32_t>(_mapObject->inventory.size());

        populateInventoryTree();
        updateStatusLabel();
        clearPreview();

        _removeButton->setEnabled(false);

        spdlog::info("InventoryViewerDialog: Removed item PID 0x{:08X} (display amount {}) from inventory", removedPid, displayAmount);

    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", QString("Failed to remove item: %1").arg(e.what()));
        spdlog::error("InventoryViewerDialog::onRemoveItemClicked: Error removing item: {}", e.what());
    }
}

void InventoryViewerDialog::onEditItemClicked() {
    QMessageBox::information(this, "Not Implemented",
        "Edit item functionality will be implemented in a future version.");
}

void InventoryViewerDialog::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column)
    if (item) {
        onEditItemClicked();
    }
}

} // namespace geck
