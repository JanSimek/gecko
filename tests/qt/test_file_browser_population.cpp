#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QTreeView>

#include "resource/GameResources.h"
#include "support/Fixtures.h"
#include "ui/Settings.h"
#include "ui/panels/FileBrowserPanel.h"

using namespace geck;

namespace {

// Depth-first search of a tree model for an exact display string in `column`.
bool modelContains(const QAbstractItemModel* model, int column, const QString& needle,
    const QModelIndex& parent = QModelIndex()) {
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex nameIndex = model->index(row, 0, parent);
        const QModelIndex index = model->index(row, column, parent);
        if (index.isValid() && index.data().toString() == needle) {
            return true;
        }
        if (modelContains(model, column, needle, nameIndex)) {
            return true;
        }
    }
    return false;
}

// The population is asynchronous (loader worker thread, then the chunked tree build);
// completion is observable through the status label's "N files loaded" text.
bool waitForPopulation(FileBrowserPanel& panel, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        for (const auto* label : panel.findChildren<QLabel*>()) {
            if (label->text().contains(QStringLiteral("files loaded"))) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_CASE("FileBrowserPanel populates the tree with worker-computed metadata", "[qt][filebrowser]") {
    auto resources = std::make_shared<resource::GameResources>();
    resources->files().addDataPath(geck::test::dataPath("f2_res.dat"));
    resources->files().addDataPath(geck::test::dataDir()); // loose files, incl. a .pro fixture
    auto settings = std::make_shared<Settings>();

    FileBrowserPanel panel(resources, settings);
    panel.loadFiles();
    REQUIRE(waitForPopulation(panel, 15000));

    auto* tree = panel.findChild<QTreeView*>();
    REQUIRE(tree != nullptr);
    const QAbstractItemModel* model = tree->model();
    REQUIRE(model->rowCount() > 0);

    // A known fixture entry appears with the metadata the worker computed off-thread:
    // its file name, its ".frm" type column, and the DAT source label.
    CHECK(modelContains(model, 0, QStringLiteral("hr_alltlk.frm")));
    CHECK(modelContains(model, 1, QStringLiteral(".frm")));
    CHECK(modelContains(model, 2, QStringLiteral("DAT (f2_res.dat)")));

    // PRO names resolve in the worker's second pass and land in the tree afterwards —
    // either via the cache at row insertion or as a live column update. The fixture tree
    // carries no pro_item.msg, so the resolved value is the explicit "MSG not found"
    // (never silently blank), which is exactly what proves the pipeline delivered.
    QElapsedTimer timer;
    timer.start();
    bool proNameArrived = false;
    while (timer.elapsed() < 15000 && !proNameArrived) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        proNameArrived = modelContains(model, 4, QStringLiteral("MSG not found"));
    }
    CHECK(proNameArrived);
    CHECK(modelContains(model, 0, QStringLiteral("test_item_drug_radx.pro")));
}
