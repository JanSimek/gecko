#include "CompletenessView.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "resource/MapCompleteness.h"
#include "ui/theme/ThemeManager.h"

namespace geck {

namespace {

    constexpr int kResourceColumn = 0;
    constexpr int kNameColumn = 1;
    constexpr int kStatusColumn = 2;

    void paintWarning(QTreeWidgetItem* item) {
        const QBrush warning{ QColor(ui::theme::colors::WARNING) };
        for (int column = 0; column < item->columnCount(); ++column) {
            item->setForeground(column, warning);
        }
    }

} // namespace

CompletenessView::CompletenessView(QWidget* parent)
    : QWidget(parent)
    , _status(new QLabel(this))
    , _refreshButton(new QPushButton(tr("Refresh"), this))
    , _copyButton(new QPushButton(tr("Copy"), this))
    , _tree(new QTreeWidget(this)) {

    _refreshButton->setToolTip(tr("Re-check the open map against the mounted data (the report reflects load time, not later edits)"));

    auto* controls = new QHBoxLayout();
    controls->setSpacing(ui::theme::spacing::NORMAL);
    controls->addWidget(_status, 1);
    controls->addWidget(_refreshButton);
    controls->addWidget(_copyButton);

    _tree->setColumnCount(3);
    _tree->setHeaderLabels({ tr("Resource"), tr("Name / path"), tr("Status") });
    _tree->setRootIsDecorated(true);
    _tree->setUniformRowHeights(true);
    _tree->setAlternatingRowColors(true);
    _tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    _tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _tree->header()->setSectionResizeMode(kResourceColumn, QHeaderView::ResizeToContents);
    _tree->header()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    _tree->header()->setSectionResizeMode(kStatusColumn, QHeaderView::ResizeToContents);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(ui::theme::spacing::TIGHT);
    layout->addLayout(controls);
    layout->addWidget(_tree, 1);

    connect(_refreshButton, &QPushButton::clicked, this, [this]() { Q_EMIT refreshRequested(); });
    connect(_copyButton, &QPushButton::clicked, this, &CompletenessView::onCopy);

    clearReport();
}

QTreeWidgetItem* CompletenessView::addGroup(const QString& title, std::size_t missing, const QString& detail) {
    auto* group = new QTreeWidgetItem(_tree);
    group->setText(kResourceColumn, title);
    group->setText(kNameColumn, detail);
    QFont bold = group->font(kResourceColumn);
    bold.setBold(true);
    group->setFont(kResourceColumn, bold);
    if (missing > 0) {
        group->setForeground(kResourceColumn, QBrush(QColor(ui::theme::colors::WARNING)));
        group->setExpanded(true);
    }
    return group;
}

void CompletenessView::setReport(const resource::MapCompletenessReport& report, const QString& mapName) {
    _tree->clear();
    _refreshButton->setEnabled(true);
    _copyButton->setEnabled(true);

    if (report.complete()) {
        _status->setText(tr("%1 — every referenced resource resolves").arg(mapName));
        _status->setStyleSheet(QStringLiteral("color: %1;").arg(ui::theme::colors::SUCCESS));
    } else {
        _status->setText(tr("%1 — %2 unresolved reference(s)").arg(mapName).arg(report.missingCount()));
        _status->setStyleSheet(QStringLiteral("color: %1;").arg(ui::theme::colors::WARNING));
    }

    auto* tiles = addGroup(tr("Tile art"), report.missingTiles.size(),
        report.missingTiles.empty()
            ? tr("%1 used tile(s), all resolve").arg(report.usedTileCount)
            : tr("%1 of %2 used tile(s) unresolved").arg(report.missingTiles.size()).arg(report.usedTileCount));
    for (const auto& tile : report.missingTiles) {
        auto* item = new QTreeWidgetItem(tiles);
        item->setText(kResourceColumn, tr("tile %1").arg(tile.id));
        item->setText(kNameColumn, QString::fromStdString(tile.art));
        item->setText(kStatusColumn, QString::fromStdString(tile.reason));
        paintWarning(item);
    }

    auto* sprites = addGroup(tr("Object sprites"), report.missingObjectArt.size(),
        report.missingObjectArt.empty()
            ? tr("%1 distinct sprite(s), all resolve").arg(report.objectArtCount)
            : tr("%1 of %2 distinct sprite(s) unresolved").arg(report.missingObjectArt.size()).arg(report.objectArtCount));
    for (const auto& art : report.missingObjectArt) {
        auto* item = new QTreeWidgetItem(sprites);
        item->setText(kResourceColumn, tr("FID %1").arg(art.fid));
        item->setText(kNameColumn, QString::fromStdString(art.art));
        item->setText(kStatusColumn, QString::fromStdString(art.reason));
        paintWarning(item);
    }

    auto* scripts = addGroup(tr("Scripts"), report.unresolvedScripts.size(),
        report.unresolvedScripts.empty()
            ? tr("%1 referenced script(s), all resolve").arg(report.scriptCount)
            : tr("%1 of %2 referenced script(s) unresolved").arg(report.unresolvedScripts.size()).arg(report.scriptCount));
    for (const auto& script : report.unresolvedScripts) {
        auto* item = new QTreeWidgetItem(scripts);
        item->setText(kResourceColumn, tr("index %1").arg(script.programIndex));
        item->setText(kNameColumn, QString::fromStdString(script.name));
        item->setText(kStatusColumn, QString::fromStdString(script.reason));
        paintWarning(item);
    }

    // Data-path sanity: the mounts every lookup above walked, plus whether the two index files
    // the checks key off were themselves found. An unmounted index makes whole sections
    // unresolvable, so it is the first thing to look at when the counts above are large.
    const std::size_t indexProblems = (report.tilesLstMounted ? 0 : 1) + (report.scriptsLstMounted ? 0 : 1);
    auto* mounts = addGroup(tr("Data paths"), indexProblems, tr("%1 mounted").arg(report.mounts.size()));
    for (const auto& mount : report.mounts) {
        auto* item = new QTreeWidgetItem(mounts);
        item->setText(kResourceColumn, mount.kind == resource::MountedSourceInfo::Kind::Dat ? tr("DAT") : tr("folder"));
        item->setText(kNameColumn, QString::fromStdString(mount.sourcePath.generic_string()));
        item->setText(kStatusColumn, tr("mounted"));
    }
    const auto addIndexRow = [&](const QString& name, bool mounted) {
        auto* item = new QTreeWidgetItem(mounts);
        item->setText(kResourceColumn, tr("index"));
        item->setText(kNameColumn, name);
        item->setText(kStatusColumn, mounted ? tr("OK") : tr("not mounted"));
        if (!mounted) {
            paintWarning(item);
        }
    };
    addIndexRow(QStringLiteral("art/tiles/tiles.lst"), report.tilesLstMounted);
    addIndexRow(QStringLiteral("scripts/scripts.lst"), report.scriptsLstMounted);
}

void CompletenessView::clearReport() {
    _tree->clear();
    _refreshButton->setEnabled(false);
    _copyButton->setEnabled(false);
    _status->setText(tr("No map loaded — completeness is computed when a map opens"));
    _status->setStyleSheet(QStringLiteral("color: %1;").arg(ui::theme::colors::TEXT_MUTED));
}

void CompletenessView::onCopy() {
    QStringList lines;
    lines.append(_status->text());
    for (int groupIndex = 0; groupIndex < _tree->topLevelItemCount(); ++groupIndex) {
        const QTreeWidgetItem* group = _tree->topLevelItem(groupIndex);
        lines.append(QStringLiteral("%1: %2").arg(group->text(kResourceColumn), group->text(kNameColumn)));
        for (int childIndex = 0; childIndex < group->childCount(); ++childIndex) {
            const QTreeWidgetItem* child = group->child(childIndex);
            lines.append(QStringLiteral("  %1\t%2\t%3")
                    .arg(child->text(kResourceColumn), child->text(kNameColumn), child->text(kStatusColumn)));
        }
    }
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

} // namespace geck
