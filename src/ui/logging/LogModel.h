#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QList>
#include <QString>

namespace geck {

/// In-memory store of log records backing the Log panel. Records arrive from LogModelSink on
/// whatever thread spdlog was called on; appendRecord() marshals them to the model's thread, so
/// all model state and signals stay on the GUI thread. The store is bounded: once MAX_RECORDS is
/// reached the oldest records are evicted.
class LogModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum class Level {
        Debug = 0,
        Info,
        Warning,
        Error,
    };

    enum Column {
        TimeColumn = 0,
        LevelColumn,
        MessageColumn,
        ColumnCount,
    };

    /// Role returning the record's Level as an int, on any column (used by LogFilterProxy).
    static constexpr int LEVEL_ROLE = Qt::UserRole + 1;

    static constexpr int MAX_RECORDS = 5000;

    explicit LogModel(QObject* parent = nullptr);

    /// Thread-safe append. When called off the model's thread the record is queued to it; the
    /// row appears once that thread's event loop runs.
    void appendRecord(Level level, const QString& message, const QDateTime& time = QDateTime::currentDateTime());

    void clear();

    static QString levelName(Level level);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    struct Record {
        QDateTime time;
        Level level;
        QString message;
    };

    void appendOnModelThread(const Record& record);

    QList<Record> _records;
};

} // namespace geck
