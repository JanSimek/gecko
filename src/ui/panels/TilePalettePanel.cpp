#include "TilePalettePanel.h"
#include "format/map/Map.h"
#include "format/lst/Lst.h"
#include "resource/GameResources.h"
#include "util/Constants.h"
#include "util/ColorUtils.h"
#include "ui/common/BaseWidget.h"
#include "ui/theme/ThemeManager.h"
#include "ui/UIConstants.h"
#include "selection/SelectionManager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QStyle>
#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

namespace geck {

TileWidget::TileWidget(int tileIndex, const QPixmap& pixmap, QWidget* parent)
    : BasePaletteWidget(tileIndex, parent) {

    QPixmap scaledPixmap = BaseWidget::scalePixmapToSize(pixmap, TILE_SIZE);
    setPixmap(scaledPixmap);

    setupCommonProperties(TILE_SIZE);
    setToolTip(QString("Tile %1").arg(tileIndex));

    connect(this, &BasePaletteWidget::clicked, this, [this](int index) {
        Q_EMIT tileClicked(index);
    });
}

TilePalettePanel::TilePalettePanel(resource::GameResources& resources, QWidget* parent)
    : GridPalettePanel("Tiles", parent)
    , _resources(resources) {
    setupUI();
    setMinimumWidth(ui::constants::sizes::WIDTH_PANEL_MIN);
}

void TilePalettePanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setSpacing(ui::constants::SPACING_NORMAL);
    _mainLayout->setContentsMargins(ui::constants::PANEL_MARGIN, ui::constants::PANEL_MARGIN, ui::constants::PANEL_MARGIN, ui::constants::PANEL_MARGIN);

    setupModeControls();
    setupFilterControls();
    setupPaginationControls();
    setupTileGrid();

    _statusLabel = new QLabel("No tiles loaded", this);
    _statusLabel->setStyleSheet(ui::theme::styles::italicSecondaryText());
    _mainLayout->addWidget(_statusLabel);

    _mainLayout->addStretch();
}

void TilePalettePanel::setupModeControls() {
    _modeGroup = new QGroupBox("Tile Painting", this);
    auto* modeLayout = new QVBoxLayout(_modeGroup);

    // Floor/Roof mode selection
    auto* layerModeLayout = new QHBoxLayout();
    layerModeLayout->addWidget(new QLabel("Layer:", this));

    _floorModeButton = new QRadioButton("Floor", this);
    _roofModeButton = new QRadioButton("Roof", this);
    _floorModeButton->setChecked(true); // Default to floor mode

    layerModeLayout->addWidget(_floorModeButton);
    layerModeLayout->addWidget(_roofModeButton);
    layerModeLayout->addStretch();

    modeLayout->addLayout(layerModeLayout);

    _placementModeLabel = new QLabel(this);
    _placementModeLabel->setText("Select a tile to paint:\n• Single click: Place one tile\n• Click and drag: Fill area with tiles\n• Auto-replace selected tiles\n• Escape or click selected tile to deselect");
    _placementModeLabel->setStyleSheet(ui::theme::styles::smallLabel());
    _placementModeLabel->setWordWrap(true);

    modeLayout->addWidget(_placementModeLabel);

    connect(_floorModeButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            setRoofMode(false);
        }
    });

    connect(_roofModeButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            setRoofMode(true);
        }
    });

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
    setupGridArea();
    _mainLayout->addWidget(scrollArea(), 1);
}

void TilePalettePanel::setupPaginationControls() {
    _paginationGroup = new QGroupBox("Page Navigation", this);
    auto* paginationLayout = new QVBoxLayout(_paginationGroup);

    _paginationWidget = new PaginationWidget(this);
    _paginationWidget->setShowFirstLastButtons(true);
    connect(_paginationWidget, &PaginationWidget::pageChanged,
        this, &TilePalettePanel::onGridPaginationPageChanged);
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

    int maxTiles = static_cast<int>(tileList->list().size()) - 1;
    _startTileSpinBox->setMaximum(maxTiles);
    _endTileSpinBox->setMaximum(maxTiles);

    updateTileGrid();
}

void TilePalettePanel::updateTileGrid() {
    if (!_tileList) {
        return;
    }

    int newColumnsPerRow = calculateOptimalColumnsPerRow(TileWidget::TILE_SIZE);
    if (newColumnsPerRow != _tilesPerRow) {
        _tilesPerRow = newColumnsPerRow;
        _previousColumnsPerRow = newColumnsPerRow;
    }

    _tileWidgets.clear();
    clearGridWidgets();

    const auto& tiles = _tileList->list();
    int startIndex = _filterStart;
    int endIndex = (_filterEnd >= 0) ? std::min(_filterEnd, static_cast<int>(tiles.size()) - 1)
                                     : static_cast<int>(tiles.size()) - 1;

    calculatePagination();

    int filteredIndex = 0;
    int targetStartIndex = getPageStartIndex();
    int targetEndIndex = getPageEndIndex();

    int row = 0;
    int col = 0;
    int tilesLoaded = 0;

    for (int i = startIndex; i <= endIndex && i < static_cast<int>(tiles.size()); ++i) {
        const std::string& tileName = tiles[i];

        // Skip reserved.frm and grid000.frm (first two tiles) like in original implementation
        if (i < 2) {
            continue;
        }

        if (!_searchText.isEmpty()) {
            QString tileNameQ = QString::fromStdString(tileName);
            if (!tileNameQ.contains(_searchText, Qt::CaseInsensitive)) {
                continue;
            }
        }

        // Only build widgets for tiles within the current page range.
        if (filteredIndex < targetStartIndex) {
            filteredIndex++;
            continue;
        }
        if (filteredIndex > targetEndIndex) {
            break;
        }

        try {
            std::string tilePath = "art/tiles/" + tileName;

            QPixmap tilePixmap;

            try {
                const auto& texture = _resources.textures().get(tilePath);

                sf::Vector2u textureSize = texture.getSize();
                sf::Image image = texture.copyToImage();

                QImage qImage(image.getPixelsPtr(), textureSize.x, textureSize.y, QImage::Format_RGBA8888);
                tilePixmap = QPixmap::fromImage(qImage);

                spdlog::debug("TilePalettePanel: Loaded texture for tile {} ({}x{})", i, textureSize.x, textureSize.y);

            } catch (const std::exception& e) {
                // Fall back to a generated placeholder when texture loading fails.
                spdlog::debug("TilePalettePanel: Using placeholder for tile {}: {}", i, e.what());

                tilePixmap = QPixmap(TileWidget::TILE_SIZE, TileWidget::TILE_SIZE);
                // Deterministic pseudo-random placeholder tint keyed on the tile index.
                tilePixmap.fill(QColor(100 + (i % 156), 100 + ((i * 7) % 156), 100 + ((i * 13) % 156)));

                QPainter painter(&tilePixmap);
                painter.setPen(ui::theme::colors::textLight());
                painter.setFont(ui::theme::fonts::compact());
                painter.drawText(QRect(0, 0, TileWidget::TILE_SIZE, TileWidget::TILE_SIZE / 2),
                    Qt::AlignCenter, QString::number(i));
                painter.setFont(ui::theme::fonts::tiny());
                painter.drawText(QRect(0, TileWidget::TILE_SIZE / 2, TileWidget::TILE_SIZE, TileWidget::TILE_SIZE / 2),
                    Qt::AlignCenter, QString::fromStdString(tileName).left(8));
            }

            auto tileWidget = std::make_unique<TileWidget>(i, tilePixmap, gridWidget());

            tileWidget->setToolTip(QString("Tile #%1\nFile: %2").arg(i).arg(QString::fromStdString(tileName)));

            connect(tileWidget.get(), &TileWidget::tileClicked,
                this, &TilePalettePanel::onTileClicked);

            gridLayout()->addWidget(tileWidget.get(), row, col);
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

        filteredIndex++;
    }

    QString statusText;
    if (totalPages() > 0) {
        if (!_searchText.isEmpty()) {
            statusText = QString("Page %1 of %2 - Found %3 tiles matching '%4' (showing %5 tiles)")
                             .arg(currentPage() + 1)
                             .arg(totalPages())
                             .arg(totalFilteredItems())
                             .arg(_searchText)
                             .arg(tilesLoaded);
        } else {
            int rangeStart = targetStartIndex + 1; // Convert to 1-based
            int rangeEnd = std::min(targetStartIndex + tilesLoaded, totalFilteredItems());
            statusText = QString("Page %1 of %2 - Showing %3 tiles (tiles %4-%5)")
                             .arg(currentPage() + 1)
                             .arg(totalPages())
                             .arg(tilesLoaded)
                             .arg(rangeStart)
                             .arg(rangeEnd);
        }
    } else {
        statusText = "No tiles to display";
    }
    _statusLabel->setText(statusText);

    GridPalettePanel::updatePaginationControls();

    spdlog::info("TilePalettePanel: Loaded {} tile widgets", tilesLoaded);
}

void TilePalettePanel::filterTiles() {
    _filterStart = _startTileSpinBox->value();
    _filterEnd = _endTileSpinBox->value();

    // Reset to first page when filter changes
    _currentPage = 0;
    updateTileGrid();
}

void TilePalettePanel::onSearchTextChanged(const QString& text) {
    _searchText = text.trimmed();

    // Reset to first page when search changes
    _currentPage = 0;
    updateTileGrid();
}

void TilePalettePanel::onTileClicked(int tileIndex) {
    // Clicking the already-selected tile deselects it.
    if (_selectedTileIndex == tileIndex) {
        clearTileSelection();
        _selectedTileIndex = -1;

        Q_EMIT tileSelected(-1, _isRoofMode); // -1 signals no tile selected
        return;
    }

    clearTileSelection();

    _selectedTileIndex = tileIndex;

    auto it = std::ranges::find_if(_tileWidgets,
        [tileIndex](const auto& widget) { return widget->getTileIndex() == tileIndex; });
    if (it != _tileWidgets.end()) {
        (*it)->setSelected(true);
    }

    // Selecting a tile to paint clears any active map selection so clicks place tiles.
    bool hasExistingSelection = _selectionManager && _selectionManager->hasSelection();
    if (hasExistingSelection) {
        _selectionManager->clearSelection();
    }

    Q_EMIT tileSelected(tileIndex, _isRoofMode);
}

void TilePalettePanel::clearTileSelection() {
    std::ranges::for_each(_tileWidgets, [](auto& widget) {
        widget->setSelected(false);
    });
}

void TilePalettePanel::deselectTile() {
    if (_selectedTileIndex >= 0) {
        clearTileSelection();
        _selectedTileIndex = -1;
        Q_EMIT tileSelected(-1, _isRoofMode); // -1 signals no tile selected
    }
}

void TilePalettePanel::setRoofMode(bool isRoof) {
    if (_isRoofMode != isRoof) {
        _isRoofMode = isRoof;

        _floorModeButton->setChecked(!isRoof);
        _roofModeButton->setChecked(isRoof);

        // Re-emit so a currently selected tile picks up the new roof/floor state.
        if (_selectedTileIndex >= 0) {
            Q_EMIT tileSelected(_selectedTileIndex, _isRoofMode);
        }
    }
}

void TilePalettePanel::onPlacementModeChanged() {
    // With unified placement mode, this function is simplified
    Q_EMIT placementModeChanged(_placementMode);

    QString modeText = "Unified placement mode - click or drag to place tiles (auto-replace if tiles selected)";
    _statusLabel->setText(modeText);
}

void TilePalettePanel::setPlacementMode(PlacementMode mode) {
    if (_placementMode != mode) {
        _placementMode = mode;

        // With unified placement mode, no button state to update
        Q_EMIT placementModeChanged(_placementMode);
    }
}

void TilePalettePanel::calculatePagination() {
    if (!_tileList) {
        updatePaginationState(0);
        return;
    }

    const auto& tiles = _tileList->list();
    int startIndex = _filterStart;
    int endIndex = (_filterEnd >= 0) ? std::min(_filterEnd, static_cast<int>(tiles.size()) - 1)
                                     : static_cast<int>(tiles.size()) - 1;

    int filteredCount = 0;
    for (int i = startIndex; i <= endIndex && i < static_cast<int>(tiles.size()); ++i) {
        // Skip reserved.frm and grid000.frm (first two tiles).
        if (i < 2)
            continue;

        if (!_searchText.isEmpty()) {
            const std::string& tileName = tiles[i];
            QString tileNameQ = QString::fromStdString(tileName);
            if (!tileNameQ.contains(_searchText, Qt::CaseInsensitive)) {
                continue;
            }
        }
        filteredCount++;
    }

    updatePaginationState(filteredCount);
}

void TilePalettePanel::resizeEvent(QResizeEvent* event) {
    GridPalettePanel::resizeEvent(event);

    int newColumnsPerRow = calculateOptimalColumnsPerRow(TileWidget::TILE_SIZE);

    // Only rebuild when the column count actually changed.
    if (newColumnsPerRow != _previousColumnsPerRow) {
        _tilesPerRow = newColumnsPerRow;
        _previousColumnsPerRow = newColumnsPerRow;

        if (!_tileWidgets.empty()) {
            updateTileGrid();
        }
    }
}

} // namespace geck
