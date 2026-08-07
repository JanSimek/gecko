#include "ui/palette/PaletteView.h"

#include "ui/theme/ThemeManager.h"

#include <QDrag>
#include <QMimeData>

namespace geck::ui {

PaletteView::PaletteView(int iconSize, QWidget* parent)
    : QListView(parent)
    , _iconSize(iconSize) {
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust); // reflows the columns on resize
    setUniformItemSizes(true);        // fixed cells let the view lay out without measuring
    setMovement(QListView::Static);
    setIconSize(QSize(iconSize, iconSize));
    setSelectionMode(QAbstractItemView::SingleSelection);
    // One line, elided: names run to "Vault 13 Outer Door", and wrapping them would make the row
    // height depend on the longest caption on screen. The full name stays in the tooltip.
    setWordWrap(false);
    applyGridSize();
}

void PaletteView::setShowLabels(bool show) {
    if (_showLabels == show) {
        return;
    }
    _showLabels = show;
    applyGridSize();
}

void PaletteView::applyGridSize() {
    const int padding = constants::SPACING_GRID * 2;
    const int caption = _showLabels ? fontMetrics().height() : 0;
    setGridSize(QSize(_iconSize + padding, _iconSize + padding + caption));
}

void PaletteView::startDrag(Qt::DropActions supportedActions) {
    const QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty() || model() == nullptr) {
        return;
    }

    QMimeData* data = model()->mimeData(indexes);
    if (data == nullptr) {
        return;
    }

    // No drag pixmap on purpose: the editor renders the item on the map under the cursor, and the
    // view's default image would sit beside it as a larger duplicate.
    auto* drag = new QDrag(this);
    drag->setMimeData(data);
    drag->exec(supportedActions, Qt::CopyAction);
}

} // namespace geck::ui
