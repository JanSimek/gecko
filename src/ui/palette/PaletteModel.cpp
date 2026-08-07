#include "ui/palette/PaletteModel.h"

namespace geck::ui {

PaletteModel::PaletteModel(QObject* parent)
    : QAbstractListModel(parent) {
}

void PaletteModel::setItems(std::vector<PaletteItem> items) {
    beginResetModel();
    _items = std::move(items);
    _iconCache.clear();
    endResetModel();
}

void PaletteModel::setIconProvider(IconProvider provider) {
    beginResetModel();
    _iconProvider = std::move(provider);
    _iconCache.clear();
    endResetModel();
}

const PaletteItem* PaletteModel::itemAt(int row) const {
    if (row < 0 || row >= static_cast<int>(_items.size())) {
        return nullptr;
    }
    return &_items[static_cast<size_t>(row)];
}

int PaletteModel::rowForEngineIndex(int engineIndex) const {
    for (size_t row = 0; row < _items.size(); ++row) {
        if (_items[row].engineIndex == engineIndex) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

int PaletteModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(_items.size());
}

QVariant PaletteModel::data(const QModelIndex& index, int role) const {
    const PaletteItem* item = index.isValid() ? itemAt(index.row()) : nullptr;
    if (item == nullptr) {
        return {};
    }

    switch (role) {
        case Qt::DisplayRole:
            return item->label;
        case Qt::ToolTipRole:
            return item->tooltip;
        case EngineIndexRole:
            return item->engineIndex;
        case Qt::DecorationRole: {
            if (!_iconProvider) {
                return {};
            }
            auto cached = _iconCache.find(index.row());
            if (cached == _iconCache.end()) {
                cached = _iconCache.emplace(index.row(), _iconProvider(*item)).first;
            }
            return cached->second;
        }
        default:
            return {};
    }
}

Qt::ItemFlags PaletteModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

} // namespace geck::ui
