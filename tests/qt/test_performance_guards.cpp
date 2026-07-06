#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <thread>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QMetaObject>
#include <QTreeView>

#include "ui/logging/LogModel.h"
#include "ui/panels/FileBrowserPanel.h"
#include "ui/panels/LogPanel.h"
#include "ui/Settings.h"
#include "resource/GameResources.h"

using namespace geck;

// Performance guards: CI-failing ceilings on operations that have already regressed
// catastrophically once. The budgets are deliberately generous (two orders of magnitude over a
// local Release run, sized for the slowest CI runner: Windows Debug) — they are tripwires for the disaster class (10x-100x regressions,
// e.g. per-record UI work inside a hot loop), not detectors of percent-level drift. If one of
// these fails, something is architecturally wrong, not merely slower.

namespace {

bool pumpUntil(const std::function<bool()>& done, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (done()) {
            return true;
        }
    }
    return done();
}

} // namespace

TEST_CASE("A cross-thread log flood drains without starving the UI thread", "[qt][performance]") {
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

    REQUIRE(pumpUntil([&model]() { return model.rowCount() == FLOOD; }, 60000));
    const auto elapsed = timer.elapsed();
    INFO("draining " << FLOOD << " cross-thread records took " << elapsed << "ms");
    CHECK(elapsed < 60000);
}

TEST_CASE("File-browser tree population stays within its time budget", "[qt][performance]") {
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
        const auto labels = panel.findChildren<QLabel*>();
        return std::any_of(labels.begin(), labels.end(), [](const QLabel* label) {
            return label->text().contains(QStringLiteral("files loaded"));
        });
    };

    QElapsedTimer timer;
    timer.start();

    // onFilesLoaded is a private slot; string-based invocation goes through the meta-object
    // system, exactly like the queued worker signal it normally receives.
    REQUIRE(QMetaObject::invokeMethod(&panel, "onFilesLoaded", Qt::DirectConnection,
        Q_ARG(std::vector<FileBrowserEntry>, entries)));

    REQUIRE(pumpUntil(populatedLabelShown, 60000));
    const auto elapsed = timer.elapsed();

    auto* tree = panel.findChild<QTreeView*>();
    REQUIRE(tree != nullptr);
    REQUIRE(tree->model()->rowCount() > 0);

    INFO("populating " << ROWS << " rows took " << elapsed << "ms");
    CHECK(elapsed < 60000);
}
