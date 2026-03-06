#include "InventoryViewerDialog.h"
#include "../../editor/Object.h"
#include "../../format/map/MapObject.h"
#include "../../util/ProHelper.h"
#include "../../util/ResourceManager.h"
#include "../../format/pro/Pro.h"
#include "../../format/msg/Msg.h"
#include "../theme/ThemeManager.h"
#include "../UIConstants.h"

#include <QApplication>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <spdlog/spdlog.h>

namespace geck {

InventoryViewerDialog::InventoryViewerDialog(std::shared_ptr<Object> object, QWidget* parent)
    : QDialog(parent)
    , _mainLayout(nullptr)
    , _splitter(nullptr)
    , _leftPanel(nullptr)
    , _leftLayout(nullptr)
    , _inventoryTree(nullptr)
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
    , _object(object)
    , _mapObject(nullptr) {

    spdlog::debug("InventoryViewerDialog: Constructor called");

    if (!_object) {
        spdlog::error("InventoryViewerDialog: Invalid object provided");
        reject();
        return;
    }

    _mapObject = _object->getMapObjectPtr();
    if (!_mapObject) {
        spdlog::error("InventoryViewerDialog: Object has no MapObject");
        QMessageBox::warning(this, "Error", "Selected object has no map data.");
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
    _mainLayout->setContentsMargins(8, 8, 8, 4); // Reduce bottom margin
    _mainLayout->setSpacing(ui::constants::SPACING_TIGHT);                  // Reduce spacing between splitter and buttons

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

    // Connect signals
    connect(_inventoryTree, &QTreeWidget::itemSelectionChanged,
        this, &InventoryViewerDialog::onItemSelectionChanged);
    connect(_inventoryTree, &QTreeWidget::itemDoubleClicked,
        this, &InventoryViewerDialog::onItemDoubleClicked);

    _leftLayout->addWidget(_inventoryTree);

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
    _editButton->setEnabled(false); // Future enhancement
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

    if (!_mapObject || _mapObject->objects_in_inventory == 0) {
        return;
    }

    for (size_t i = 0; i < _mapObject->inventory.size(); ++i) {
        const auto& item = _mapObject->inventory[i];
        if (!item)
            continue;

        QTreeWidgetItem* treeItem = new QTreeWidgetItem(_inventoryTree);

        // Store inventory index in item data
        treeItem->setData(COLUMN_NAME, Qt::UserRole, static_cast<int>(i));

        // Set item icon (placeholder for now)
        QPixmap icon = getItemIcon(item->pro_pid);
        if (!icon.isNull()) {
            treeItem->setIcon(COLUMN_ICON, QIcon(icon));
        }

        // Set item details
        treeItem->setText(COLUMN_NAME, getItemName(item->pro_pid));
        treeItem->setText(COLUMN_TYPE, getItemTypeName(item->pro_pid));
        treeItem->setText(COLUMN_AMOUNT, QString::number(item->amount));
        treeItem->setText(COLUMN_PID, QString("0x%1").arg(item->pro_pid, 8, 16, QChar('0')));

        _inventoryTree->addTopLevelItem(treeItem);
    }

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

    // Enable context buttons
    _removeButton->setEnabled(true); // Enable remove for selected item
    _editButton->setEnabled(false);  // Still disabled for future enhancement
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

    // Update preview sprite
    QPixmap sprite = getItemIcon(mapItem->pro_pid);
    if (!sprite.isNull()) {
        // Scale sprite to fit preview area while maintaining aspect ratio
        sprite = sprite.scaled(_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        _previewLabel->setPixmap(sprite);
    } else {
        _previewLabel->setText("No sprite");
    }

    // Update item details
    _previewNameLabel->setText(getItemName(mapItem->pro_pid));
    _previewTypeLabel->setText(getItemTypeName(mapItem->pro_pid));
    _previewAmountLabel->setText(QString::number(mapItem->amount));
    _previewPidLabel->setText(QString("0x%1").arg(mapItem->pro_pid, 8, 16, QChar('0')));
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

    QString statusText = QString("%1 item%2 total")
                             .arg(_mapObject->objects_in_inventory)
                             .arg(_mapObject->objects_in_inventory != 1 ? "s" : "");

    if (_mapObject->max_inventory_size > 0) {
        statusText += QString(" (max %1)").arg(_mapObject->max_inventory_size);
    }

    _statusLabel->setText(statusText);
}

QString InventoryViewerDialog::getItemName(uint32_t pid) const {
    try {
        // Extract object type from PID (upper 8 bits)
        uint32_t objectType = (pid & 0xFF000000) >> 24;
        Pro::OBJECT_TYPE proType = static_cast<Pro::OBJECT_TYPE>(objectType);

        // Get MSG file for the object type
        Msg* msgFile = ProHelper::msgFile(proType);
        if (!msgFile) {
            return QString("Unknown Item (%1)").arg(pid, 8, 16, QChar('0'));
        }

        // Extract object index from PID (lower 24 bits)
        uint32_t objectIndex = pid & 0x00FFFFFF;

        // Get item name from MSG file
        auto message = msgFile->message(objectIndex * 100); // Names are at index*100
        QString name = QString::fromStdString(message.text);
        if (!name.isEmpty()) {
            return name;
        }
    } catch (const std::exception& e) {
        spdlog::warn("InventoryViewerDialog::getItemName: Error getting name for PID {}: {}", pid, e.what());
    }

    return QString("Item %1").arg(pid, 8, 16, QChar('0'));
}

QString InventoryViewerDialog::getItemTypeName(uint32_t pid) const {
    uint32_t objectType = (pid & 0xFF000000) >> 24;

    switch (objectType) {
        case 0:
            return "Item";
        case 1:
            return "Critter";
        case 2:
            return "Scenery";
        case 3:
            return "Wall";
        case 4:
            return "Tile";
        case 5:
            return "Misc";
        default:
            return "Unknown";
    }
}

QPixmap InventoryViewerDialog::getItemIcon(uint32_t pid) const {
    try {
        // Try to load the PRO file to get the inventory FID
        std::string proPath = ProHelper::basePath(pid);
        auto& resourceManager = ResourceManager::getInstance();

        auto pro = resourceManager.loadResource<Pro>(proPath);
        if (!pro) {
            spdlog::debug("InventoryViewerDialog::getItemIcon: Could not load PRO file for PID {}", pid);
            return QPixmap();
        }

        std::string frmPath;

        // Try inventory FID first (preferred for inventory display)
        if (pro->commonItemData.inventoryFID > 0) {
            try {
                frmPath = resourceManager.FIDtoFrmName(pro->commonItemData.inventoryFID);
                spdlog::debug("InventoryViewerDialog::getItemIcon: Using inventory FID 0x{:08X} -> {}",
                    pro->commonItemData.inventoryFID, frmPath);
            } catch (const std::exception& e) {
                spdlog::debug("InventoryViewerDialog::getItemIcon: Inventory FID 0x{:08X} resolution failed for PID {}: {}",
                    pro->commonItemData.inventoryFID, pid, e.what());
                frmPath.clear();
            }
        }

        // Fallback to main FID if inventory FID is not available or failed
        if (frmPath.empty() && pro->header.FID > 0) {
            try {
                frmPath = resourceManager.FIDtoFrmName(pro->header.FID);
                spdlog::debug("InventoryViewerDialog::getItemIcon: Using main FID 0x{:08X} -> {} for PID {}",
                    pro->header.FID, frmPath, pid);
            } catch (const std::exception& e) {
                spdlog::debug("InventoryViewerDialog::getItemIcon: Main FID 0x{:08X} resolution failed for PID {}: {}",
                    pro->header.FID, pid, e.what());
                return QPixmap();
            }
        }

        if (frmPath.empty()) {
            spdlog::debug("InventoryViewerDialog::getItemIcon: No valid FID found for PID {}", pid);
            return QPixmap();
        }

        // Try to load the texture using the resolved path
        sf::Texture texture = resourceManager.texture(frmPath);

        // Convert SFML texture to QPixmap
        sf::Image image = texture.copyToImage();
        QImage qImage(image.getPixelsPtr(), image.getSize().x, image.getSize().y, QImage::Format_RGBA8888);
        QPixmap pixmap = QPixmap::fromImage(qImage);

        // Scale to appropriate size for tree widget (64x64 max, maintaining aspect ratio)
        if (!pixmap.isNull() && (pixmap.width() > 64 || pixmap.height() > 64)) {
            pixmap = pixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        spdlog::debug("InventoryViewerDialog::getItemIcon: Successfully loaded icon for PID {} from {}", pid, frmPath);
        return pixmap;

    } catch (const std::exception& e) {
        spdlog::debug("InventoryViewerDialog::getItemIcon: Could not load icon for PID {}: {}", pid, e.what());
    }

    return QPixmap();
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

    // Validate PID by trying to load the PRO file
    try {
        std::string proPath = ProHelper::basePath(pid);
        auto& resourceManager = ResourceManager::getInstance();
        auto pro = resourceManager.loadResource<Pro>(proPath);

        if (!pro) {
            QMessageBox::warning(this, "Invalid Item", QString("Item with PID 0x%1 not found in game data.").arg(pid, 8, 16, QChar('0')));
            return;
        }

        // Create new MapObject for the item
        auto newItem = std::make_unique<MapObject>();
        newItem->pro_pid = pid;
        newItem->frm_pid = 0;   // Use default FRM from PRO
        newItem->position = 0;  // Not used for inventory items
        newItem->elevation = 0; // Not used for inventory items
        newItem->direction = 0;
        newItem->amount = amount;
        newItem->objects_in_inventory = 0;
        newItem->max_inventory_size = 0;
        newItem->inventory.clear();

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
    QString itemName = getItemName(mapItem->pro_pid);
    int amount = mapItem->amount;

    // Confirm removal
    int result = QMessageBox::question(this, "Remove Item",
        QString("Remove %1 x %2 from inventory?").arg(amount).arg(itemName),
        QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) {
        return;
    }

    try {
        // Remove the item from inventory
        uint32_t removedPid = mapItem->pro_pid;
        _mapObject->inventory.erase(_mapObject->inventory.begin() + inventoryIndex);
        _mapObject->objects_in_inventory = static_cast<uint32_t>(_mapObject->inventory.size());

        // Refresh the display
        populateInventoryTree();
        updateStatusLabel();
        clearPreview();

        // Disable remove button since no item is selected now
        _removeButton->setEnabled(false);

        spdlog::info("InventoryViewerDialog: Removed item PID 0x{:08X} (amount {}) from inventory", removedPid, amount);

    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", QString("Failed to remove item: %1").arg(e.what()));
        spdlog::error("InventoryViewerDialog::onRemoveItemClicked: Error removing item: {}", e.what());
    }
}

void InventoryViewerDialog::onEditItemClicked() {
    // Future enhancement: Open PRO editor for selected item
    QMessageBox::information(this, "Not Implemented",
        "Edit item functionality will be implemented in a future version.");
}

void InventoryViewerDialog::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column)
    if (item) {
        onEditItemClicked(); // Double-click to edit (when implemented)
    }
}

} // namespace geck