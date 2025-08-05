#include "SelectionPanel.h"
#include "../dialogs/FrmSelectorDialog.h"

#include <QFormLayout>
#include <QPixmap>
#include <QApplication>
#include <QTimer>
#include <spdlog/spdlog.h>
#include <cmath>

#include "../../format/map/Map.h"
#include "../../format/lst/Lst.h"
#include "../../util/ResourceManager.h"
#include "../../util/ProHelper.h"
#include "../../format/map/MapObject.h"

namespace geck {

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
    , _selectedTileIndex(-1)
    , _selectedElevation(-1)
    , _isRoofSelected(false)
    , _hasTileSelection(false)
    , _map(nullptr) {

    setupUI();
}

void SelectionPanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(5, 5, 5, 5);

    // Create scroll area for content
    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    _contentWidget = new QWidget();
    _contentLayout = new QVBoxLayout(_contentWidget);
    _contentLayout->setContentsMargins(5, 5, 5, 5);

    // Create stacked widget to switch between object and tile panels
    _stackedWidget = new QStackedWidget();

    // Setup Object Panel
    _objectPanelWidget = new QWidget();
    QVBoxLayout* objectLayout = new QVBoxLayout(_objectPanelWidget);

    _objectInfoGroup = new QGroupBox("Object Information");
    QFormLayout* objectFormLayout = new QFormLayout(_objectInfoGroup);

    // Object sprite display
    _objectSpriteLabel = new QLabel("No object selected");
    _objectSpriteLabel->setAlignment(Qt::AlignCenter);
    _objectSpriteLabel->setMinimumHeight(128);
    _objectSpriteLabel->setMinimumWidth(128);
    _objectSpriteLabel->setMaximumHeight(128);
    _objectSpriteLabel->setMaximumWidth(128);
    _objectSpriteLabel->setScaledContents(false);
    _objectSpriteLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _objectSpriteLabel->setStyleSheet("border: 1px solid gray; background-color: #f0f0f0;");
    objectFormLayout->addRow("Sprite:", _objectSpriteLabel);

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
    
    // Change FRM button
    _changeFrmButton = new QPushButton("Change FRM...");
    _changeFrmButton->setEnabled(false);
    connect(_changeFrmButton, &QPushButton::clicked, this, &SelectionPanel::onChangeFrmClicked);
    objectFormLayout->addRow("", _changeFrmButton);
    
    // Edit PRO button
    _editProButton = new QPushButton("Edit PRO...");
    _editProButton->setEnabled(false);
    connect(_editProButton, &QPushButton::clicked, this, &SelectionPanel::onEditProClicked);
    objectFormLayout->addRow("", _editProButton);
    
    // Edit Exit Grid button
    _editExitGridButton = new QPushButton("Edit Exit Grid...");
    _editExitGridButton->setEnabled(false);
    _editExitGridButton->setVisible(false); // Hidden by default
    connect(_editExitGridButton, &QPushButton::clicked, this, &SelectionPanel::onEditExitGridClicked);
    objectFormLayout->addRow("", _editExitGridButton);

    objectLayout->addWidget(_objectInfoGroup);
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
            
            // Enable the change FRM button
            _changeFrmButton->setEnabled(true);
            
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

                _objectSpriteLabel->setPixmap(pixmap);
                _objectSpriteLabel->setText("");
            } else {
                _objectSpriteLabel->setText("Failed to convert sprite");
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
    
    _changeFrmButton->setEnabled(false);
    _editProButton->setEnabled(false);
    _editExitGridButton->setEnabled(false);
    _editExitGridButton->setVisible(false);

    _objectSpriteLabel->clear();
    _objectSpriteLabel->setText("No object selected");
    _objectInfoGroup->setTitle("Object Information");
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

} // namespace geck