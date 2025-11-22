#include "SelectionPanel.h"
#include "../dialogs/FrmSelectorDialog.h"

#include <QFormLayout>
#include <QPixmap>
#include <QApplication>
#include <QTimer>
#include <QPainter>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QSpinBox>
#include <QEnterEvent>
#include <spdlog/spdlog.h>
#include <cmath>

#include "../../format/map/Map.h"
#include "../../format/lst/Lst.h"
#include "../../util/ResourceManager.h"
#include "../../util/ProHelper.h"
#include "../IconHelper.h"
#include "../../format/map/MapObject.h"
#include "../../format/pro/Pro.h"
#include "../../format/msg/Msg.h"
#include "../../format/frm/Frm.h"

namespace geck {

// Custom delegate for editing amount column with spinbox
class SelectionPanel::AmountDelegate : public QStyledItemDelegate {
public:
    AmountDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(option)
        Q_UNUSED(index)

        QSpinBox* editor = new QSpinBox(parent);
        editor->setRange(1, 999999);
        editor->setSingleStep(1);
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        int value = index.model()->data(index, Qt::EditRole).toInt();
        QSpinBox* spinBox = static_cast<QSpinBox*>(editor);
        spinBox->setValue(value);
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
        QSpinBox* spinBox = static_cast<QSpinBox*>(editor);
        spinBox->interpretText();
        int value = spinBox->value();
        model->setData(index, value, Qt::EditRole);
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index)
        editor->setGeometry(option.rect);
    }
};

// Implementation of HoverSpriteLabel
HoverSpriteLabel::HoverSpriteLabel(QWidget* parent) : QLabel(parent) {
    setMouseTracking(true);
    setupEditButton();
}

void HoverSpriteLabel::setupEditButton() {
    _editButton = new QPushButton(this);
    _editButton->setIcon(createIcon(":/icons/actions/edit.svg"));
    _editButton->setToolTip("Change FRM file");
    _editButton->setFixedSize(24, 24);
    _editButton->setStyleSheet(
        "QPushButton {"
        "  background-color: rgba(255, 255, 255, 180);"
        "  border: 1px solid rgba(0, 0, 0, 100);"
        "  border-radius: 12px;"
        "}"
        "QPushButton:hover {"
        "  background-color: rgba(255, 255, 255, 220);"
        "  border-color: rgba(0, 0, 0, 150);"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(255, 255, 255, 255);"
        "}"
    );
    _editButton->setIconSize(QSize(18, 18));
    _editButton->setVisible(false); // Hidden by default
    _editButton->raise();
}

void HoverSpriteLabel::enterEvent(QEnterEvent* event) {
    Q_UNUSED(event)
    if (_editButton) {
        _editButton->setVisible(true);
        positionEditButton();
    }
}

void HoverSpriteLabel::leaveEvent(QEvent* event) {
    Q_UNUSED(event)
    if (_editButton) {
        _editButton->setVisible(false);
    }
}

void HoverSpriteLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    if (_editButton && _editButton->isVisible()) {
        positionEditButton();
    }
}

void HoverSpriteLabel::positionEditButton() {
    if (!_editButton) return;

    // Position in top-left corner with small margin
    QPoint topLeft = rect().topLeft();
    topLeft.setX(topLeft.x() + 4);
    topLeft.setY(topLeft.y() + 4);
    _editButton->move(topLeft);
    _editButton->raise();
}

// Static constants
const QColor SelectionPanel::HIGHLIGHT_COLOR = QColor(0, 255, 0, 100); // Semi-transparent green
const int SelectionPanel::ICON_SIZE = 96; // Larger icons than the separate panel
const int SelectionPanel::MAX_QUANTITY_DISPLAY = 99;

QSize SelectionPanel::sizeHint() const {
    return QSize(360, 250);
}

QSize SelectionPanel::minimumSizeHint() const {
    return QSize(200, 100);
}

SelectionPanel::SelectionPanel(QWidget* parent)
    : QWidget(parent)
    , _mainLayout(nullptr)
    , _scrollArea(nullptr)
    , _contentWidget(nullptr)
    , _contentLayout(nullptr)
    , _stackedWidget(nullptr)
    , _objectPanelWidget(nullptr)
    , _objectInfoGroup(nullptr)
    , _objectSpriteLabel(nullptr)
    , _objectNameEdit(nullptr)
    , _objectTypeEdit(nullptr)
    , _objectMessageIdSpin(nullptr)
    , _objectPositionSpin(nullptr)
    , _objectProtoPidSpin(nullptr)
    , _objectFrmPidSpin(nullptr)
    , _objectFrmPathEdit(nullptr)
    , _changeFrmButton(nullptr)
    , _editProButton(nullptr)
    , _editExitGridButton(nullptr)
    , _inventoryGroup(nullptr)
    , _inventoryTree(nullptr)
    , _addInventoryButton(nullptr)
    , _removeInventoryButton(nullptr)
    , _tilePanelWidget(nullptr)
    , _tileInfoGroup(nullptr)
    , _tilePreviewLabel(nullptr)
    , _tileIndexSpin(nullptr)
    , _elevationSpin(nullptr)
    , _hexXSpin(nullptr)
    , _hexYSpin(nullptr)
    , _worldXSpin(nullptr)
    , _worldYSpin(nullptr)
    , _tileTypeEdit(nullptr)
    , _tileIdSpin(nullptr)
    , _tileNameEdit(nullptr)
    , _hoverSpriteLabel(nullptr)
    , _selectedTileIndex(-1)
    , _selectedElevation(-1)
    , _isRoofSelected(false)
    , _hasTileSelection(false)
    , _map(nullptr) {

    // Initialize amount delegate for editable inventory amounts
    _amountDelegate = new AmountDelegate(this);

    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    setupUI();
}

void SelectionPanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(5, 5, 5, 5);

    // Create scroll area for content
    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setMinimumSize(0, 0);
    _scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _contentWidget = new QWidget();
    _contentWidget->setMinimumSize(0, 0);
    _contentWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    _contentLayout = new QVBoxLayout(_contentWidget);
    _contentLayout->setContentsMargins(5, 5, 5, 5);

    // Create stacked widget to switch between object and tile panels
    _stackedWidget = new QStackedWidget();

    // Setup Object Panel
    _objectPanelWidget = new QWidget();
    QVBoxLayout* objectLayout = new QVBoxLayout(_objectPanelWidget);

    _objectInfoGroup = new QGroupBox("Object Information");

    // Create main horizontal layout for object info
    QHBoxLayout* objectInfoMainLayout = new QHBoxLayout(_objectInfoGroup);

    // Left side: sprite and button container
    QVBoxLayout* leftSideLayout = new QVBoxLayout();

    // Create hover sprite label
    _hoverSpriteLabel = new HoverSpriteLabel();
    _hoverSpriteLabel->setText("No object selected");
    _hoverSpriteLabel->setAlignment(Qt::AlignCenter);
    _hoverSpriteLabel->setMinimumHeight(128);
    _hoverSpriteLabel->setMinimumWidth(128);
    _hoverSpriteLabel->setMaximumHeight(128);
    _hoverSpriteLabel->setMaximumWidth(128);
    _hoverSpriteLabel->setScaledContents(false);
    _hoverSpriteLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _hoverSpriteLabel->setStyleSheet("border: 1px solid gray; background-color: #f0f0f0;");

    // Connect the edit button signal
    connect(_hoverSpriteLabel->editButton(), &QPushButton::clicked, this, &SelectionPanel::onChangeFrmClicked);

    leftSideLayout->addWidget(_hoverSpriteLabel);

    // Edit PRO button below the sprite
    _editProButton = new QPushButton("Edit PRO...");
    _editProButton->setEnabled(false);
    connect(_editProButton, &QPushButton::clicked, this, &SelectionPanel::onEditProClicked);
    leftSideLayout->addWidget(_editProButton);
    leftSideLayout->addStretch(); // Push everything to top

    // Right side: object properties form
    QFormLayout* objectFormLayout = new QFormLayout();

    // Keep the old _objectSpriteLabel as null since we're using _hoverSpriteLabel now
    _objectSpriteLabel = nullptr;

    // Object properties
    _objectNameEdit = new QLineEdit();
    _objectNameEdit->setReadOnly(true);
    _objectNameEdit->setPlaceholderText("No object selected");
    objectFormLayout->addRow("Name:", _objectNameEdit);

    _objectTypeEdit = new QLineEdit();
    _objectTypeEdit->setReadOnly(true);
    _objectTypeEdit->setPlaceholderText("No object selected");
    objectFormLayout->addRow("Type:", _objectTypeEdit);

    _objectMessageIdSpin = new QSpinBox();
    _objectMessageIdSpin->setRange(0, INT_MAX);
    _objectMessageIdSpin->setReadOnly(true);
    _objectMessageIdSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    objectFormLayout->addRow("Message ID:", _objectMessageIdSpin);

    _objectPositionSpin = new QSpinBox();
    _objectPositionSpin->setRange(0, INT_MAX);
    _objectPositionSpin->setReadOnly(true);
    _objectPositionSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    objectFormLayout->addRow("Position:", _objectPositionSpin);

    _objectProtoPidSpin = new QSpinBox();
    _objectProtoPidSpin->setRange(0, INT_MAX);
    _objectProtoPidSpin->setReadOnly(true);
    _objectProtoPidSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    objectFormLayout->addRow("Proto PID:", _objectProtoPidSpin);
    
    // FRM PID field
    _objectFrmPidSpin = new QSpinBox();
    _objectFrmPidSpin->setRange(0, INT_MAX);
    _objectFrmPidSpin->setReadOnly(true);
    _objectFrmPidSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    objectFormLayout->addRow("FRM PID:", _objectFrmPidSpin);
    
    // FRM Path display
    _objectFrmPathEdit = new QLineEdit();
    _objectFrmPathEdit->setReadOnly(true);
    _objectFrmPathEdit->setPlaceholderText("FRM path");
    objectFormLayout->addRow("FRM Path:", _objectFrmPathEdit);

    // Change FRM button is now the hover edit icon on the sprite
    _changeFrmButton = nullptr;
    
    // Edit Exit Grid button
    _editExitGridButton = new QPushButton("Edit Exit Grid...");
    _editExitGridButton->setEnabled(false);
    _editExitGridButton->setVisible(false); // Hidden by default
    connect(_editExitGridButton, &QPushButton::clicked, this, &SelectionPanel::onEditExitGridClicked);
    objectFormLayout->addRow("", _editExitGridButton);

    // Complete the object info group layout
    objectInfoMainLayout->addLayout(leftSideLayout);
    objectInfoMainLayout->addLayout(objectFormLayout, 1); // Form takes more space

    objectLayout->addWidget(_objectInfoGroup);

    // Setup inventory section (initially hidden)
    setupInventorySection();
    objectLayout->addWidget(_inventoryGroup);

    objectLayout->addStretch();

    // Setup Tile Panel
    _tilePanelWidget = new QWidget();
    QVBoxLayout* tileLayout = new QVBoxLayout(_tilePanelWidget);

    _tileInfoGroup = new QGroupBox("Tile Information");
    QFormLayout* tileFormLayout = new QFormLayout(_tileInfoGroup);

    // Tile preview display
    _tilePreviewLabel = new QLabel("No tile selected");
    _tilePreviewLabel->setAlignment(Qt::AlignCenter);
    _tilePreviewLabel->setMinimumHeight(96);
    _tilePreviewLabel->setMinimumWidth(128);
    _tilePreviewLabel->setMaximumHeight(96);
    _tilePreviewLabel->setMaximumWidth(128);
    _tilePreviewLabel->setScaledContents(false);
    _tilePreviewLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _tilePreviewLabel->setStyleSheet("border: 1px solid gray; background-color: #f0f0f0;");
    tileFormLayout->addRow("Preview:", _tilePreviewLabel);

    // Tile type (Floor/Roof)
    _tileTypeEdit = new QLineEdit();
    _tileTypeEdit->setReadOnly(true);
    _tileTypeEdit->setPlaceholderText("No tile selected");
    tileFormLayout->addRow("Type:", _tileTypeEdit);

    // Tile index
    _tileIndexSpin = new QSpinBox();
    _tileIndexSpin->setRange(0, Map::TILES_PER_ELEVATION - 1);
    _tileIndexSpin->setReadOnly(true);
    _tileIndexSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("Tile Index:", _tileIndexSpin);

    // Elevation
    _elevationSpin = new QSpinBox();
    _elevationSpin->setRange(0, 1);
    _elevationSpin->setReadOnly(true);
    _elevationSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("Elevation:", _elevationSpin);

    // Hex coordinates
    _hexXSpin = new QSpinBox();
    _hexXSpin->setRange(0, Map::COLS - 1);
    _hexXSpin->setReadOnly(true);
    _hexXSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("Hex X:", _hexXSpin);

    _hexYSpin = new QSpinBox();
    _hexYSpin->setRange(0, Map::ROWS - 1);
    _hexYSpin->setReadOnly(true);
    _hexYSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("Hex Y:", _hexYSpin);

    // World coordinates
    _worldXSpin = new QSpinBox();
    _worldXSpin->setRange(0, INT_MAX);
    _worldXSpin->setReadOnly(true);
    _worldXSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("World X:", _worldXSpin);

    _worldYSpin = new QSpinBox();
    _worldYSpin->setRange(0, INT_MAX);
    _worldYSpin->setReadOnly(true);
    _worldYSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("World Y:", _worldYSpin);

    // Tile information (shows either floor or roof depending on selection)
    _tileIdSpin = new QSpinBox();
    _tileIdSpin->setRange(0, UINT16_MAX);
    _tileIdSpin->setReadOnly(true);
    _tileIdSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("Tile ID:", _tileIdSpin);

    _tileNameEdit = new QLineEdit();
    _tileNameEdit->setReadOnly(true);
    _tileNameEdit->setPlaceholderText("No tile selected");
    tileFormLayout->addRow("Tile Name:", _tileNameEdit);

    tileLayout->addWidget(_tileInfoGroup);
    tileLayout->addStretch();

    // Add both panels to stacked widget
    _stackedWidget->addWidget(_objectPanelWidget);
    _stackedWidget->addWidget(_tilePanelWidget);

    _contentLayout->addWidget(_stackedWidget);
    _scrollArea->setWidget(_contentWidget);
    _mainLayout->addWidget(_scrollArea);

    // Initially clear the display
    clearSelection();
}

void SelectionPanel::setMap(Map* map) {
    _map = map;
}

void SelectionPanel::selectObject(std::shared_ptr<Object> selectedObject) {
    if (selectedObject == nullptr) {
        clearSelection();
        spdlog::debug("SelectionPanel: Object deselected");
    } else {
        _selectedObject = selectedObject;
        _hasTileSelection = false;
        showObjectPanel();
        updateObjectInfo();
        spdlog::debug("SelectionPanel: Object selected with PID {}",
            selectedObject->getMapObject().pro_pid);
    }
}

void SelectionPanel::selectTile(int tileIndex, int elevation, bool isRoof) {
    _selectedTileIndex = tileIndex;
    _selectedElevation = elevation;
    _isRoofSelected = isRoof;
    _hasTileSelection = true;
    _selectedObject.reset();

    showTilePanel();
    updateTileInfo();

    spdlog::debug("SelectionPanel: Tile selected - index: {}, elevation: {}, isRoof: {}",
        tileIndex, elevation, isRoof);
}

void SelectionPanel::clearSelection() {
    _selectedObject.reset();
    _hasTileSelection = false;
    clearObjectInfo();
    clearTileInfo();

    // Show object panel by default when nothing is selected
    showObjectPanel();

    spdlog::debug("SelectionPanel: Selection cleared");
}

void SelectionPanel::showObjectPanel() {
    _stackedWidget->setCurrentWidget(_objectPanelWidget);
}

void SelectionPanel::showTilePanel() {
    _stackedWidget->setCurrentWidget(_tilePanelWidget);
}

void SelectionPanel::updateObjectInfo() {
    if (!_selectedObject || !_selectedObject.value()) {
        clearObjectInfo();
        return;
    }

    try {
        auto& selectedMapObject = _selectedObject.value()->getMapObject();
        int32_t PID = selectedMapObject.pro_pid;

        // Load Proto file to get object information
        auto pro = ResourceManager::getInstance().loadResource<Pro>(ProHelper::basePath(PID));

        if (pro) {
            // Get object name from message file
            auto msg = ProHelper::msgFile(pro->type());
            std::string objectName = "Unknown";
            if (msg) {
                try {
                    objectName = msg->message(pro->header.message_id).text;
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to get message for ID {}: {}", pro->header.message_id, e.what());
                }
            }

            // Update UI with object information
            _objectNameEdit->setText(QString::fromStdString(objectName));
            _objectTypeEdit->setText(QString::fromStdString(pro->typeToString()));
            _objectMessageIdSpin->setValue(static_cast<int>(pro->header.message_id));
            _objectPositionSpin->setValue(selectedMapObject.position);
            _objectProtoPidSpin->setValue(static_cast<int>(selectedMapObject.pro_pid));
            
            // Update FRM information
            _objectFrmPidSpin->setValue(static_cast<int>(selectedMapObject.frm_pid));
            
            // Determine which FRM is actually being used
            uint32_t activeFrmPid = selectedMapObject.frm_pid != 0 ? selectedMapObject.frm_pid : pro->header.FID;
            std::string frmPath = ResourceManager::getInstance().FIDtoFrmName(activeFrmPid);
            _objectFrmPathEdit->setText(QString::fromStdString(frmPath));
            
            // Enable the change FRM button (edit icon)
            _hoverSpriteLabel->editButton()->setEnabled(true);
            
            // Enable the edit PRO button
            _editProButton->setEnabled(true);
            
            // Show/hide and enable exit grid button based on object type
            if (selectedMapObject.isExitGridMarker()) {
                _editExitGridButton->setVisible(true);
                _editExitGridButton->setEnabled(true);
            } else {
                _editExitGridButton->setVisible(false);
                _editExitGridButton->setEnabled(false);
            }
            

            // Convert SFML sprite to QPixmap for display
            const auto& sprite = _selectedObject.value()->getSprite();
            const auto& texture = sprite.getTexture();

            // Get the image from SFML texture (texture is always valid in SFML 3)
            auto image = texture.copyToImage();
            auto textureRect = sprite.getTextureRect();

            // Create QImage from SFML image data
            const std::uint8_t* pixels = image.getPixelsPtr();
            QImage qImage(pixels, image.getSize().x, image.getSize().y, QImage::Format_RGBA8888);

            // Extract the sprite's texture rectangle if needed
            if (textureRect.size.x > 0 && textureRect.size.y > 0) {
                qImage = qImage.copy(textureRect.position.x, textureRect.position.y, textureRect.size.x, textureRect.size.y);
            }

            // Create pixmap and scale to fit label while maintaining aspect ratio
            QPixmap pixmap = QPixmap::fromImage(qImage);
            if (!pixmap.isNull()) {
                // Scale the pixmap to fit within 128x128 while keeping aspect ratio
                QSize maxSize(128, 128);

                if (pixmap.width() > maxSize.width() || pixmap.height() > maxSize.height()) {
                    pixmap = pixmap.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }

                _hoverSpriteLabel->setPixmap(pixmap);
                _hoverSpriteLabel->setText("");
            } else {
                _hoverSpriteLabel->setText("Failed to convert sprite");
            }

            _objectInfoGroup->setTitle("Object Information");
        } else {
            spdlog::warn("Failed to load proto file for PID: {}", PID);
            clearObjectInfo();
        }
    } catch (const std::exception& e) {
        spdlog::error("Error updating object info: {}", e.what());
        clearObjectInfo();
    }

    // Update inventory section
    updateInventorySection();
}

void geck::SelectionPanel::updateTileInfo() {
    if (!_hasTileSelection || !_map) {
        clearTileInfo();
        return;
    }

    try {
        // Get the actual tile data from the map
        auto& mapFile = _map->getMapFile();
        
        // Check if the selected elevation exists in the map data
        if (mapFile.tiles.find(_selectedElevation) == mapFile.tiles.end()) {
            spdlog::warn("SelectionPanel::updateTileInfo: Selected elevation {} does not exist in map data", _selectedElevation);
            clearTileInfo();
            return;
        }
        
        auto& tile = mapFile.tiles.at(_selectedElevation).at(_selectedTileIndex);

        // Calculate hex coordinates from tile index
        uint32_t hexX = static_cast<uint32_t>(std::ceil(static_cast<double>(_selectedTileIndex) / 100));
        uint32_t hexY = _selectedTileIndex % 100;

        // Calculate world coordinates
        uint32_t worldX = (100 - hexY - 1) * 48 + 32 * (hexX - 1);
        uint32_t worldY = hexX * 24 + (hexY - 1) * 12 + 1;

        // Update basic tile information
        _tileTypeEdit->setText(_isRoofSelected ? "Roof Tile" : "Floor Tile");
        _tileIndexSpin->setValue(_selectedTileIndex);
        _elevationSpin->setValue(_selectedElevation);
        _hexXSpin->setValue(static_cast<int>(hexX));
        _hexYSpin->setValue(static_cast<int>(hexY));
        _worldXSpin->setValue(static_cast<int>(worldX));
        _worldYSpin->setValue(static_cast<int>(worldY));

        // Get the actual tile IDs from the map data
        uint16_t floorTileId = tile.getFloor();
        uint16_t roofTileId = tile.getRoof();

        // Load tiles.lst to get tile names
        try {
            auto& resourceManager = ResourceManager::getInstance();
            auto tilesList = resourceManager.getResource<Lst, std::string>("art/tiles/tiles.lst");

            if (tilesList) {
                auto tileNames = tilesList->list();

                uint16_t currentTileId = _isRoofSelected ? roofTileId : floorTileId;
                _tileIdSpin->setValue(currentTileId);

                if (currentTileId < tileNames.size()) {
                    _tileNameEdit->setText(QString::fromStdString(tileNames.at(currentTileId)));
                } else {
                    _tileNameEdit->setText("Invalid tile ID");
                }

                // Load and display tile preview
                loadTilePreview(tilesList, currentTileId);

            } else {
                _tileIdSpin->setValue(0);
                _tileNameEdit->setText("tiles.lst not found");
                _tilePreviewLabel->setText("Preview unavailable");
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to load tile information: {}", e.what());
            _tileIdSpin->setValue(0);
            _tileNameEdit->setText("Error loading tile info");
            _tilePreviewLabel->setText("Preview error");
        }

        _tileInfoGroup->setTitle(QString("Tile Information - %1").arg(_isRoofSelected ? "Roof" : "Floor"));

    } catch (const std::exception& e) {
        spdlog::error("Error updating tile info: {}", e.what());
        clearTileInfo();
    }
}

void geck::SelectionPanel::clearObjectInfo() {
    _objectNameEdit->clear();
    _objectNameEdit->setPlaceholderText("No object selected");

    _objectTypeEdit->clear();
    _objectTypeEdit->setPlaceholderText("No object selected");

    _objectMessageIdSpin->setValue(0);
    _objectPositionSpin->setValue(0);
    _objectProtoPidSpin->setValue(0);
    _objectFrmPidSpin->setValue(0);
    
    _objectFrmPathEdit->clear();
    _objectFrmPathEdit->setPlaceholderText("FRM path");
    
    _hoverSpriteLabel->editButton()->setEnabled(false);
    _editProButton->setEnabled(false);
    _editExitGridButton->setEnabled(false);
    _editExitGridButton->setVisible(false);

    _hoverSpriteLabel->clear();
    _hoverSpriteLabel->setText("No object selected");
    _objectInfoGroup->setTitle("Object Information");

    // Hide inventory section when no object is selected
    _inventoryGroup->setVisible(false);
}

void geck::SelectionPanel::clearTileInfo() {
    _tileTypeEdit->clear();
    _tileTypeEdit->setPlaceholderText("No tile selected");

    _tileIndexSpin->setValue(0);
    _elevationSpin->setValue(0);
    _hexXSpin->setValue(0);
    _hexYSpin->setValue(0);
    _worldXSpin->setValue(0);
    _worldYSpin->setValue(0);

    _tileIdSpin->setValue(0);

    _tileNameEdit->clear();
    _tileNameEdit->setPlaceholderText("No tile selected");

    _tilePreviewLabel->clear();
    _tilePreviewLabel->setText("No tile selected");
    _tileInfoGroup->setTitle("Tile Information");
}

void geck::SelectionPanel::loadTilePreview(Lst* tilesList, uint16_t tileId) {
    try {
        auto tileNames = tilesList->list();
        if (tileId >= tileNames.size()) {
            _tilePreviewLabel->setText("Invalid tile ID");
            return;
        }

        // Get the tile texture path
        std::string tilePath = "art/tiles/" + tileNames.at(tileId);

        // Load the texture from ResourceManager
        auto& resourceManager = ResourceManager::getInstance();
        auto& texture = resourceManager.texture(tilePath);

        // Convert SFML texture to QPixmap for display
        auto image = texture.copyToImage();
        const std::uint8_t* pixels = image.getPixelsPtr();
        QImage qImage(pixels, image.getSize().x, image.getSize().y, QImage::Format_RGBA8888);

        // Create pixmap and scale to fit label while maintaining aspect ratio
        QPixmap pixmap = QPixmap::fromImage(qImage);
        if (!pixmap.isNull()) {
            // Scale the pixmap to fit within the label size while keeping aspect ratio
            QSize maxSize(128, 96);

            if (pixmap.width() > maxSize.width() || pixmap.height() > maxSize.height()) {
                pixmap = pixmap.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }

            _tilePreviewLabel->setPixmap(pixmap);
            _tilePreviewLabel->setText(""); // Clear the text when we have a pixmap
        } else {
            _tilePreviewLabel->setText("Failed to load tile");
        }

    } catch (const std::exception& e) {
        spdlog::warn("Failed to load tile preview: {}", e.what());
        _tilePreviewLabel->setText("Preview error");
    }
}

void geck::SelectionPanel::handleSelectionChanged(const selection::SelectionState& selection, int elevation) {
    // Handle selection changes efficiently for large selections
    if (selection.isEmpty()) {
        clearSelection();
        return;
    }

    // For multi-selection, show summary info instead of individual tile details
    if (selection.items.size() > 1) {
        showTilePanel();

        // Update tile info group title to show selection count
        _tileInfoGroup->setTitle(QString("Tile Selection (%1 tiles)").arg(selection.items.size()));

        // Clear individual tile details for multi-selection
        _tileIndexSpin->setValue(0);
        _elevationSpin->setValue(elevation);
        _hexXSpin->setValue(0);
        _hexYSpin->setValue(0);
        _tileTypeEdit->clear();
        _tileIdSpin->setValue(0);
        _tileNameEdit->clear();
        _tilePreviewLabel->setText(QString("Multiple tiles selected (%1)").arg(selection.items.size()));

        // Enable relevant controls
        _elevationSpin->setEnabled(true);
        _tileTypeEdit->setEnabled(false);
        _tileIdSpin->setEnabled(false);
        _tileNameEdit->setEnabled(false);

        spdlog::debug("SelectionPanel: Multiple selection - {} tiles", selection.items.size());
        return;
    }

    // Single selection - use existing logic
    const auto& item = selection.items[0];
    switch (item.type) {
        case selection::SelectionType::OBJECT: {
            auto object = item.getObject();
            if (object) {
                selectObject(object);
            }
            break;
        }
        case selection::SelectionType::ROOF_TILE:
        case selection::SelectionType::FLOOR_TILE: {
            int tileIndex = item.getTileIndex();
            bool isRoof = (item.type == selection::SelectionType::ROOF_TILE);
            selectTile(tileIndex, elevation, isRoof);
            break;
        }
        case selection::SelectionType::HEX: {
            // Hex selection - could show hex coordinates or other hex-specific info
            int hexIndex = item.getHexIndex();
            // For now, just log hex selection
            spdlog::debug("SelectionPanel: Hex {} selected", hexIndex);
            break;
        }
    }
}

void SelectionPanel::onChangeFrmClicked() {
    if (!_selectedObject || !_selectedObject.value()) {
        return;
    }

    auto& mapObject = _selectedObject.value()->getMapObject();
    
    // Create and show FRM selector dialog
    FrmSelectorDialog dialog(this);
    
    // Set the current FRM PID as initial selection
    uint32_t currentFrmPid = mapObject.frm_pid != 0 ? mapObject.frm_pid : mapObject.pro_pid;
    dialog.setInitialFrmPid(currentFrmPid);
    
    // Set object type filter - extract type from current FRM PID
    uint32_t objectType = (currentFrmPid >> 24) & 0xFF;
    dialog.setObjectTypeFilter(objectType);
    spdlog::info("SelectionPanel: Filtering FRM dialog by object type: {}", objectType);
    
    if (dialog.exec() == QDialog::Accepted) {
        uint32_t newFrmPid = dialog.getSelectedFrmPid();
        std::string newFrmPath = dialog.getSelectedFrmPath();
        
        if (!newFrmPath.empty()) {
            // Validate that the FRM file actually exists before attempting the change
            try {
                auto& resourceManager = ResourceManager::getInstance();
                auto testLoad = resourceManager.loadResource<Frm>(newFrmPath);
                if (!testLoad) {
                    spdlog::error("SelectionPanel: FRM file not found or invalid: {} - aborting change", newFrmPath);
                    return;
                }
            } catch (const std::exception& e) {
                spdlog::error("SelectionPanel: Failed to validate FRM file {}: {} - aborting change", newFrmPath, e.what());
                return;
            }
            
            // Always update the visual representation using the path first
            emit objectFrmPathChanged(_selectedObject.value(), newFrmPath);
            
            // Try to update MapObject's frm_pid if we have a valid derived FID
            if (newFrmPid != 0) {
                // Check if this is a custom FID (has 0xFF in high byte of baseId)
                bool isCustomFid = ((newFrmPid & 0x00FF0000) == 0x00FF0000);
                
                if (newFrmPid != currentFrmPid) {
                    if (isCustomFid) {
                        // Custom FID - visual will work in editor but may not persist in game
                        spdlog::warn("SelectionPanel: Using custom FID 0x{:08X} for non-LST FRM - may not work in game", newFrmPid);
                        
                        // For custom FIDs, we'll update the visual but keep original frm_pid for game compatibility
                        // This allows the editor to show the new FRM while maintaining game compatibility
                        spdlog::info("SelectionPanel: Keeping original frm_pid {} for game compatibility, visual uses custom FRM", currentFrmPid);
                        
                        // Show warning to user
                        emit statusMessage(QString("Warning: Custom FRM may not display correctly in game"));
                    } else {
                        // Valid LST-based FID, safe to update
                        mapObject.frm_pid = newFrmPid;
                        spdlog::info("SelectionPanel: Updated MapObject frm_pid from {} to {} for persistent save", 
                                    currentFrmPid, newFrmPid);
                    }
                    
                    // Refresh the object info display to show updated FRM PID
                    updateObjectInfo();
                } else {
                    spdlog::debug("SelectionPanel: FRM PID unchanged ({}), no MapObject update needed", newFrmPid);
                    // Still update the FRM path display
                    _objectFrmPathEdit->setText(QString::fromStdString(newFrmPath));
                }
            } else {
                // For paths that don't have reliable FIDs, we need to be more careful
                spdlog::warn("SelectionPanel: Could not derive reliable FID for path: {} - using alternative approach", 
                            newFrmPath);
                
                // IMPORTANT: The FRM might be valid but just not in the LST file
                // In this case, we should still try to use it by keeping the visual change
                // but warning the user about potential game compatibility issues
                
                // Extract just the filename for analysis
                size_t lastSlash = newFrmPath.find_last_of('/');
                std::string filename = (lastSlash != std::string::npos) ? newFrmPath.substr(lastSlash + 1) : newFrmPath;
                
                // For critters, we have a special case - many FRMs work in-game even if not in LST
                if ((currentFrmPid >> 24) == 1) { // Critter type
                    // Try to find a similar base in the LST file
                    // For example, "hanpwraa.frm" might work if "hapowr" is in the LST
                    
                    // Keep the visual change but warn about compatibility
                    spdlog::warn("SelectionPanel: FRM '{}' not found in critters.lst - change may not persist in game", filename);
                    spdlog::info("SelectionPanel: Keeping original FRM PID ({}) for game compatibility", currentFrmPid);
                    
                    // Show warning to user
                    emit statusMessage(QString("Warning: FRM '%1' may not display correctly in game - not found in critters.lst")
                                     .arg(QString::fromStdString(filename)));
                }
                
                // Update the FRM path display
                _objectFrmPathEdit->setText(QString::fromStdString(newFrmPath));
                updateObjectInfo();
            }
            
            spdlog::info("SelectionPanel: Changed object FRM visual to path: {}", newFrmPath);
            
            // Ensure the object remains selected and highlighted after FRM change
            if (_selectedObject.has_value() && _selectedObject.value()) {
                // Use a single-shot timer to delay the highlight request
                // This ensures the texture update is fully processed before highlighting
                QTimer::singleShot(50, this, [this]() {
                    if (_selectedObject.has_value() && _selectedObject.value()) {
                        emit requestObjectHighlight(_selectedObject.value());
                    }
                });
            }
        }
    }
}

void SelectionPanel::onEditProClicked() {
    if (!_selectedObject || !_selectedObject.value()) {
        return;
    }
    
    // Emit signal to request PRO editor for the selected object
    emit requestProEditor(_selectedObject.value());
}

void SelectionPanel::onEditExitGridClicked() {
    if (!_selectedObject || !_selectedObject.value()) {
        return;
    }
    
    // Emit signal to request exit grid editor for the selected object
    emit requestExitGridEditor(_selectedObject.value());
}


void SelectionPanel::onAddInventoryClicked() {
    // TODO: Implement add inventory item functionality
    spdlog::debug("SelectionPanel::onAddInventoryClicked: Add inventory functionality not yet implemented");
}

void SelectionPanel::onRemoveInventoryClicked() {
    QTreeWidgetItem* currentItem = _inventoryTree->currentItem();
    if (!currentItem) {
        return;
    }

    // TODO: Implement remove inventory item functionality
    // For now, just remove from tree widget
    delete currentItem;
    spdlog::debug("SelectionPanel::onRemoveInventoryClicked: Remove inventory functionality partially implemented");
}

void SelectionPanel::onInventoryItemChanged(QTreeWidgetItem* item, int column) {
    if (!item || column != COLUMN_AMOUNT) {
        return;
    }

    // TODO: Implement inventory item amount change functionality
    bool ok;
    int newAmount = item->text(COLUMN_AMOUNT).toInt(&ok);
    if (ok && newAmount >= 0) {
        // Update the underlying data
        uint32_t pid = item->data(COLUMN_ICON, Qt::UserRole).toUInt();
        spdlog::debug("SelectionPanel::onInventoryItemChanged: Changing amount for PID {} to {}", pid, newAmount);

        // Update the icon with new quantity overlay
        QPixmap iconWithQuantity = getItemIconWithQuantity(pid, newAmount);
        spdlog::debug("SelectionPanel::onInventoryItemChanged: Updating icon for PID {} with size {}x{}",
                     pid, iconWithQuantity.width(), iconWithQuantity.height());

        // Create QIcon explicitly with the correct size to avoid scaling issues
        QIcon icon;
        icon.addPixmap(iconWithQuantity, QIcon::Normal, QIcon::Off);
        item->setIcon(COLUMN_ICON, icon);

        // Set explicit size hint to ensure proper icon display
        item->setSizeHint(COLUMN_ICON, QSize(ICON_SIZE, ICON_SIZE));
    } else {
        // Revert to previous value if invalid
        // TODO: Get actual amount from underlying data
        item->setText(COLUMN_AMOUNT, "1");
    }
}

void SelectionPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // Check if we should switch to horizontal layout
    bool shouldUseHorizontal = (width() >= HORIZONTAL_LAYOUT_MIN_WIDTH &&
                                _inventoryGroup && _inventoryGroup->isVisible());

    if (shouldUseHorizontal != _isHorizontalLayout) {
        switchLayout(shouldUseHorizontal);
    }
}

void SelectionPanel::switchLayout(bool horizontal) {
    if (!_objectInfoGroup || !_inventoryGroup) {
        return;
    }

    if (horizontal) {
        applyHorizontalLayout();
    } else {
        applyVerticalLayout();
    }

    _isHorizontalLayout = horizontal;
}

void SelectionPanel::applyHorizontalLayout() {
    // Create a new horizontal container if it doesn't exist
    QWidget* newContainer = new QWidget();
    QHBoxLayout* hLayout = new QHBoxLayout(newContainer);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(10);

    // Create left side for object info
    QWidget* leftSide = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftSide);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(_objectInfoGroup);
    leftLayout->addStretch();

    // Add widgets to horizontal layout
    hLayout->addWidget(leftSide, 1);  // Object info takes 1 part
    hLayout->addWidget(_inventoryGroup, 1);  // Inventory takes 1 part (50/50 split)

    // Replace the content in object panel
    QLayout* oldLayout = _objectPanelWidget->layout();
    if (oldLayout) {
        // Remove widgets from old layout without deleting them
        oldLayout->removeWidget(_objectInfoGroup);
        oldLayout->removeWidget(_inventoryGroup);
        delete oldLayout;
    }

    // Set the new layout
    QVBoxLayout* wrapperLayout = new QVBoxLayout(_objectPanelWidget);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->addWidget(newContainer);
}

void SelectionPanel::applyVerticalLayout() {
    // Remove widgets from any current layout
    if (_objectPanelWidget->layout()) {
        QLayout* currentLayout = _objectPanelWidget->layout();

        // Find and remove widgets
        for (int i = currentLayout->count() - 1; i >= 0; --i) {
            QLayoutItem* item = currentLayout->itemAt(i);
            if (item && item->widget()) {
                item->widget()->setParent(nullptr);
            }
        }

        delete currentLayout;
    }

    // Create standard vertical layout
    QVBoxLayout* vLayout = new QVBoxLayout(_objectPanelWidget);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->addWidget(_objectInfoGroup);
    vLayout->addWidget(_inventoryGroup);
    vLayout->addStretch();
}

void SelectionPanel::setupInventorySection() {
    _inventoryGroup = new QGroupBox("Inventory");
    _inventoryGroup->setVisible(false); // Hidden by default

    QVBoxLayout* inventoryLayout = new QVBoxLayout(_inventoryGroup);

    // Create inventory tree widget
    _inventoryTree = new QTreeWidget();
    _inventoryTree->setHeaderLabels({"", "Name", "Type", "Amount"});
    _inventoryTree->setColumnWidth(COLUMN_ICON, ICON_SIZE + 20);
    _inventoryTree->setColumnWidth(COLUMN_NAME, 150);
    _inventoryTree->setColumnWidth(COLUMN_TYPE, 100);
    _inventoryTree->setColumnWidth(COLUMN_AMOUNT, 80);
    _inventoryTree->setRootIsDecorated(false);
    _inventoryTree->setAlternatingRowColors(true);
    _inventoryTree->setSelectionMode(QAbstractItemView::SingleSelection);
    _inventoryTree->setMinimumHeight(300);
    _inventoryTree->setIconSize(QSize(ICON_SIZE, ICON_SIZE));

    // Set uniform item heights for larger icons
    _inventoryTree->setUniformRowHeights(true);

    // Set custom delegate for amount column editing
    _inventoryTree->setItemDelegateForColumn(COLUMN_AMOUNT, _amountDelegate);

    connect(_inventoryTree, &QTreeWidget::itemChanged, this, &SelectionPanel::onInventoryItemChanged);

    inventoryLayout->addWidget(_inventoryTree);

    // Button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    _addInventoryButton = new QPushButton("Add Item");
    _removeInventoryButton = new QPushButton("Remove");
    _removeInventoryButton->setEnabled(false);

    connect(_addInventoryButton, &QPushButton::clicked, this, &SelectionPanel::onAddInventoryClicked);
    connect(_removeInventoryButton, &QPushButton::clicked, this, &SelectionPanel::onRemoveInventoryClicked);

    buttonLayout->addWidget(_addInventoryButton);
    buttonLayout->addWidget(_removeInventoryButton);
    buttonLayout->addStretch();

    inventoryLayout->addLayout(buttonLayout);
}

void SelectionPanel::updateInventorySection() {
    bool wasVisible = _inventoryGroup->isVisible();

    if (!_selectedObject || !_selectedObject.value()) {
        _inventoryGroup->setVisible(false);
        // Check if layout needs updating after visibility change
        if (wasVisible) {
            resizeEvent(nullptr);
        }
        return;
    }

    auto object = _selectedObject.value();
    auto mapObject = object->getMapObjectPtr();

    if (!mapObject) {
        _inventoryGroup->setVisible(false);
        if (wasVisible) {
            resizeEvent(nullptr);
        }
        return;
    }

    // Check if object has inventory capability by loading Pro file
    try {
        auto pro = ResourceManager::getInstance().loadResource<Pro>(ProHelper::basePath(mapObject->pro_pid));
        if (pro) {
            bool hasInventory = (pro->type() == Pro::OBJECT_TYPE::ITEM &&
                               pro->itemType() == Pro::ITEM_TYPE::CONTAINER) ||
                              pro->type() == Pro::OBJECT_TYPE::CRITTER;

            _inventoryGroup->setVisible(hasInventory);

            // Check if layout needs updating after visibility change
            if (wasVisible != hasInventory) {
                resizeEvent(nullptr);
            }

            if (hasInventory) {
                populateInventoryTree();
            }
        } else {
            _inventoryGroup->setVisible(false);
            if (wasVisible) {
                resizeEvent(nullptr);
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load pro file for inventory check: {}", e.what());
        _inventoryGroup->setVisible(false);
        if (wasVisible) {
            resizeEvent(nullptr);
        }
    }
}

void SelectionPanel::populateInventoryTree() {
    spdlog::debug("SelectionPanel::populateInventoryTree: Starting to populate inventory tree");
    _inventoryTree->clear();

    if (!_selectedObject || !_selectedObject.value()) {
        spdlog::debug("SelectionPanel::populateInventoryTree: No selected object");
        return;
    }

    auto object = _selectedObject.value();
    auto mapObject = object->getMapObjectPtr();

    if (!mapObject) {
        spdlog::debug("SelectionPanel::populateInventoryTree: No map object");
        return;
    }

    spdlog::debug("SelectionPanel::populateInventoryTree: Found {} inventory items", mapObject->inventory.size());
    // Populate tree with inventory items using the MapObject's inventory vector
    for (const auto& inventoryItem : mapObject->inventory) {
        if (!inventoryItem) continue;

        spdlog::debug("SelectionPanel::populateInventoryTree: Processing inventory item with PID {}, amount {}",
                     inventoryItem->pro_pid, inventoryItem->amount);
        QTreeWidgetItem* item = new QTreeWidgetItem(_inventoryTree);

        // Set all text data first
        item->setText(COLUMN_NAME, getItemName(inventoryItem->pro_pid));
        item->setText(COLUMN_TYPE, getItemTypeName(inventoryItem->pro_pid));
        item->setText(COLUMN_AMOUNT, QString::number(inventoryItem->amount));

        // Store PID in item data for reference
        item->setData(COLUMN_ICON, Qt::UserRole, inventoryItem->pro_pid);

        // Make amount column editable
        item->setFlags(item->flags() | Qt::ItemIsEditable);

        // Set explicit size hint to ensure proper icon display
        item->setSizeHint(COLUMN_ICON, QSize(ICON_SIZE, ICON_SIZE));

        // Set icon with quantity overlay AFTER all other data is set
        QPixmap iconWithQuantity = getItemIconWithQuantity(inventoryItem->pro_pid, inventoryItem->amount);
        spdlog::debug("SelectionPanel::populateInventoryTree: Setting icon for PID {} with size {}x{}",
                     inventoryItem->pro_pid, iconWithQuantity.width(), iconWithQuantity.height());

        // Create QIcon explicitly with the correct size to avoid scaling issues
        QIcon icon;
        icon.addPixmap(iconWithQuantity, QIcon::Normal, QIcon::Off);
        item->setIcon(COLUMN_ICON, icon);

        // Force tree widget to recognize the icon change
        _inventoryTree->updateGeometry();
    }

    // Enable/disable remove button based on selection
    connect(_inventoryTree, &QTreeWidget::itemSelectionChanged, [this]() {
        _removeInventoryButton->setEnabled(_inventoryTree->currentItem() != nullptr);
    });

    // Force tree widget to refresh display to ensure icons appear properly
    _inventoryTree->update();
    _inventoryTree->repaint();
    spdlog::debug("SelectionPanel::populateInventoryTree: Completed with {} items, forcing tree refresh", _inventoryTree->topLevelItemCount());
}

QPixmap SelectionPanel::getItemIconWithQuantity(uint32_t pid, int amount) const {
    QPixmap baseIcon = getItemIcon(pid);

    if (amount <= 1) {
        return baseIcon;
    }

    return addQuantityOverlay(baseIcon, amount);
}

QPixmap SelectionPanel::addQuantityOverlay(const QPixmap& baseIcon, int amount) const {
    if (amount <= 1) {
        return baseIcon;
    }

    // Create result with consistent size
    QPixmap result(ICON_SIZE, ICON_SIZE);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Draw the base icon (should already be properly sized from getItemIcon)
    if (baseIcon.size() != QSize(ICON_SIZE, ICON_SIZE)) {
        // Only scale if the base icon is not the expected size
        QPixmap scaledIcon = baseIcon.scaled(ICON_SIZE, ICON_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = (ICON_SIZE - scaledIcon.width()) / 2;
        int y = (ICON_SIZE - scaledIcon.height()) / 2;
        painter.drawPixmap(x, y, scaledIcon);
    } else {
        // Draw the icon directly since it's already the correct size
        painter.drawPixmap(0, 0, baseIcon);
    }

    // Draw quantity text overlay in bottom-right corner
    QString quantityText;
    if (amount > MAX_QUANTITY_DISPLAY) {
        quantityText = QString("%1k").arg(amount / 1000);
    } else {
        quantityText = QString::number(amount);
    }

    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(10);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(quantityText);

    // Position in bottom-right corner with padding
    int textX = ICON_SIZE - textRect.width() - 4;
    int textY = ICON_SIZE - 4;

    // Draw text outline (black)
    QPen outlinePen(Qt::black, 2);
    painter.setPen(outlinePen);
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx != 0 || dy != 0) {
                painter.drawText(textX + dx, textY + dy, quantityText);
            }
        }
    }

    // Draw main text (bright green)
    QPen textPen(QColor(50, 255, 50));
    painter.setPen(textPen);
    painter.drawText(textX, textY, quantityText);

    return result;
}

QString SelectionPanel::getItemName(uint32_t pid) const {
    try {
        auto pro = ResourceManager::getInstance().loadResource<Pro>(ProHelper::basePath(pid));
        if (pro) {
            // Get object name from message file
            auto msg = ProHelper::msgFile(pro->type());
            if (msg) {
                try {
                    return QString::fromStdString(msg->message(pro->header.message_id).text);
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to get message for ID {}: {}", pro->header.message_id, e.what());
                }
            }
        }
        return QString("Unknown Item (%1)").arg(pid);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to get item name for PID {}: {}", pid, e.what());
        return QString("Unknown Item (%1)").arg(pid);
    }
}

QString SelectionPanel::getItemTypeName(uint32_t pid) const {
    try {
        auto pro = ResourceManager::getInstance().loadResource<Pro>(ProHelper::basePath(pid));
        if (pro) {
            switch (pro->type()) {
                case Pro::OBJECT_TYPE::ITEM:
                    switch (pro->itemType()) {
                        case Pro::ITEM_TYPE::ARMOR: return "Armor";
                        case Pro::ITEM_TYPE::CONTAINER: return "Container";
                        case Pro::ITEM_TYPE::DRUG: return "Drug";
                        case Pro::ITEM_TYPE::WEAPON: return "Weapon";
                        case Pro::ITEM_TYPE::AMMO: return "Ammo";
                        case Pro::ITEM_TYPE::MISC: return "Misc";
                        case Pro::ITEM_TYPE::KEY: return "Key";
                        default: return "Item";
                    }
                case Pro::OBJECT_TYPE::CRITTER: return "Critter";
                case Pro::OBJECT_TYPE::SCENERY: return "Scenery";
                case Pro::OBJECT_TYPE::WALL: return "Wall";
                case Pro::OBJECT_TYPE::TILE: return "Tile";
                case Pro::OBJECT_TYPE::MISC: return "Misc";
                default: return "Unknown";
            }
        }
        return "Unknown";
    } catch (const std::exception& e) {
        spdlog::warn("Failed to get item type for PID {}: {}", pid, e.what());
        return "Unknown";
    }
}

QPixmap SelectionPanel::getItemIcon(uint32_t pid) const {
    try {
        spdlog::debug("SelectionPanel::getItemIcon: Loading icon for PID {}", pid);
        auto& resourceManager = ResourceManager::getInstance();

        // Load the Pro file to get the FID
        auto pro = resourceManager.loadResource<Pro>(ProHelper::basePath(pid));
        if (!pro) {
            spdlog::warn("Failed to load pro file for PID {} - returning placeholder", pid);
            return createPlaceholderIcon();
        }
        spdlog::debug("SelectionPanel::getItemIcon: Successfully loaded PRO for PID {}, FID: {}", pid, pro->header.FID);

        // Get FRM path from the FID
        // Use inventory FID instead of ground FID for inventory display
        uint32_t fid = pro->commonItemData.inventoryFID != 0 ? pro->commonItemData.inventoryFID : pro->header.FID;
        std::string frmPath = resourceManager.FIDtoFrmName(fid);
        spdlog::debug("SelectionPanel::getItemIcon: Using inventory FID {} for PID {}, FRM path: '{}'", fid, pid, frmPath);

        if (frmPath.empty()) {
            spdlog::warn("Empty FRM path for PID {} - returning placeholder", pid);
            return createPlaceholderIcon();
        }

        // Load FRM from ResourceManager
        auto frm = resourceManager.loadResource<Frm>(frmPath);
        if (!frm) {
            spdlog::warn("Failed to load FRM for path '{}' - returning placeholder", frmPath);
            return createPlaceholderIcon();
        }
        spdlog::debug("SelectionPanel::getItemIcon: Successfully loaded FRM for path '{}'", frmPath);

        // Get the first frame (direction 0, frame 0)
        const auto& directions = frm->directions();
        if (directions.empty() || directions[0].frames().empty()) {
            spdlog::warn("FRM has no frames for PID {}", pid);
            return createPlaceholderIcon();
        }

        const auto& frame = directions[0].frames()[0];

        // Load texture from ResourceManager
        const sf::Texture& texture = resourceManager.texture(frmPath);

        // Convert SFML texture to QPixmap and extract the first frame
        sf::Image image = texture.copyToImage();

        // Extract just the first frame based on frame dimensions
        QImage fullImage(reinterpret_cast<const uchar*>(image.getPixelsPtr()),
                        image.getSize().x, image.getSize().y, QImage::Format_RGBA8888);

        // Extract the first frame (assuming frames are laid out horizontally)
        QImage frameImage = fullImage.copy(0, 0, frame.width(), frame.height());

        // Convert to pixmap and create properly sized final icon
        QPixmap framePixmap = QPixmap::fromImage(frameImage);

        // Always create icon at exact ICON_SIZE to ensure consistency
        QPixmap finalIcon(ICON_SIZE, ICON_SIZE);
        finalIcon.fill(Qt::transparent);

        QPainter painter(&finalIcon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        // Scale and center the frame in the fixed-size icon
        QPixmap scaledPixmap = framePixmap.scaled(ICON_SIZE, ICON_SIZE,
                                                  Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation);
        int x = (ICON_SIZE - scaledPixmap.width()) / 2;
        int y = (ICON_SIZE - scaledPixmap.height()) / 2;
        painter.drawPixmap(x, y, scaledPixmap);

        spdlog::debug("SelectionPanel::getItemIcon: Successfully created icon for PID {}", pid);
        return finalIcon;

    } catch (const std::exception& e) {
        spdlog::warn("Failed to load icon for PID {}: {} - returning placeholder", pid, e.what());
        return createPlaceholderIcon();
    }
}

QPixmap SelectionPanel::createPlaceholderIcon() const {
    spdlog::debug("SelectionPanel::createPlaceholderIcon: Creating {}x{} placeholder icon", ICON_SIZE, ICON_SIZE);
    // Create consistently sized placeholder icon
    QPixmap placeholder(ICON_SIZE, ICON_SIZE);
    placeholder.fill(Qt::lightGray);

    QPainter painter(&placeholder);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::darkGray, 2));
    painter.drawRect(1, 1, ICON_SIZE-3, ICON_SIZE-3);

    // Draw question mark
    QFont font = painter.font();
    font.setPointSize(ICON_SIZE / 4);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(placeholder.rect(), Qt::AlignCenter, "?");

    return placeholder;
}

} // namespace geck
#include <QSize>
