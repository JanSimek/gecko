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

void PaletteModel::setMimeProvider(QString mimeType, MimeProvider provider) {
    _mimeType = std::move(mimeType);
    _mimeProvider = std::move(provider);
}

void PaletteModel::setShowLabels(bool show) {
    if (_showLabels == show) {
        return;
    }
    _showLabels = show;
    if (!_items.empty()) {
        Q_EMIT dataChanged(index(0, 0), index(static_cast<int>(_items.size()) - 1, 0), { Qt::DisplayRole });
    }
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
            return _showLabels ? item->label : QString();
        case LabelRole:
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
    Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (_mimeProvider) {
        itemFlags |= Qt::ItemIsDragEnabled;
    }
    return itemFlags;
}

QStringList PaletteModel::mimeTypes() const {
    return _mimeProvider ? QStringList{ _mimeType } : QStringList{};
}

QMimeData* PaletteModel::mimeData(const QModelIndexList& indexes) const {
    if (!_mimeProvider || indexes.isEmpty()) {
        return nullptr;
    }
    const PaletteItem* item = itemAt(indexes.first().row());
    return item ? _mimeProvider(*item) : nullptr;
}

Qt::DropActions PaletteModel::supportedDragActions() const {
    return Qt::CopyAction;
}

} // namespace geck::ui
