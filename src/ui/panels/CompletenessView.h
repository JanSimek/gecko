#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace geck {

namespace resource {
    struct MapCompletenessReport;
}

/// The "Map" tab of the Log dock: a structured per-map completeness summary computed by
/// resource::scanMapCompleteness — every tile, object sprite, and script the open map references
/// but the mounted data cannot supply, grouped by kind, plus the data-path mounts the lookups
/// walk. The structured counterpart of the loader's prose warnings in the Log tab.
class CompletenessView : public QWidget {
    Q_OBJECT

public:
    explicit CompletenessView(QWidget* parent = nullptr);

    /// Populate the tree from a scan of the open map. `mapName` labels the status line.
    void setReport(const resource::MapCompletenessReport& report, const QString& mapName);

    /// No-map state: empty tree, muted status line, Refresh disabled.
    void clearReport();

Q_SIGNALS:
    /// The user asked to re-scan (the report reflects load time, not later edits).
    void refreshRequested();

private:
    void onCopy();

    /// Top-level section row; `missing` of `total` colours and expands it when non-zero.
    QTreeWidgetItem* addGroup(const QString& title, std::size_t missing, const QString& detail);

    QLabel* _status;
    QPushButton* _refreshButton;
    QPushButton* _copyButton;
    QTreeWidget* _tree;
};

} // namespace geck
