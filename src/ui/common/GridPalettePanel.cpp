#include "GridPalettePanel.h"
#include "../theme/ThemeManager.h"
#include "../widgets/PaginationWidget.h"
#include <spdlog/spdlog.h>
#include <QResizeEvent>

namespace geck {

GridPalettePanel::GridPalettePanel(const QString& title, QWidget* parent)
    : BasePanel(title, parent) {
}

void GridPalettePanel::setupGridArea() {
    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    _gridWidget = new QWidget();
    _gridLayout = new QGridLayout(_gridWidget);
    _gridLayout->setSpacing(ui::constants::SPACING_GRID);
    _gridLayout->setContentsMargins(
        ui::constants::COMPACT_MARGIN,
        ui::constants::COMPACT_MARGIN,
        ui::constants::COMPACT_MARGIN,
        ui::constants::COMPACT_MARGIN);

    _scrollArea->setWidget(_gridWidget);

    // Note: Caller should add _scrollArea to their layout with appropriate stretch factor
}

void GridPalettePanel::clearGridWidgets() {
    if (!_gridLayout) {
        return;
    }

    QLayoutItem* item;
    while ((item = _gridLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }
}

void GridPalettePanel::updatePaginationState(int filteredCount) {
    _totalFilteredItems = filteredCount;
    _totalPages = (filteredCount + ui::constants::palette::ITEMS_PER_PAGE - 1)
                  / ui::constants::palette::ITEMS_PER_PAGE; // Ceiling division

    // Ensure current page is valid
    if (_currentPage >= _totalPages) {
        _currentPage = std::max(0, _totalPages - 1);
    }

    spdlog::debug("GridPalettePanel: Pagination updated - {} filtered items, {} pages, current page {}",
        _totalFilteredItems, _totalPages, _currentPage + 1);
}

void GridPalettePanel::updatePaginationControls() {
    if (!_paginationGroup || !_paginationWidget) {
        return;
    }

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

int GridPalettePanel::getPageStartIndex() const {
    return _currentPage * ui::constants::palette::ITEMS_PER_PAGE;
}

int GridPalettePanel::getPageEndIndex() const {
    return getPageStartIndex() + ui::constants::palette::ITEMS_PER_PAGE - 1;
}

int GridPalettePanel::calculateOptimalColumnsPerRow(int itemSize) const {
    if (!_scrollArea || !_scrollArea->viewport()) {
        return getDefaultColumnsPerRow();
    }

    // Get available width from the scroll area viewport
    int availableWidth = _scrollArea->viewport()->width();

    // Calculate space needed per item (widget size + margin)
    int itemWidth = itemSize + 4; // Item size + margin

    // Get spacing and margins from the grid layout
    int spacing = _gridLayout ? _gridLayout->spacing() : 2;
    int leftMargin = _gridLayout ? _gridLayout->contentsMargins().left() : 4;
    int rightMargin = _gridLayout ? _gridLayout->contentsMargins().right() : 4;

    // Calculate effective width available for items
    int effectiveWidth = availableWidth - leftMargin - rightMargin;

    // Calculate how many items can fit per row
    // Each item needs itemWidth + spacing, except the last one doesn't need spacing
    int columns = 1; // At least 1 column
    if (effectiveWidth >= itemWidth) {
        columns = (effectiveWidth + spacing) / (itemWidth + spacing);
    }

    // Apply reasonable bounds
    columns = std::max(1, std::min(columns, ui::constants::palette::MAX_ITEMS_PER_ROW));

    return columns;
}

void GridPalettePanel::onGridPaginationPageChanged(int page) {
    int newPage = page - 1; // Convert from 1-based to 0-based
    if (newPage != _currentPage && newPage >= 0 && newPage < _totalPages) {
        _currentPage = newPage;
        updateGrid();
    }
}

void GridPalettePanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // Subclasses should call this and then check if columns changed
    // to avoid unnecessary grid rebuilds
}

} // namespace geck
