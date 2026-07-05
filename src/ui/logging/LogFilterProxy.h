#pragma once

#include <QSortFilterProxyModel>

#include "LogModel.h"

namespace geck {

/// Filters LogModel rows by a minimum level and a case-insensitive message substring.
class LogFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit LogFilterProxy(QObject* parent = nullptr);

    void setMinimumLevel(LogModel::Level level);
    void setSearchText(const QString& text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    LogModel::Level _minimumLevel = LogModel::Level::Debug;
    QString _searchText;
};

} // namespace geck
