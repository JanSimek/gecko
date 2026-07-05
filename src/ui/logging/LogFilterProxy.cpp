#include "LogFilterProxy.h"

namespace geck {

LogFilterProxy::LogFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent) {
}

void LogFilterProxy::setMinimumLevel(LogModel::Level level) {
    if (_minimumLevel == level) {
        return;
    }
    _minimumLevel = level;
    invalidateRowsFilter();
}

void LogFilterProxy::setSearchText(const QString& text) {
    if (_searchText == text) {
        return;
    }
    _searchText = text;
    invalidateRowsFilter();
}

bool LogFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QModelIndex index = sourceModel()->index(sourceRow, LogModel::MessageColumn, sourceParent);

    const int level = sourceModel()->data(index, LogModel::LEVEL_ROLE).toInt();
    if (level < static_cast<int>(_minimumLevel)) {
        return false;
    }

    if (_searchText.isEmpty()) {
        return true;
    }
    return sourceModel()->data(index, Qt::DisplayRole).toString().contains(_searchText, Qt::CaseInsensitive);
}

} // namespace geck
