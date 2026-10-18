#include <catch2/catch_test_macros.hpp>

#include <QLabel>
#include <QAbstractButton>
#include <QSignalSpy>
#include <QTreeWidget>

#include "resource/MapCompleteness.h"
#include "ui/panels/CompletenessView.h"

#include "support/QtFind.h"

using namespace geck;

namespace {

resource::MapCompletenessReport reportWithGaps() {
    resource::MapCompletenessReport report;
    report.usedTileCount = 166;
    report.objectArtCount = 33;
    report.scriptCount = 3;
    report.missingTiles.push_back({ 271, "art/tiles/edg5000.frm", "art not in mounted data" });
    report.missingObjectArt.push_back({ 16793664u, {}, "FID does not resolve" });
    report.mounts.push_back({ resource::MountedSourceInfo::Kind::Dat, "master.dat", "DAT (master.dat)" });
    report.tilesLstMounted = true;
    report.scriptsLstMounted = false;
    return report;
}

} // namespace

TEST_CASE("CompletenessView starts in the no-map state", "[qt][completeness]") {
    CompletenessView view;

    auto* tree = view.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);
    CHECK(tree->topLevelItemCount() == 0);
    REQUIRE(test::findButton(view, "Refresh") != nullptr);
    CHECK_FALSE(test::findButton(view, "Refresh")->isEnabled());
}

TEST_CASE("CompletenessView renders a report as four grouped sections", "[qt][completeness]") {
    CompletenessView view;
    view.setReport(reportWithGaps(), "artemple.map");

    auto* tree = view.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);
    REQUIRE(tree->topLevelItemCount() == 4); // tiles, sprites, scripts, data paths

    const QTreeWidgetItem* tiles = tree->topLevelItem(0);
    REQUIRE(tiles->childCount() == 1);
    CHECK(tiles->isExpanded()); // sections with problems come pre-expanded
    CHECK(tiles->child(0)->text(0) == "tile 271");
    CHECK(tiles->child(0)->text(1) == "art/tiles/edg5000.frm");

    const QTreeWidgetItem* sprites = tree->topLevelItem(1);
    REQUIRE(sprites->childCount() == 1);
    CHECK(sprites->child(0)->text(0) == "FID 16793664");
    CHECK(sprites->child(0)->text(2) == "FID does not resolve");

    const QTreeWidgetItem* scripts = tree->topLevelItem(2);
    CHECK(scripts->childCount() == 0); // no unresolved scripts -> count-only summary row

    // Data paths: one mount row plus the two index rows; the unmounted scripts.lst is flagged.
    const QTreeWidgetItem* mounts = tree->topLevelItem(3);
    REQUIRE(mounts->childCount() == 3);
    CHECK(mounts->child(0)->text(2) == "mounted");
    CHECK(mounts->child(1)->text(1) == "art/tiles/tiles.lst");
    CHECK(mounts->child(1)->text(2) == "OK");
    CHECK(mounts->child(2)->text(1) == "scripts/scripts.lst");
    CHECK(mounts->child(2)->text(2) == "not mounted");

    // The status line carries the map name and the total.
    const QLabel* status = test::labelContaining(view, "artemple.map");
    REQUIRE(status != nullptr);
    CHECK(status->text().contains("2"));
}

TEST_CASE("CompletenessView reports a clean map without warning rows", "[qt][completeness]") {
    CompletenessView view;
    resource::MapCompletenessReport report;
    report.usedTileCount = 10;
    report.tilesLstMounted = true;
    report.scriptsLstMounted = true;
    view.setReport(report, "clean.map");

    auto* tree = view.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);
    REQUIRE(tree->topLevelItemCount() == 4);
    CHECK(tree->topLevelItem(0)->childCount() == 0);
    CHECK_FALSE(tree->topLevelItem(0)->isExpanded());

    CHECK(test::labelContaining(view, "every referenced resource resolves") != nullptr);
}

TEST_CASE("CompletenessView clearReport returns to the no-map state", "[qt][completeness]") {
    CompletenessView view;
    view.setReport(reportWithGaps(), "artemple.map");
    view.clearReport();

    auto* tree = view.findChild<QTreeWidget*>();
    REQUIRE(tree != nullptr);
    CHECK(tree->topLevelItemCount() == 0);
    const QAbstractButton* refresh = test::findButton(view, "Refresh");
    REQUIRE(refresh != nullptr);
    CHECK_FALSE(refresh->isEnabled());
}

TEST_CASE("CompletenessView refresh button emits refreshRequested", "[qt][completeness]") {
    CompletenessView view;
    view.setReport(reportWithGaps(), "artemple.map");

    QSignalSpy spy(&view, &CompletenessView::refreshRequested);
    QAbstractButton* refresh = test::findButton(view, "Refresh");
    REQUIRE(refresh != nullptr);
    refresh->click();
    CHECK(spy.count() == 1);
}
