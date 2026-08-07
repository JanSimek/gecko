#include "TilePalettePanel.h"
#include "format/map/Map.h"
#include "format/lst/Lst.h"
#include "resource/GameResources.h"
#include "util/Constants.h"
#include "util/ColorUtils.h"
#include "ui/common/BaseWidget.h"
#include "ui/theme/ThemeManager.h"
#include "selection/SelectionManager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QStyle>
#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

namespace geck {

TilePalettePanel::TilePalettePanel(resource::GameResources& resources, QWidget* parent)
    : BasePanel("Tiles", parent)
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
    setupTileList();

    _statusLabel = new QLabel("No tiles loaded", this);
    _statusLabel->setStyleSheet(ui::theme::styles::italicSecondaryText());
    _mainLayout->addWidget(_statusLabel);
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
    });

    _mainLayout->addWidget(_filterGroup);
}

void TilePalettePanel::setupTileList() {
    _model = new ui::PaletteModel(this);
    _model->setIconProvider([this](const ui::PaletteItem& item) {
        return tilePixmap(item.engineIndex, item.label.toStdString());
    });

    // The search box filters through the proxy rather than by rebuilding the list.
    _filter = new QSortFilterProxyModel(this);
    _filter->setSourceModel(_model);
    _filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    _filter->setFilterRole(ui::PaletteModel::LabelRole); // the caption is hidden; filter the name

    _tileView = new ui::PaletteView(TILE_SIZE, this);
    _tileView->setModel(_filter);

    connect(_tileView->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& current, const QModelIndex&) {
            if (!current.isValid()) {
                return;
            }
            onTileClicked(current.data(ui::PaletteModel::EngineIndexRole).toInt());
        });

    _mainLayout->addWidget(_tileView, 1);
}

void TilePalettePanel::loadTiles(const Lst* tileList) {
    if (!tileList) {
        spdlog::error("TilePalettePanel: tileList is null");
        return;
    }

    _tileList = tileList;

    spdlog::debug("TilePalettePanel: Loading {} tiles", tileList->list().size());

    int maxTiles = static_cast<int>(tileList->list().size()) - 1;
    _startTileSpinBox->setMaximum(maxTiles);
    _endTileSpinBox->setMaximum(maxTiles);

    rebuildItems();
}

QPixmap TilePalettePanel::tilePixmap(int tileIndex, const std::string& tileName) const {
    try {
        const auto& texture = _resources.textures().get("art/tiles/" + tileName);
        const sf::Vector2u size = texture.getSize();
        const sf::Image image = texture.copyToImage();
        const QImage qImage(image.getPixelsPtr(), size.x, size.y, QImage::Format_RGBA8888);
        return BaseWidget::scalePixmapToSize(QPixmap::fromImage(qImage), TILE_SIZE);
    } catch (const std::exception& e) {
        spdlog::debug("TilePalettePanel: Using placeholder for tile {}: {}", tileIndex, e.what());
    }

    // Deterministic tint keyed on the index, so a missing tile still reads as itself.
    QPixmap placeholder(TILE_SIZE, TILE_SIZE);
    placeholder.fill(QColor(100 + (tileIndex % 156), 100 + ((tileIndex * 7) % 156), 100 + ((tileIndex * 13) % 156)));

    QPainter painter(&placeholder);
    painter.setPen(ui::theme::colors::textLight());
    painter.setFont(ui::theme::fonts::compact());
    painter.drawText(QRect(0, 0, TILE_SIZE, TILE_SIZE / 2), Qt::AlignCenter, QString::number(tileIndex));
    painter.setFont(ui::theme::fonts::tiny());
    painter.drawText(QRect(0, TILE_SIZE / 2, TILE_SIZE, TILE_SIZE / 2), Qt::AlignCenter,
        QString::fromStdString(tileName).left(8));
    return placeholder;
}

void TilePalettePanel::rebuildItems() {
    if (!_tileList || !_model) {
        return;
    }

    const auto& tiles = _tileList->list();
    const int last = (_filterEnd >= 0) ? std::min(_filterEnd, static_cast<int>(tiles.size()) - 1)
                                       : static_cast<int>(tiles.size()) - 1;

    std::vector<ui::PaletteItem> items;
    for (int i = std::max(_filterStart, 2); i <= last; ++i) { // 0 and 1 are reserved.frm / grid000.frm
        const QString name = QString::fromStdString(tiles[static_cast<size_t>(i)]);
        items.push_back({ i, name, QString("Tile #%1\nFile: %2").arg(i).arg(name) });
    }

    _model->setItems(std::move(items));
    selectRowForTile(_selectedTileIndex);
    updateStatusLabel();
}

void TilePalettePanel::updateStatusLabel() {
    if (!_statusLabel || !_filter) {
        return;
    }

    const int shown = _filter->rowCount();
    const int total = _model ? _model->rowCount() : 0;
    _statusLabel->setText(shown == total ? QString("%1 tiles").arg(total)
                                         : QString("%1 of %2 tiles").arg(shown).arg(total));
}

void TilePalettePanel::selectRowForTile(int tileId) {
    if (!_tileView || !_model || tileId < 0) {
        return;
    }

    const int row = _model->rowForEngineIndex(tileId);
    if (row < 0) {
        return;
    }

    // Through the proxy: the row is only selectable while the current filter shows it.
    const QModelIndex mapped = _filter->mapFromSource(_model->index(row, 0));
    if (!mapped.isValid()) {
        return;
    }

    QSignalBlocker blocker(_tileView->selectionModel());
    _tileView->setCurrentIndex(mapped);
    _tileView->scrollTo(mapped, QAbstractItemView::PositionAtCenter);
}

void TilePalettePanel::filterTiles() {
    _filterStart = _startTileSpinBox->value();
    _filterEnd = _endTileSpinBox->value();
    rebuildItems();
}

void TilePalettePanel::onSearchTextChanged(const QString& text) {
    _filter->setFilterFixedString(text.trimmed());
    selectRowForTile(_selectedTileIndex);
    updateStatusLabel();
}

void TilePalettePanel::onTileClicked(int tileIndex) {
    // Clicking the already-selected tile deselects it.
    if (_selectedTileIndex == tileIndex) {
        clearTileSelection();
        _selectedTileIndex = -1;

        Q_EMIT tileSelected(-1, _isRoofMode); // -1 signals no tile selected
        return;
    }

    // No selectRowForTile() here: this runs from the view's own currentChanged, so the row is
    // already current, and re-applying it would scrollTo(PositionAtCenter) and recentre the grid
    // under the still-pressed cursor - the click then landed on whatever item the scroll brought
    // under the mouse. Programmatic callers sync the view themselves.
    _selectedTileIndex = tileIndex;

    // Selecting a tile to paint clears any active map selection so clicks place tiles.
    bool hasExistingSelection = _selectionManager && _selectionManager->hasSelection();
    if (hasExistingSelection) {
        _selectionManager->clearSelection();
    }

    Q_EMIT tileSelected(tileIndex, _isRoofMode);
}

void TilePalettePanel::clearTileSelection() {
    if (_tileView == nullptr) {
        return;
    }
    QSignalBlocker blocker(_tileView->selectionModel());
    _tileView->clearSelection();
    _tileView->setCurrentIndex({});
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

void TilePalettePanel::pickTile(int tileId, bool isRoof) {
    // Match the picked tile's layer so paint lands on floor vs roof. Set the state directly rather
    // than via setRoofMode() to avoid it re-emitting for the previously selected tile.
    _isRoofMode = isRoof;
    if (_floorModeButton && _roofModeButton) {
        _floorModeButton->setChecked(!isRoof);
        _roofModeButton->setChecked(isRoof);
    }

    // Clear any active filter so the picked tile is part of the visible set (mirrors "Show All").
    // Each of these rebuilds the grid; that is fine for a one-shot pick.
    if (_searchLineEdit) {
        _searchLineEdit->clear();
    }
    if (_startTileSpinBox) {
        _startTileSpinBox->setValue(0);
    }
    if (_endTileSpinBox) {
        _endTileSpinBox->setValue(-1);
    }

    // MainWindow's tileSelected() connection arms PlaceTile with this tile, so pressing P loads the
    // brush with it. The list holds every tile, so the highlight always lands and scrolls into view.
    _selectedTileIndex = tileId;
    selectRowForTile(tileId);

    Q_EMIT tileSelected(tileId, isRoof);
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

} // namespace geck
