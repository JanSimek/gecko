#include "InventoryPanel.h"
#include "../../editor/Object.h"
#include "../../format/map/MapObject.h"
#include "../../util/ProHelper.h"
#include "../../util/ResourceManager.h"
#include "../../format/pro/Pro.h"
#include "../../format/msg/Msg.h"

#include <QApplication>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QPainter>
#include <spdlog/spdlog.h>

namespace geck {

// Static constants
const QColor InventoryPanel::HIGHLIGHT_COLOR = QColor(0, 255, 0, 100); // Semi-transparent green
const int InventoryPanel::ICON_SIZE = 64;
const int InventoryPanel::MAX_QUANTITY_DISPLAY = 99;

InventoryPanel::InventoryPanel(QWidget* parent)
    : QWidget(parent)
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
    , _object(nullptr)
    , _mapObject(nullptr)
    , _currentHighlightedItem(nullptr) {

    spdlog::debug("InventoryPanel: Constructor called");
    setupUI();
}

void InventoryPanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(4, 4, 4, 4);
    _mainLayout->setSpacing(4);

    // Create splitter for left/right panels
    _splitter = new QSplitter(Qt::Horizontal);

    // === LEFT PANEL: Inventory Tree ===
    _leftPanel = new QWidget();
    _leftLayout = new QVBoxLayout(_leftPanel);
    _leftLayout->setContentsMargins(0, 0, 0, 0);
    _leftLayout->setSpacing(4);

    // Inventory tree widget
    _inventoryTree = new QTreeWidget();
    _inventoryTree->setColumnCount(COLUMN_COUNT);

    QStringList headers = {"Icon", "Name", "Type", "Amount", "PID"};
    _inventoryTree->setHeaderLabels(headers);

    // Configure columns
    _inventoryTree->setColumnWidth(COLUMN_ICON, 80);
    _inventoryTree->setColumnWidth(COLUMN_NAME, 180);
    _inventoryTree->setColumnWidth(COLUMN_TYPE, 100);
    _inventoryTree->setColumnWidth(COLUMN_AMOUNT, 60);
    _inventoryTree->setColumnWidth(COLUMN_PID, 80);

    // Set icon size and row height
    _inventoryTree->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
    _inventoryTree->header()->setDefaultSectionSize(ICON_SIZE + 8);

    _inventoryTree->setSortingEnabled(false);
    _inventoryTree->setAlternatingRowColors(true);
    _inventoryTree->setSelectionMode(QAbstractItemView::SingleSelection);
    _inventoryTree->setSelectionBehavior(QAbstractItemView::SelectRows);

    // Connect signals
    connect(_inventoryTree, &QTreeWidget::itemSelectionChanged,
            this, &InventoryPanel::onItemSelectionChanged);
    connect(_inventoryTree, &QTreeWidget::itemDoubleClicked,
            this, &InventoryPanel::onItemDoubleClicked);

    _leftLayout->addWidget(_inventoryTree);

    // Status label
    _statusLabel = new QLabel("No inventory object selected");
    _statusLabel->setStyleSheet("color: #666; font-size: 11px; padding: 2px;");
    _leftLayout->addWidget(_statusLabel);

    // === RIGHT PANEL: Preview and Actions ===
    _rightPanel = new QWidget();
    _rightLayout = new QVBoxLayout(_rightPanel);
    _rightLayout->setContentsMargins(0, 0, 0, 0);
    _rightLayout->setSpacing(8);

    // Preview group
    _previewGroup = new QGroupBox("Item Preview");
    _previewFormLayout = new QFormLayout(_previewGroup);
    _previewFormLayout->setContentsMargins(8, 12, 8, 8);
    _previewFormLayout->setSpacing(6);

    // Item sprite preview
    _previewLabel = new QLabel("No item selected");
    _previewLabel->setAlignment(Qt::AlignCenter);
    _previewLabel->setMinimumSize(120, 120);
    _previewLabel->setMaximumSize(120, 120);
    _previewLabel->setScaledContents(false);
    _previewLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _previewLabel->setStyleSheet("border: 1px solid gray; background-color: #f0f0f0;");
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

    // === ACTION BUTTONS ===
    _buttonPanel = new QWidget();
    _buttonLayout = new QVBoxLayout(_buttonPanel);
    _buttonLayout->setContentsMargins(0, 0, 0, 0);
    _buttonLayout->setSpacing(4);

    _addButton = new QPushButton("Add Item...");
    _addButton->setEnabled(false); // Disabled until inventory object is selected
    _addButton->setToolTip("Add new item to inventory");
    connect(_addButton, &QPushButton::clicked, this, &InventoryPanel::onAddItemClicked);
    _buttonLayout->addWidget(_addButton);

    _removeButton = new QPushButton("Remove Item");
    _removeButton->setEnabled(false); // Disabled until item is selected
    _removeButton->setToolTip("Remove selected item from inventory");
    connect(_removeButton, &QPushButton::clicked, this, &InventoryPanel::onRemoveItemClicked);
    _buttonLayout->addWidget(_removeButton);

    _editButton = new QPushButton("Edit Properties...");
    _editButton->setEnabled(false); // Future enhancement
    _editButton->setToolTip("Edit selected item properties (Coming Soon)");
    connect(_editButton, &QPushButton::clicked, this, &InventoryPanel::onEditItemClicked);
    _buttonLayout->addWidget(_editButton);

    _buttonLayout->addStretch(); // Push buttons to top

    _rightLayout->addWidget(_buttonPanel);
    _rightLayout->addStretch(); // Keep preview and buttons at top

    // Add panels to splitter
    _splitter->addWidget(_leftPanel);
    _splitter->addWidget(_rightPanel);
    _splitter->setSizes({300, 200}); // 60/40 split
    _mainLayout->addWidget(_splitter);

    // Initial state - no object selected
    clearInventory();
}

void InventoryPanel::setCurrentObject(std::shared_ptr<Object> object) {
    spdlog::debug("InventoryPanel::setCurrentObject called");

    _object = object;
    _mapObject = nullptr;

    if (!_object) {
        clearInventory();
        return;
    }

    _mapObject = _object->getMapObjectPtr();
    if (!_mapObject) {
        spdlog::debug("InventoryPanel: Object has no MapObject");
        clearInventory();
        return;
    }

    // Check if object can have inventory
    if (_mapObject->objects_in_inventory == 0 && _mapObject->inventory.empty()) {
        // Object exists but has no inventory capability
        clearInventory();
        return;
    }

    spdlog::debug("InventoryPanel: Setting up for object with {} inventory items",
                 _mapObject->objects_in_inventory);

    // Enable inventory management
    _addButton->setEnabled(true);

    populateInventoryTree();
    updateStatusLabel();
}

void InventoryPanel::clearInventory() {
    _inventoryTree->clear();
    clearPreview();
    _statusLabel->setText("No inventory object selected");

    // Disable all buttons
    _addButton->setEnabled(false);
    _removeButton->setEnabled(false);
    _editButton->setEnabled(false);

    _object = nullptr;
    _mapObject = nullptr;
    _currentHighlightedItem = nullptr;
}

bool InventoryPanel::hasValidInventory() const {
    return _mapObject && (_mapObject->objects_in_inventory > 0 || !_mapObject->inventory.empty());
}

void InventoryPanel::populateInventoryTree() {
    _inventoryTree->clear();
    clearItemHighlight();

    if (!_mapObject || _mapObject->objects_in_inventory == 0) {
        return;
    }

    for (size_t i = 0; i < _mapObject->inventory.size(); ++i) {
        const auto& item = _mapObject->inventory[i];
        if (!item) continue;

        QTreeWidgetItem* treeItem = new QTreeWidgetItem(_inventoryTree);

        // Store inventory index in item data
        treeItem->setData(COLUMN_NAME, Qt::UserRole, static_cast<int>(i));

        // Set enhanced icon with quantity overlay
        QPixmap iconWithQuantity = getItemIconWithQuantity(item->pro_pid, item->amount);
        if (!iconWithQuantity.isNull()) {
            treeItem->setIcon(COLUMN_ICON, QIcon(iconWithQuantity));
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

QPixmap InventoryPanel::getItemIconWithQuantity(uint32_t pid, int amount) const {
    QPixmap baseIcon = getItemIcon(pid);
    if (baseIcon.isNull()) {
        return QPixmap();
    }

    // Add quantity overlay if amount > 1
    if (amount > 1) {
        return addQuantityOverlay(baseIcon, amount);
    }

    return baseIcon;
}

QPixmap InventoryPanel::addQuantityOverlay(const QPixmap& baseIcon, int amount) const {
    if (baseIcon.isNull() || amount <= 1) {
        return baseIcon;
    }

    QPixmap result = baseIcon;
    QPainter painter(&result);

    // Prepare quantity text
    QString quantityText = (amount <= MAX_QUANTITY_DISPLAY) ?
                          QString("x%1").arg(amount) :
                          QString("x%1+").arg(MAX_QUANTITY_DISPLAY);

    // Set up font and pen - make it bold and visible
    QFont font = painter.font();
    font.setPointSize(10);
    font.setBold(true);
    painter.setFont(font);

    // Draw text with outline for better visibility
    QPen outlinePen(Qt::black, 2);
    QPen textPen(Qt::green);

    // Calculate text position (bottom-left corner with padding)
    int x = 4;
    int y = result.height() - 4;

    // Draw text outline
    painter.setPen(outlinePen);
    painter.drawText(x-1, y-1, quantityText);
    painter.drawText(x+1, y-1, quantityText);
    painter.drawText(x-1, y+1, quantityText);
    painter.drawText(x+1, y+1, quantityText);

    // Draw main text
    painter.setPen(textPen);
    painter.drawText(x, y, quantityText);

    return result;
}

void InventoryPanel::onItemSelectionChanged() {
    QList<QTreeWidgetItem*> selected = _inventoryTree->selectedItems();

    clearItemHighlight();

    if (selected.isEmpty()) {
        clearPreview();
        _removeButton->setEnabled(false);
        _editButton->setEnabled(false);
        return;
    }

    QTreeWidgetItem* item = selected.first();
    updateItemPreview(item);
    highlightSelectedItem(item);

    // Enable context buttons
    _removeButton->setEnabled(true);  // Enable remove for selected item
    _editButton->setEnabled(false);   // Still disabled for future enhancement
}

void InventoryPanel::highlightSelectedItem(QTreeWidgetItem* item) {
    clearItemHighlight();
    _currentHighlightedItem = item;

    // Set custom background color for enhanced visual feedback
    if (item) {
        for (int col = 0; col < COLUMN_COUNT; ++col) {
            item->setBackground(col, QBrush(HIGHLIGHT_COLOR));
        }
    }
}

void InventoryPanel::clearItemHighlight() {
    if (_currentHighlightedItem) {
        for (int col = 0; col < COLUMN_COUNT; ++col) {
            _currentHighlightedItem->setBackground(col, QBrush()); // Clear background
        }
        _currentHighlightedItem = nullptr;
    }
}

void InventoryPanel::updateItemPreview(QTreeWidgetItem* item) {
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

void InventoryPanel::clearPreview() {
    _previewLabel->setText("No item selected");
    _previewLabel->setPixmap(QPixmap());
    _previewNameLabel->setText("—");
    _previewTypeLabel->setText("—");
    _previewAmountLabel->setText("—");
    _previewPidLabel->setText("—");
}

void InventoryPanel::updateStatusLabel() {
    if (!_mapObject) {
        _statusLabel->setText("No inventory data");
        return;
    }

    QString statusText = QString("%1 item%2")
        .arg(_mapObject->objects_in_inventory)
        .arg(_mapObject->objects_in_inventory != 1 ? "s" : "");

    if (_mapObject->max_inventory_size > 0) {
        statusText += QString(" (max %1)").arg(_mapObject->max_inventory_size);
    }

    _statusLabel->setText(statusText);
}

// Helper methods - reuse logic from InventoryViewerDialog
QString InventoryPanel::getItemName(uint32_t pid) const {
    try {
        uint32_t objectType = (pid & 0xFF000000) >> 24;
        Pro::OBJECT_TYPE proType = static_cast<Pro::OBJECT_TYPE>(objectType);

        Msg* msgFile = ProHelper::msgFile(proType);
        if (!msgFile) {
            return QString("Unknown Item (%1)").arg(pid, 8, 16, QChar('0'));
        }

        uint32_t objectIndex = pid & 0x00FFFFFF;
        auto message = msgFile->message(objectIndex * 100);
        QString name = QString::fromStdString(message.text);
        if (!name.isEmpty()) {
            return name;
        }
    } catch (const std::exception& e) {
        spdlog::warn("InventoryPanel::getItemName: Error getting name for PID {}: {}", pid, e.what());
    }

    return QString("Item %1").arg(pid, 8, 16, QChar('0'));
}

QString InventoryPanel::getItemTypeName(uint32_t pid) const {
    uint32_t objectType = (pid & 0xFF000000) >> 24;

    switch (objectType) {
        case 0: return "Item";
        case 1: return "Critter";
        case 2: return "Scenery";
        case 3: return "Wall";
        case 4: return "Tile";
        case 5: return "Misc";
        default: return "Unknown";
    }
}

QPixmap InventoryPanel::getItemIcon(uint32_t pid) const {
    try {
        std::string proPath = ProHelper::basePath(pid);
        auto& resourceManager = ResourceManager::getInstance();

        auto pro = resourceManager.loadResource<Pro>(proPath);
        if (!pro) {
            return QPixmap();
        }

        std::string frmPath;

        // Try inventory FID first, fallback to main FID
        if (pro->commonItemData.inventoryFID > 0) {
            try {
                frmPath = resourceManager.FIDtoFrmName(pro->commonItemData.inventoryFID);
            } catch (const std::exception&) {
                frmPath.clear();
            }
        }

        if (frmPath.empty() && pro->header.FID > 0) {
            try {
                frmPath = resourceManager.FIDtoFrmName(pro->header.FID);
            } catch (const std::exception&) {
                return QPixmap();
            }
        }

        if (frmPath.empty()) {
            return QPixmap();
        }

        // Load texture and convert to QPixmap
        sf::Texture texture = resourceManager.texture(frmPath);
        sf::Image image = texture.copyToImage();
        QImage qImage(image.getPixelsPtr(), image.getSize().x, image.getSize().y, QImage::Format_RGBA8888);
        QPixmap pixmap = QPixmap::fromImage(qImage);

        // Scale to icon size
        if (!pixmap.isNull() && (pixmap.width() > ICON_SIZE || pixmap.height() > ICON_SIZE)) {
            pixmap = pixmap.scaled(ICON_SIZE, ICON_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        return pixmap;

    } catch (const std::exception& e) {
        spdlog::debug("InventoryPanel::getItemIcon: Could not load icon for PID {}: {}", pid, e.what());
    }

    return QPixmap();
}

void InventoryPanel::onAddItemClicked() {
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
        newItem->frm_pid = 0;
        newItem->position = 0;
        newItem->elevation = 0;
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

        // Emit signal for other components to update
        emit inventoryChanged();

        spdlog::info("InventoryPanel: Added item PID 0x{:08X} with amount {} to inventory", pid, amount);

    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", QString("Failed to add item: %1").arg(e.what()));
        spdlog::error("InventoryPanel::onAddItemClicked: Error adding item PID {}: {}", pid, e.what());
    }
}

void InventoryPanel::onRemoveItemClicked() {
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

        // Emit signal for other components to update
        emit inventoryChanged();

        spdlog::info("InventoryPanel: Removed item PID 0x{:08X} (amount {}) from inventory", removedPid, amount);

    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", QString("Failed to remove item: %1").arg(e.what()));
        spdlog::error("InventoryPanel::onRemoveItemClicked: Error removing item: {}", e.what());
    }
}

void InventoryPanel::onEditItemClicked() {
    // Future enhancement: Open PRO editor for selected item
    QMessageBox::information(this, "Not Implemented",
                            "Edit item functionality will be implemented in a future version.");
}

void InventoryPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column)
    if (item) {
        onEditItemClicked(); // Double-click to edit (when implemented)
    }
}

} // namespace geck