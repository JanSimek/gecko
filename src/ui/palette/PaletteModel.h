#pragma once

#include <QAbstractListModel>
#include <QPixmap>
#include <QString>

#include <functional>
#include <unordered_map>
#include <vector>

namespace geck::ui {

/// One palette entry, as the engine identifies it plus what the view shows.
struct PaletteItem {
    int engineIndex = -1; ///< tiles.lst index, proto number, ... - what the panel emits on selection
    QString label;        ///< shown under the icon
    QString tooltip;
};

/**
 * @brief The items of a palette, as a model a QListView can render.
 *
 * The icon for a row is produced on demand and cached: a view asks only for the rows it is about
 * to paint, so a palette of several thousand entries costs the artwork of one screenful rather
 * than of a page, and needs no pagination to stay responsive.
 */
class PaletteModel : public QAbstractListModel {
    Q_OBJECT

public:
    /// Produces the icon for an item. Called once per item; the result is cached.
    using IconProvider = std::function<QPixmap(const PaletteItem&)>;

    /// The engine index of the item in a row, for callers holding a QModelIndex.
    static constexpr int EngineIndexRole = Qt::UserRole + 1;

    explicit PaletteModel(QObject* parent = nullptr);

    void setItems(std::vector<PaletteItem> items);
    void setIconProvider(IconProvider provider);

    [[nodiscard]] const PaletteItem* itemAt(int row) const;
    /// The row showing @p engineIndex, or -1. Linear: palettes are built once and searched rarely.
    [[nodiscard]] int rowForEngineIndex(int engineIndex) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    std::vector<PaletteItem> _items;
    IconProvider _iconProvider;
    mutable std::unordered_map<int, QPixmap> _iconCache; ///< keyed by row
};

} // namespace geck::ui
