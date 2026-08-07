#include "ui/palette/PaletteView.h"

#include "ui/theme/ThemeManager.h"

#include <QDrag>
#include <QMimeData>

namespace geck::ui {

PaletteView::PaletteView(int iconSize, QWidget* parent)
    : QListView(parent) {
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust); // reflows the columns on resize
    setUniformItemSizes(true);        // fixed cells let the view lay out without measuring
    setMovement(QListView::Static);
    setIconSize(QSize(iconSize, iconSize));
    setGridSize(QSize(iconSize + constants::SPACING_GRID * 2, iconSize + constants::SPACING_GRID * 2));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setWordWrap(true);
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
