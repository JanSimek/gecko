#include "SelectedTilePanel.h"

#include <QFormLayout>
#include <QPixmap>
#include <QApplication>
#include <spdlog/spdlog.h>
#include <cmath>

#include "../format/map/Map.h"
#include "../format/lst/Lst.h"
#include "../reader/lst/LstReader.h"
#include "../util/ResourceManager.h"

namespace geck {

SelectedTilePanel::SelectedTilePanel(QWidget* parent)
    : QWidget(parent)
    , _mainLayout(nullptr)
    , _scrollArea(nullptr)
    , _contentWidget(nullptr)
    , _contentLayout(nullptr)
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
    , _hasSelection(false)
    , _map(nullptr) {
    
    setupUI();
}

void SelectedTilePanel::setupUI() {
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
    
    // Tile information group
    _tileInfoGroup = new QGroupBox("Tile Information");
    QFormLayout* formLayout = new QFormLayout(_tileInfoGroup);
    
    // Tile preview display (placeholder for now)
    _tilePreviewLabel = new QLabel("No tile selected");
    _tilePreviewLabel->setAlignment(Qt::AlignCenter);
    _tilePreviewLabel->setMinimumHeight(96);
    _tilePreviewLabel->setMinimumWidth(128);
    _tilePreviewLabel->setMaximumHeight(96);
    _tilePreviewLabel->setMaximumWidth(128);
    _tilePreviewLabel->setScaledContents(false);
    _tilePreviewLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _tilePreviewLabel->setStyleSheet("border: 1px solid gray; background-color: #f0f0f0;");
    formLayout->addRow("Preview:", _tilePreviewLabel);
    
    // Tile type (Floor/Roof)
    _tileTypeEdit = new QLineEdit();
    _tileTypeEdit->setReadOnly(true);
    _tileTypeEdit->setPlaceholderText("No tile selected");
    formLayout->addRow("Type:", _tileTypeEdit);
    
    // Tile index
    _tileIndexSpin = new QSpinBox();
    _tileIndexSpin->setRange(0, Map::TILES_PER_ELEVATION - 1);
    _tileIndexSpin->setReadOnly(true);
    _tileIndexSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    formLayout->addRow("Tile Index:", _tileIndexSpin);
    
    // Elevation
    _elevationSpin = new QSpinBox();
    _elevationSpin->setRange(0, 1);
    _elevationSpin->setReadOnly(true);
    _elevationSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    formLayout->addRow("Elevation:", _elevationSpin);
    
    // Hex coordinates
    _hexXSpin = new QSpinBox();
    _hexXSpin->setRange(0, Map::COLS - 1);
    _hexXSpin->setReadOnly(true);
    _hexXSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    formLayout->addRow("Hex X:", _hexXSpin);
    
    _hexYSpin = new QSpinBox();
    _hexYSpin->setRange(0, Map::ROWS - 1);
    _hexYSpin->setReadOnly(true);
    _hexYSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    formLayout->addRow("Hex Y:", _hexYSpin);
    
    // World coordinates
    _worldXSpin = new QSpinBox();
    _worldXSpin->setRange(0, INT_MAX);
    _worldXSpin->setReadOnly(true);
    _worldXSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    formLayout->addRow("World X:", _worldXSpin);
    
    _worldYSpin = new QSpinBox();
    _worldYSpin->setRange(0, INT_MAX);
    _worldYSpin->setReadOnly(true);
    _worldYSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    formLayout->addRow("World Y:", _worldYSpin);
    
    // Tile information (shows either floor or roof depending on selection)
    _tileIdSpin = new QSpinBox();
    _tileIdSpin->setRange(0, UINT16_MAX);
    _tileIdSpin->setReadOnly(true);
    _tileIdSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    formLayout->addRow("Tile ID:", _tileIdSpin);
    
    _tileNameEdit = new QLineEdit();
    _tileNameEdit->setReadOnly(true);
    _tileNameEdit->setPlaceholderText("No tile selected");
    formLayout->addRow("Tile Name:", _tileNameEdit);
    
    _contentLayout->addWidget(_tileInfoGroup);
    _contentLayout->addStretch(); // Add stretch to push content to top
    
    _scrollArea->setWidget(_contentWidget);
    _mainLayout->addWidget(_scrollArea);
    
    // Initially clear the display
    clearTileInfo();
}

void SelectedTilePanel::selectTile(int tileIndex, int elevation, bool isRoof) {
    _selectedTileIndex = tileIndex;
    _selectedElevation = elevation;
    _isRoofSelected = isRoof;
    _hasSelection = true;
    
    updateTileInfo();
    
    spdlog::debug("SelectedTilePanel: Tile selected - index: {}, elevation: {}, isRoof: {}", 
                 tileIndex, elevation, isRoof);
}

void SelectedTilePanel::setMap(Map* map) {
    _map = map;
}

void SelectedTilePanel::clearSelection() {
    _hasSelection = false;
    clearTileInfo();
    spdlog::debug("SelectedTilePanel: Tile selection cleared");
}

void SelectedTilePanel::updateTileInfo() {
    if (!_hasSelection || !_map) {
        clearTileInfo();
        return;
    }
    
    try {
        // Get the actual tile data from the map
        auto& mapFile = _map->getMapFile();
        auto& tile = mapFile.tiles.at(_selectedElevation).at(_selectedTileIndex);
        
        // Calculate hex coordinates from tile index
        uint32_t hexX = static_cast<uint32_t>(std::ceil(static_cast<double>(_selectedTileIndex) / 100));
        uint32_t hexY = _selectedTileIndex % 100;
        
        // Calculate world coordinates (same calculation as in EditorState::loadTileSprites)
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
            LstReader lstReader;
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
                loadTilePreview(tilesList, _isRoofSelected ? roofTileId : floorTileId);
                
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

void SelectedTilePanel::clearTileInfo() {
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

void SelectedTilePanel::loadTilePreview(Lst* tilesList, uint16_t tileId) {
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
        const sf::Uint8* pixels = image.getPixelsPtr();
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

} // namespace geck