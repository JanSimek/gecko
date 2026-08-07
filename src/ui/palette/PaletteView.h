#pragma once

#include <QListView>

namespace geck::ui {

/**
 * @brief The grid a palette shows its items in.
 *
 * A QListView in IconMode - items flow left to right and wrap - configured the same way for every
 * palette, and dragging without a drag image: the editor already draws the item under the cursor
 * on the map, so the view's own would be a second, larger copy of it.
 */
class PaletteView : public QListView {
    Q_OBJECT

public:
    explicit PaletteView(int iconSize, QWidget* parent = nullptr);

    /// Makes room under each icon for a one-line caption. The model decides whether a row actually
    /// has one (PaletteModel::setShowLabels); this only sizes the cell, and a cell sized for the
    /// icon alone would clip the caption into the row below.
    void setShowLabels(bool show);

protected:
    void startDrag(Qt::DropActions supportedActions) override;

private:
    void applyGridSize();

    int _iconSize;
    bool _showLabels = false;
};

} // namespace geck::ui
