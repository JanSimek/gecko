#include "TilePalettePanel.h"
#include "../format/map/Map.h"
#include "../format/lst/Lst.h"
#include "../util/ResourceManager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QStyle>
#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

namespace geck {

TileWidget::TileWidget(int tileIndex, const QPixmap& pixmap, QWidget* parent)
    : QLabel(parent), _tileIndex(tileIndex) {
    
    QPixmap scaledPixmap = pixmap.scaled(TILE_SIZE, TILE_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    setPixmap(scaledPixmap);
    
    setFixedSize(TILE_SIZE + 4, TILE_SIZE + 4); // Add border space
    setAlignment(Qt::AlignCenter);
    setFrameStyle(QFrame::Box);
    setToolTip(QString("Tile %1").arg(tileIndex));
    
    setCursor(Qt::PointingHandCursor);
}

void TileWidget::setSelected(bool selected) {
    if (_selected != selected) {
        _selected = selected;
        update(); // Trigger repaint
    }
}

void TileWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit tileClicked(_tileIndex);
    }
    QLabel::mousePressEvent(event);
}

void TileWidget::paintEvent(QPaintEvent* event) {
    QLabel::paintEvent(event);
    
    if (_selected) {
        QPainter painter(this);
        painter.setPen(QPen(QColor(255, 0, 0), 3)); // Red selection border
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
    }
}

// TilePalettePanel implementation
TilePalettePanel::TilePalettePanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    setMinimumWidth(300);
}

void TilePalettePanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setSpacing(8);
    _mainLayout->setContentsMargins(8, 8, 8, 8);
    
    setupModeControls();
    setupFilterControls();
    setupTileGrid();
    
    // Status label
    _statusLabel = new QLabel("No tiles loaded", this);
    _statusLabel->setStyleSheet("color: gray; font-style: italic;");
    _mainLayout->addWidget(_statusLabel);
    
    _mainLayout->addStretch(); // Push everything to top
}

void TilePalettePanel::setupModeControls() {
    _modeGroup = new QGroupBox("Placement Mode", this);
    auto* modeLayout = new QVBoxLayout(_modeGroup);
    
    _modeButtonGroup = new QButtonGroup(this);
    
    _singlePlacementButton = new QPushButton("Single Placement", this);
    _singlePlacementButton->setCheckable(true);
    _singlePlacementButton->setChecked(true);
    _singlePlacementButton->setToolTip("Click to place one tile at a time");
    
    _areaFillButton = new QPushButton("Area Fill", this);
    _areaFillButton->setCheckable(true);
    _areaFillButton->setToolTip("Draw selection box to fill area with tile");
    
    _replaceSelectedButton = new QPushButton("Replace Selected", this);
    _replaceSelectedButton->setCheckable(true);
    _replaceSelectedButton->setToolTip("Replace currently selected tiles with chosen tile");
    
    _modeButtonGroup->addButton(_singlePlacementButton, static_cast<int>(PlacementMode::SINGLE_PLACEMENT));
    _modeButtonGroup->addButton(_areaFillButton, static_cast<int>(PlacementMode::AREA_FILL));
    _modeButtonGroup->addButton(_replaceSelectedButton, static_cast<int>(PlacementMode::REPLACE_SELECTED));
    
    modeLayout->addWidget(_singlePlacementButton);
    modeLayout->addWidget(_areaFillButton);
    modeLayout->addWidget(_replaceSelectedButton);
    
    connect(_modeButtonGroup, &QButtonGroup::idClicked,
            this, &TilePalettePanel::onPlacementModeChanged);
    
    _mainLayout->addWidget(_modeGroup);
}


void TilePalettePanel::setupFilterControls() {
    _filterGroup = new QGroupBox("Tile Range", this);
    auto* filterLayout = new QHBoxLayout(_filterGroup);
    
    filterLayout->addWidget(new QLabel("Start:", this));
    _startTileSpinBox = new QSpinBox(this);
    _startTileSpinBox->setMinimum(0);
    _startTileSpinBox->setMaximum(9999);
    _startTileSpinBox->setValue(0);
    filterLayout->addWidget(_startTileSpinBox);
    
    filterLayout->addWidget(new QLabel("End:", this));
    _endTileSpinBox = new QSpinBox(this);
    _endTileSpinBox->setMinimum(-1); // -1 means show all
    _endTileSpinBox->setMaximum(9999);
    _endTileSpinBox->setValue(-1);
    _endTileSpinBox->setSpecialValueText("All");
    filterLayout->addWidget(_endTileSpinBox);
    
    _showAllButton = new QPushButton("Show All", this);
    filterLayout->addWidget(_showAllButton);
    
    connect(_startTileSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TilePalettePanel::filterTiles);
    connect(_endTileSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TilePalettePanel::filterTiles);
    connect(_showAllButton, &QPushButton::clicked, [this]() {
        _startTileSpinBox->setValue(0);
        _endTileSpinBox->setValue(-1);
    });
    
    _mainLayout->addWidget(_filterGroup);
}

void TilePalettePanel::setupTileGrid() {
    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    _tileGridWidget = new QWidget();
    _tileGridLayout = new QGridLayout(_tileGridWidget);
    _tileGridLayout->setSpacing(2);
    _tileGridLayout->setContentsMargins(4, 4, 4, 4);
    
    _scrollArea->setWidget(_tileGridWidget);
    _mainLayout->addWidget(_scrollArea, 1); // Take remaining space
}

void TilePalettePanel::loadTiles(const Lst* tileList) {
    if (!tileList) {
        spdlog::error("TilePalettePanel: tileList is null");
        return;
    }
    
    _tileList = tileList;
    
    spdlog::info("TilePalettePanel: Loading {} tiles", tileList->list().size());
    
    // Update filter range
    int maxTiles = static_cast<int>(tileList->list().size()) - 1;
    _startTileSpinBox->setMaximum(maxTiles);
    _endTileSpinBox->setMaximum(maxTiles);
    
    updateTileGrid();
}

void TilePalettePanel::updateTileGrid() {
    if (!_tileList) {
        return;
    }
    
    // Clear existing tiles
    _tileWidgets.clear();
    QLayoutItem* item;
    while ((item = _tileGridLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    
    const auto& tiles = _tileList->list();
    int startIndex = _filterStart;
    int endIndex = (_filterEnd >= 0) ? std::min(_filterEnd, static_cast<int>(tiles.size()) - 1) 
                                     : static_cast<int>(tiles.size()) - 1;
    
    // Limit tiles to prevent UI slowdown
    int tilesToLoad = std::min(endIndex - startIndex + 1, MAX_TILES_TO_LOAD);
    endIndex = startIndex + tilesToLoad - 1;
    
    int row = 0;
    int col = 0;
    int tilesLoaded = 0;
    
    for (int i = startIndex; i <= endIndex && i < static_cast<int>(tiles.size()); ++i) {
        const std::string& tileName = tiles[i];
        
        // Skip reserved.frm and grid000.frm (first two tiles) like in original implementation
        if (i < 2) {
            continue;
        }
        
        try {
            // Load tile texture through ResourceManager
            std::string tilePath = "art/tiles/" + tileName;
            
            QPixmap tilePixmap;
            
            try {
                // Try to get actual texture from ResourceManager
                auto& resourceManager = ResourceManager::getInstance();
                const auto& texture = resourceManager.texture(tilePath);
                
                // Convert SFML texture to QPixmap
                sf::Vector2u textureSize = texture.getSize();
                sf::Image image = texture.copyToImage();
                
                // Create QImage from SFML image data
                QImage qImage(image.getPixelsPtr(), textureSize.x, textureSize.y, QImage::Format_RGBA8888);
                tilePixmap = QPixmap::fromImage(qImage);
                
                spdlog::debug("TilePalettePanel: Loaded texture for tile {} ({}x{})", i, textureSize.x, textureSize.y);
                
            } catch (const std::exception& e) {
                // Fallback to placeholder if texture loading fails
                spdlog::debug("TilePalettePanel: Using placeholder for tile {}: {}", i, e.what());
                
                tilePixmap = QPixmap(TileWidget::TILE_SIZE, TileWidget::TILE_SIZE);
                tilePixmap.fill(QColor(100 + (i % 156), 100 + ((i * 7) % 156), 100 + ((i * 13) % 156)));
                
                // Draw tile index and filename
                QPainter painter(&tilePixmap);
                painter.setPen(Qt::white);
                painter.setFont(QFont("Arial", 8));
                painter.drawText(QRect(0, 0, TileWidget::TILE_SIZE, TileWidget::TILE_SIZE/2), 
                               Qt::AlignCenter, QString::number(i));
                painter.setFont(QFont("Arial", 6));
                painter.drawText(QRect(0, TileWidget::TILE_SIZE/2, TileWidget::TILE_SIZE, TileWidget::TILE_SIZE/2), 
                               Qt::AlignCenter, QString::fromStdString(tileName).left(8));
            }
            
            auto tileWidget = std::make_unique<TileWidget>(i, tilePixmap, _tileGridWidget);
            
            // Add tooltip with tile information (like in original implementation)
            tileWidget->setToolTip(QString("Tile #%1\nFile: %2").arg(i).arg(QString::fromStdString(tileName)));
            
            connect(tileWidget.get(), &TileWidget::tileClicked,
                    this, &TilePalettePanel::onTileClicked);
            
            _tileGridLayout->addWidget(tileWidget.get(), row, col);
            _tileWidgets.push_back(std::move(tileWidget));
            
            col++;
            if (col >= _tilesPerRow) {
                col = 0;
                row++;
            }
            
            tilesLoaded++;
        } catch (const std::exception& e) {
            spdlog::warn("TilePalettePanel: Failed to load tile {}: {}", tileName, e.what());
        }
    }
    
    // Update status
    _statusLabel->setText(QString("Loaded %1 tiles (showing %2-%3)")
                         .arg(tilesLoaded)
                         .arg(startIndex)
                         .arg(endIndex));
    
    spdlog::info("TilePalettePanel: Loaded {} tile widgets", tilesLoaded);
}

void TilePalettePanel::filterTiles() {
    _filterStart = _startTileSpinBox->value();
    _filterEnd = _endTileSpinBox->value();
    
    updateTileGrid();
}

void TilePalettePanel::onTileClicked(int tileIndex) {
    // Check if clicking the same tile again (deselect)
    if (_selectedTileIndex == tileIndex) {
        // Deselect the tile
        clearTileSelection();
        _selectedTileIndex = -1;
        
        // Disable tile placement mode
        emit tileSelected(-1); // -1 signals no tile selected
        
        spdlog::debug("TilePalettePanel: Deselected tile {}", tileIndex);
        return;
    }
    
    // Clear previous selection
    clearTileSelection();
    
    // Set new selection
    _selectedTileIndex = tileIndex;
    
    // Update visual selection
    for (auto& tileWidget : _tileWidgets) {
        if (tileWidget->getTileIndex() == tileIndex) {
            tileWidget->setSelected(true);
            break;
        }
    }
    
    // Handle different placement modes
    switch (_placementMode) {
        case PlacementMode::SINGLE_PLACEMENT:
        case PlacementMode::AREA_FILL:
            // For single placement and area fill, just emit tileSelected to enable placement mode
            emit tileSelected(tileIndex);
            break;
            
        case PlacementMode::REPLACE_SELECTED:
            // For replace mode, directly replace selected tiles
            // Note: The EditorWidget will handle checking if tiles are actually selected
            emit replaceSelectedTiles(tileIndex);
            break;
    }
    
    spdlog::debug("TilePalettePanel: Selected tile {} in mode {}", tileIndex, static_cast<int>(_placementMode));
}

void TilePalettePanel::clearTileSelection() {
    for (auto& tileWidget : _tileWidgets) {
        tileWidget->setSelected(false);
    }
}

void TilePalettePanel::onPlacementModeChanged() {
    int modeId = _modeButtonGroup->checkedId();
    if (modeId >= 0) {
        _placementMode = static_cast<PlacementMode>(modeId);
        emit placementModeChanged(_placementMode);
        
        QString modeText;
        switch (_placementMode) {
            case PlacementMode::SINGLE_PLACEMENT:
                modeText = "Single tile placement mode";
                break;
            case PlacementMode::AREA_FILL:
                modeText = "Area fill mode - draw selection box";
                break;
            case PlacementMode::REPLACE_SELECTED:
                modeText = "Replace selected tiles mode";
                break;
        }
        
        _statusLabel->setText(modeText);
        spdlog::debug("TilePalettePanel: Changed to placement mode {}", static_cast<int>(_placementMode));
    }
}

void TilePalettePanel::setPlacementMode(PlacementMode mode) {
    if (_placementMode != mode) {
        _placementMode = mode;
        
        // Update button selection
        auto* button = qobject_cast<QPushButton*>(_modeButtonGroup->button(static_cast<int>(mode)));
        if (button) {
            button->setChecked(true);
        }
        
        emit placementModeChanged(_placementMode);
    }
}

} // namespace geck

