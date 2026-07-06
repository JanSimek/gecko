#include "LogModel.h"

#include <mutex>

#include <QColor>
#include <QThread>

#include "ui/theme/ThemeManager.h"

namespace geck {

LogModel::LogModel(QObject* parent)
    : QAbstractTableModel(parent) {
}

void LogModel::appendRecord(Level level, const QString& message, const QDateTime& time) {
    Record record{ time, level, message };
    if (QThread::currentThread() == thread()) {
        appendBatchOnModelThread({ std::move(record) });
        return;
    }

    // Buffer and coalesce: a debug-heavy burst (e.g. thousands of proto reads) must become a
    // handful of batched model updates, not one queued event + row insert + view relayout per
    // record — that flood starved the UI thread for tens of seconds. Only the first record of
    // a burst schedules a drain; if the model is destroyed first, Qt drops the queued call.
    bool scheduleDrain = false;
    {
        const std::lock_guard<std::mutex> lock(_pendingMutex);
        _pending.append(std::move(record));
        scheduleDrain = !_drainQueued;
        _drainQueued = true;
    }
    if (scheduleDrain) {
        QMetaObject::invokeMethod(this, &LogModel::drainPending, Qt::QueuedConnection);
    }
}

void LogModel::drainPending() {
    QList<Record> batch;
    {
        const std::lock_guard<std::mutex> lock(_pendingMutex);
        batch.swap(_pending);
        _drainQueued = false;
    }
    appendBatchOnModelThread(std::move(batch));
}

void LogModel::appendBatchOnModelThread(QList<Record> batch) {
    if (batch.isEmpty()) {
        return;
    }
    if (batch.size() > MAX_RECORDS) {
        batch = batch.mid(batch.size() - MAX_RECORDS);
    }

    const int overflow = static_cast<int>(_records.size() + batch.size()) - MAX_RECORDS;
    if (overflow > 0) {
        beginRemoveRows(QModelIndex(), 0, overflow - 1);
        _records.remove(0, overflow);
        endRemoveRows();
    }

    beginInsertRows(QModelIndex(), static_cast<int>(_records.size()),
        static_cast<int>(_records.size() + batch.size()) - 1);
    _records.append(std::move(batch));
    endInsertRows();
}

void LogModel::clear() {
    if (_records.isEmpty()) {
        return;
    }
    beginResetModel();
    _records.clear();
    endResetModel();
}

QString LogModel::levelName(Level level) {
    switch (level) {
        case Level::Debug:
            return QStringLiteral("debug");
        case Level::Info:
            return QStringLiteral("info");
        case Level::Warning:
            return QStringLiteral("warning");
        case Level::Error:
            return QStringLiteral("error");
    }
    return {};
}

int LogModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(_records.size());
}

int LogModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= _records.size()) {
        return {};
    }

    const Record& record = _records.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case TimeColumn:
                return record.time.toString(QStringLiteral("HH:mm:ss.zzz"));
            case LevelColumn:
                return levelName(record.level);
            case MessageColumn:
                return record.message;
            default:
                return {};
        }
    }

    if (role == Qt::ForegroundRole) {
        switch (record.level) {
            case Level::Error:
                return QColor(ui::theme::colors::STATUS_ERROR);
            case Level::Warning:
                return ui::theme::colors::statusWarningRgb();
            case Level::Debug:
                return QColor(ui::theme::colors::TEXT_MUTED);
            case Level::Info:
                return {};
        }
    }

    if (role == LEVEL_ROLE) {
        return static_cast<int>(record.level);
    }

    return {};
}

QVariant LogModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
        case TimeColumn:
            return tr("Time");
        case LevelColumn:
            return tr("Level");
        case MessageColumn:
            return tr("Message");
        default:
            return {};
    }
}

} // namespace geck
