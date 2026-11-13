#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include <QElapsedTimer>
#include <QMetaObject>
#include <QTreeView>

#include "ui/logging/LogModel.h"
#include "ui/panels/FileBrowserPanel.h"
#include "ui/panels/LogPanel.h"
#include "ui/Settings.h"
#include "resource/GameResources.h"
#include "support/QtFind.h"
#include "support/QtWait.h"

using namespace geck;

// Performance guards: CI-failing ceilings on operations that have already regressed
// catastrophically once. The budgets are deliberately generous (two orders of magnitude over a
// local Release run, sized for the slowest CI runner: Windows Debug) — they are tripwires for the disaster class (10x-100x regressions,
// e.g. per-record UI work inside a hot loop), not detectors of percent-level drift. If one of
// these fails, something is architecturally wrong, not merely slower.

using geck::test::pumpUntil;

namespace {

constexpr std::chrono::milliseconds BUDGET{ 60000 }; // every guard's ceiling

} // namespace

TEST_CASE("A cross-thread log flood drains without starving the UI thread", "[log_panel][performance]") {
    // Full stack: model + panel (filter proxy, tree view, follow-tail scrolling), the way the
    // application wires it. Regression history: one queued event + row insert + scroll relayout
    // PER RECORD made a --debug proto pass (tens of thousands of records) stall the main thread
    // for tens of seconds; batched draining handles the same flood in well under a second.
    LogModel model;
    LogPanel panel;
    panel.setModel(&model);

    constexpr int FLOOD = 20000;

    QElapsedTimer timer;
    timer.start();

    std::thread producer([&model]() {
        for (int i = 0; i < FLOOD; ++i) {
            model.appendRecord(LogModel::Level::Debug, QStringLiteral("record %1").arg(i));
        }
    });
    producer.join();

    REQUIRE(pumpUntil([&model]() { return model.rowCount() == FLOOD; }, BUDGET));
    const auto elapsed = timer.elapsed();
    INFO("draining " << FLOOD << " cross-thread records took " << elapsed << "ms");
    CHECK(elapsed < BUDGET.count());
}

TEST_CASE("A UI-thread log flood is buffered, not inserted row by row", "[log_panel][performance]") {
    // Regression history: records logged ON the UI thread were inserted one row at a time, and the
    // panel's ResizeToContents columns re-measure every row on each insert, so a script console run
    // under --debug (a debug line per painted tile) spent ~15ms per record and hit the console's
    // 30s budget after ~2000 records, leaving a cave map half painted. Same-thread records now
    // buffer like cross-thread ones and land as one batched insert. The regression grows with the row
    // count (every insert re-measures every row), so 20k records under it run far past the budget.
    LogModel model;
    LogPanel panel;
    panel.setModel(&model);
    panel.show();

    constexpr int FLOOD = 20000;

    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < FLOOD; ++i) {
        model.appendRecord(LogModel::Level::Debug, QStringLiteral("record %1").arg(i));
    }
    const auto appending = timer.elapsed();
    INFO("appending " << FLOOD << " UI-thread records took " << appending << "ms");
    CHECK(appending < BUDGET.count());

    REQUIRE(pumpUntil([&model]() { return model.rowCount() == FLOOD; }, BUDGET));
    INFO("appending and draining took " << timer.elapsed() << "ms");
    CHECK(timer.elapsed() < BUDGET.count());
}

TEST_CASE("File-browser tree population stays within its time budget", "[file_browser][performance]") {
    // Regression history: per-row VFS/proto lookups, sorted proxy inserts on repopulation, and
    // an event-loop-starving chunk pump each independently pushed a ~100ms build into the
    // 10-30 second range. 30k synthetic rows through the real population path must stay fast.
    auto resources = std::make_shared<resource::GameResources>();
    auto settings = std::make_shared<Settings>();
    FileBrowserPanel panel(resources, settings);
    panel.setFileTypeFilter(QStringLiteral("All Files"));

    std::vector<FileBrowserEntry> entries;
    constexpr int ROWS = 15000;
    entries.reserve(ROWS);
    for (int i = 0; i < ROWS; ++i) {
        FileBrowserEntry entry;
        entry.path = QStringLiteral("art/dir%1/file%2.frm").arg(i % 40).arg(i);
        entry.normalizedPath = entry.path;
        entry.extension = QStringLiteral(".frm");
        entry.source = QStringLiteral("Test");
        entries.push_back(std::move(entry));
    }

    const auto populatedLabelShown = [&panel]() {
        return geck::test::labelContaining(panel, QStringLiteral("files loaded")) != nullptr;
    };

    QElapsedTimer timer;
    timer.start();

    // onFilesLoaded is a private slot; string-based invocation goes through the meta-object
    // system, exactly like the queued worker signal it normally receives.
    REQUIRE(QMetaObject::invokeMethod(&panel, "onFilesLoaded", Qt::DirectConnection,
        Q_ARG(std::vector<FileBrowserEntry>, entries)));

    REQUIRE(pumpUntil(populatedLabelShown, BUDGET));
    const auto elapsed = timer.elapsed();

    auto* tree = panel.findChild<QTreeView*>();
    REQUIRE(tree != nullptr);
    REQUIRE(tree->model()->rowCount() > 0);

    INFO("populating " << ROWS << " rows took " << elapsed << "ms");
    CHECK(elapsed < BUDGET.count());
}
