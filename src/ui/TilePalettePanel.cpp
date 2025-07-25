#include "TilePalettePanel.h"
#include "../format/map/Map.h"
#include "../format/lst/Lst.h"
#include "../util/ResourceManager.h"
#include "../util/Constants.h"
#include "../util/ColorUtils.h"

#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QStyle>
#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

namespace geck {

TileWidget::TileWidget(int tileIndex, const QPixmap& pixmap, QWidget* parent)
    : QLabel(parent)
    , _tileIndex(tileIndex) {

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
        painter.setPen(QPen(geck::ColorUtils::createSelectionBorderColor(), 3));
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
    }
}

TilePalettePanel::TilePalettePanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    setMinimumWidth(300);
}

void TilePalettePanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setSpacing(8);
    _mainLayout->setContentsMargins(8, 8, 8, 8);

    setupInteractionModeControls();
    setupModeControls();
    setupFilterControls();
    setupPaginationControls();
    setupTileGrid();

    // Status label
    _statusLabel = new QLabel("No tiles loaded", this);
    _statusLabel->setStyleSheet("color: gray; font-style: italic;");
    _mainLayout->addWidget(_statusLabel);

    _mainLayout->addStretch(); // Push everything to top
}

void TilePalettePanel::setupInteractionModeControls() {
    _interactionGroup = new QGroupBox("Interaction Mode", this);
    auto* interactionLayout = new QVBoxLayout(_interactionGroup);

    _interactionButtonGroup = new QButtonGroup(this);

    _selectionModeButton = new QPushButton("Selection Mode", this);
    _selectionModeButton->setCheckable(true);
    _selectionModeButton->setChecked(true);
    _selectionModeButton->setToolTip("Normal selection and editing mode (default)");

    _tilePaintingModeButton = new QPushButton("Tile Painting Mode", this);
    _tilePaintingModeButton->setCheckable(true);
    _tilePaintingModeButton->setToolTip("Paint tiles from palette onto the map");

    _interactionButtonGroup->addButton(_selectionModeButton, static_cast<int>(InteractionMode::SELECTION));
    _interactionButtonGroup->addButton(_tilePaintingModeButton, static_cast<int>(InteractionMode::TILE_PAINTING));

    interactionLayout->addWidget(_selectionModeButton);
    interactionLayout->addWidget(_tilePaintingModeButton);

    connect(_interactionButtonGroup, &QButtonGroup::idClicked,
        this, &TilePalettePanel::onInteractionModeChanged);

    _mainLayout->addWidget(_interactionGroup);
}

void TilePalettePanel::setupModeControls() {
    _modeGroup = new QGroupBox("Placement Mode", this);
    auto* modeLayout = new QVBoxLayout(_modeGroup);

    _placementModeLabel = new QLabel(this);
    _placementModeLabel->setText("• Single click: Place one tile\n• Click and drag: Fill area with tiles\n• Auto-replace selected tiles");
    _placementModeLabel->setStyleSheet("color: #666; font-size: 11px;");
    _placementModeLabel->setWordWrap(true);

    modeLayout->addWidget(_placementModeLabel);

    // Initially hide placement mode controls since we start in SELECTION mode
    _modeGroup->hide();

    _mainLayout->addWidget(_modeGroup);
}

void TilePalettePanel::setupFilterControls() {
    _filterGroup = new QGroupBox("Filter", this);
    auto* filterGroupLayout = new QVBoxLayout(_filterGroup);

    // Search field
    auto* searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:", this));
    _searchLineEdit = new QLineEdit(this);
    _searchLineEdit->setPlaceholderText("Enter tile filename...");
    _searchLineEdit->setClearButtonEnabled(true);
    searchLayout->addWidget(_searchLineEdit, 1);
    filterGroupLayout->addLayout(searchLayout);

    // Range filter
    auto* rangeLayout = new QHBoxLayout();
    rangeLayout->addWidget(new QLabel("Range:", this));
    rangeLayout->addWidget(new QLabel("Start:", this));
    _startTileSpinBox = new QSpinBox(this);
    _startTileSpinBox->setMinimum(0);
    _startTileSpinBox->setMaximum(9999);
    _startTileSpinBox->setValue(0);
    rangeLayout->addWidget(_startTileSpinBox);

    rangeLayout->addWidget(new QLabel("End:", this));
    _endTileSpinBox = new QSpinBox(this);
    _endTileSpinBox->setMinimum(-1); // -1 means show all
    _endTileSpinBox->setMaximum(9999);
    _endTileSpinBox->setValue(-1);
    _endTileSpinBox->setSpecialValueText("All");
    rangeLayout->addWidget(_endTileSpinBox);

    _showAllButton = new QPushButton("Show All", this);
    rangeLayout->addWidget(_showAllButton);

    filterGroupLayout->addLayout(rangeLayout);

    // Connect signals
    connect(_searchLineEdit, &QLineEdit::textChanged,
        this, &TilePalettePanel::onSearchTextChanged);
    connect(_startTileSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
        this, &TilePalettePanel::filterTiles);
    connect(_endTileSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
        this, &TilePalettePanel::filterTiles);
    connect(_showAllButton, &QPushButton::clicked, [this]() {
        _searchLineEdit->clear();
        _startTileSpinBox->setValue(0);
        _endTileSpinBox->setValue(-1);
        _currentPage = 0; // Reset to first page
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

void TilePalettePanel::setupPaginationControls() {
    _paginationGroup = new QGroupBox("Page Navigation", this);
    auto* paginationLayout = new QVBoxLayout(_paginationGroup);

    // Shared pagination widget with all controls
    _paginationWidget = new PaginationWidget(this);
    _paginationWidget->setShowFirstLastButtons(true);
    connect(_paginationWidget, &PaginationWidget::pageChanged, this, &TilePalettePanel::onPaginationPageChanged);
    paginationLayout->addWidget(_paginationWidget);

    _mainLayout->addWidget(_paginationGroup);
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

    // Calculate pagination for filtered tiles
    calculatePagination();
    
    // Skip to the current page within filtered results
    int filteredIndex = 0;
    int targetStartIndex = _currentPage * TILES_PER_PAGE;
    int targetEndIndex = targetStartIndex + TILES_PER_PAGE - 1;

    int row = 0;
    int col = 0;
    int tilesLoaded = 0;

    for (int i = startIndex; i <= endIndex && i < static_cast<int>(tiles.size()); ++i) {
        const std::string& tileName = tiles[i];

        // Skip reserved.frm and grid000.frm (first two tiles) like in original implementation
        if (i < 2) {
            continue;
        }

        // Apply search filter if set
        if (!_searchText.isEmpty()) {
            QString tileNameQ = QString::fromStdString(tileName);
            if (!tileNameQ.contains(_searchText, Qt::CaseInsensitive)) {
                continue; // Skip tiles that don't match search
            }
        }
        
        // Check if this filtered tile is in the current page range
        if (filteredIndex < targetStartIndex) {
            filteredIndex++;
            continue; // Skip tiles before current page
        }
        if (filteredIndex > targetEndIndex) {
            break; // Stop loading tiles beyond current page
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
                tilePixmap.fill(geck::ColorUtils::createTilePlaceholderColor(i));

                // Draw tile index and filename
                QPainter painter(&tilePixmap);
                painter.setPen(Qt::white);
                painter.setFont(QFont("Arial", 8));
                painter.drawText(QRect(0, 0, TileWidget::TILE_SIZE, TileWidget::TILE_SIZE / 2),
                    Qt::AlignCenter, QString::number(i));
                painter.setFont(QFont("Arial", 6));
                painter.drawText(QRect(0, TileWidget::TILE_SIZE / 2, TileWidget::TILE_SIZE, TileWidget::TILE_SIZE / 2),
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
        
        filteredIndex++; // Move to next filtered tile
    }

    // Update status with pagination info
    QString statusText;
    if (_totalPages > 0) {
        if (!_searchText.isEmpty()) {
            statusText = QString("Page %1 of %2 - Found %3 tiles matching '%4' (showing %5 tiles)")
                             .arg(_currentPage + 1)
                             .arg(_totalPages)
                             .arg(_totalFilteredTiles)
                             .arg(_searchText)
                             .arg(tilesLoaded);
        } else {
            int rangeStart = targetStartIndex + 1; // Convert to 1-based
            int rangeEnd = std::min(targetStartIndex + tilesLoaded, _totalFilteredTiles);
            statusText = QString("Page %1 of %2 - Showing %3 tiles (tiles %4-%5)")
                             .arg(_currentPage + 1)
                             .arg(_totalPages)
                             .arg(tilesLoaded)
                             .arg(rangeStart)
                             .arg(rangeEnd);
        }
    } else {
        statusText = "No tiles to display";
    }
    _statusLabel->setText(statusText);
    
    updatePaginationControls();

    spdlog::info("TilePalettePanel: Loaded {} tile widgets", tilesLoaded);
}

void TilePalettePanel::filterTiles() {
    _filterStart = _startTileSpinBox->value();
    _filterEnd = _endTileSpinBox->value();
    
    // Reset to first page when filter changes
    _currentPage = 0;
    calculatePagination();
    updateTileGrid();
}

void TilePalettePanel::onSearchTextChanged(const QString& text) {
    _searchText = text.trimmed();
    
    // Reset to first page when search changes
    _currentPage = 0;
    calculatePagination();
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

    // Only emit signals for tile operations when in TILE_PAINTING mode
    if (_interactionMode == InteractionMode::TILE_PAINTING) {
        // First check if there are selected tiles - if so, replace them automatically
        // This signal will always be emitted to let EditorWidget decide whether to replace or enable placement
        emit tileSelected(tileIndex);

        // Also emit the replacement signal - EditorWidget will handle whether tiles are actually selected
        emit replaceSelectedTiles(tileIndex);
    }

    spdlog::debug("TilePalettePanel: Selected tile {} in mode {}", tileIndex, static_cast<int>(_placementMode));
}

void TilePalettePanel::clearTileSelection() {
    for (auto& tileWidget : _tileWidgets) {
        tileWidget->setSelected(false);
    }
}

void TilePalettePanel::onPlacementModeChanged() {
    // With unified placement mode, this function is simplified
    emit placementModeChanged(_placementMode);

    QString modeText = "Unified placement mode - click or drag to place tiles (auto-replace if tiles selected)";
    _statusLabel->setText(modeText);
    spdlog::debug("TilePalettePanel: Using unified placement mode");
}

void TilePalettePanel::setPlacementMode(PlacementMode mode) {
    if (_placementMode != mode) {
        _placementMode = mode;

        // With unified placement mode, no button state to update
        emit placementModeChanged(_placementMode);
    }
}

void TilePalettePanel::onInteractionModeChanged() {
    int modeId = _interactionButtonGroup->checkedId();
    if (modeId >= 0) {
        _interactionMode = static_cast<InteractionMode>(modeId);
        emit interactionModeChanged(_interactionMode);

        // Show/hide placement mode controls based on interaction mode
        if (_interactionMode == InteractionMode::TILE_PAINTING) {
            _modeGroup->show();
            _statusLabel->setText("Tile painting mode - select a tile and click/drag to paint");

            // If we already have a tile selected, re-emit the signal to enable tile placement
            if (_selectedTileIndex >= 0) {
                emit tileSelected(_selectedTileIndex);
                spdlog::debug("TilePalettePanel: Re-emitting selected tile {} for painting mode", _selectedTileIndex);
            }
        } else {
            _modeGroup->hide();
            _statusLabel->setText("Selection mode - normal editing behavior");

            // When switching to selection mode, disable tile placement if active
            emit tileSelected(-1); // -1 signals disable tile placement mode
        }

        spdlog::debug("TilePalettePanel: Changed to interaction mode {}", static_cast<int>(_interactionMode));
    }
}

void TilePalettePanel::setInteractionMode(InteractionMode mode) {
    if (_interactionMode != mode) {
        _interactionMode = mode;

        // Update button selection
        auto* button = qobject_cast<QPushButton*>(_interactionButtonGroup->button(static_cast<int>(mode)));
        if (button) {
            button->setChecked(true);
        }

        // Trigger the UI update
        onInteractionModeChanged();

        emit interactionModeChanged(_interactionMode);
    }
}

void TilePalettePanel::calculatePagination() {
    if (!_tileList) {
        _totalPages = 0;
        _totalFilteredTiles = 0;
        return;
    }
    
    const auto& tiles = _tileList->list();
    int startIndex = _filterStart;
    int endIndex = (_filterEnd >= 0) ? std::min(_filterEnd, static_cast<int>(tiles.size()) - 1)
                                     : static_cast<int>(tiles.size()) - 1;
    
    // Count tiles that match current filters
    int filteredCount = 0;
    for (int i = startIndex; i <= endIndex && i < static_cast<int>(tiles.size()); ++i) {
        // Skip reserved tiles
        if (i < 2) continue;
        
        // Apply search filter if set
        if (!_searchText.isEmpty()) {
            const std::string& tileName = tiles[i];
            QString tileNameQ = QString::fromStdString(tileName);
            if (!tileNameQ.contains(_searchText, Qt::CaseInsensitive)) {
                continue;
            }
        }
        filteredCount++;
    }
    
    _totalFilteredTiles = filteredCount;
    _totalPages = (filteredCount + TILES_PER_PAGE - 1) / TILES_PER_PAGE; // Ceiling division
    
    // Ensure current page is valid
    if (_currentPage >= _totalPages) {
        _currentPage = std::max(0, _totalPages - 1);
    }
    
    spdlog::debug("Pagination calculated: {} filtered tiles, {} pages, current page {}", 
                  _totalFilteredTiles, _totalPages, _currentPage + 1);
}

void TilePalettePanel::updatePaginationControls() {
    if (_totalPages <= 1) {
        _paginationGroup->hide();
        return;
    }
    
    _paginationGroup->show();
    
    // Update shared pagination widget
    _paginationWidget->setTotalPages(_totalPages);
    _paginationWidget->setCurrentPage(_currentPage + 1); // Convert to 1-based
    _paginationWidget->setEnabled(_totalPages > 1);
}


void TilePalettePanel::onPaginationPageChanged(int page) {
    int newPage = page - 1; // Convert from 1-based to 0-based
    if (newPage != _currentPage && newPage >= 0 && newPage < _totalPages) {
        _currentPage = newPage;
        updateTileGrid();
    }
}

} // namespace geck
