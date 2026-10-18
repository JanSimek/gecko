#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include <QCoreApplication>
#include <QHeaderView>
#include <QTreeView>

#include <spdlog/logger.h>

#include "ui/logging/LogFilterProxy.h"
#include "ui/logging/LogModel.h"
#include "ui/logging/LogModelSink.h"
#include "ui/panels/LogPanel.h"
#include "support/QtWait.h"

using namespace geck;

namespace {

// Records reach the model through one queued drain per burst; run it.
void drainLog() {
    QCoreApplication::processEvents();
}

} // namespace

TEST_CASE("LogPanel configures its header only once a model provides columns", "[log_panel]") {
    // Before a source model attaches, the view's header has zero sections — per-index header
    // configuration then violates QHeaderView's precondition, which is a live Q_ASSERT in debug
    // Qt builds (it hung qt_tests on Windows Debug CI as a modal abort dialog).
    LogPanel panel;
    auto* view = panel.findChild<QTreeView*>();
    REQUIRE(view != nullptr);
    CHECK(view->header()->count() == 0);

    LogModel model;
    panel.setModel(&model);
    REQUIRE(view->header()->count() == LogModel::ColumnCount);
    CHECK(view->header()->sectionResizeMode(LogModel::TimeColumn) == QHeaderView::ResizeToContents);
    CHECK(view->header()->sectionResizeMode(LogModel::MessageColumn) == QHeaderView::Stretch);
}

TEST_CASE("LogModel stores records with level, message, and time", "[log_panel]") {
    LogModel model;
    REQUIRE(model.rowCount() == 0);

    model.appendRecord(LogModel::Level::Warning, "tile art missing",
        QDateTime::fromMSecsSinceEpoch(0));
    CHECK(model.rowCount() == 0); // buffered until the event loop drains the burst
    drainLog();

    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.columnCount() == LogModel::ColumnCount);
    CHECK(model.index(0, LogModel::LevelColumn).data().toString() == "warning");
    CHECK(model.index(0, LogModel::MessageColumn).data().toString() == "tile art missing");
    CHECK(model.index(0, LogModel::MessageColumn).data(LogModel::LEVEL_ROLE).toInt()
        == static_cast<int>(LogModel::Level::Warning));
    CHECK(!model.index(0, LogModel::TimeColumn).data().toString().isEmpty());
}

TEST_CASE("LogModel evicts the oldest records beyond the cap", "[log_panel]") {
    // Cross-thread so the records arrive as coalesced batches: 50k single-row same-thread
    // inserts take minutes under a debug heap, and the eviction arithmetic being tested is
    // shared by both paths.
    LogModel model;
    std::thread producer([&model]() {
        for (int i = 0; i < LogModel::MAX_RECORDS + 10; ++i) {
            model.appendRecord(LogModel::Level::Info, QString::number(i));
        }
    });
    producer.join();

    REQUIRE(geck::test::pumpUntil([&model]() { return model.rowCount() >= LogModel::MAX_RECORDS; },
        std::chrono::seconds(60)));
    REQUIRE(model.rowCount() == LogModel::MAX_RECORDS);
    CHECK(model.index(0, LogModel::MessageColumn).data().toString() == "10");
    CHECK(model.index(model.rowCount() - 1, LogModel::MessageColumn).data().toString()
        == QString::number(LogModel::MAX_RECORDS + 9));
}

TEST_CASE("LogModel clear empties the store, including records not yet drained", "[log_panel]") {
    LogModel model;
    model.appendRecord(LogModel::Level::Error, "boom");
    drainLog();
    model.appendRecord(LogModel::Level::Error, "pending");
    model.clear();
    drainLog();
    REQUIRE(model.rowCount() == 0);
}

TEST_CASE("LogModel delivers records appended from another thread", "[log_panel]") {
    LogModel model;

    std::thread worker([&model]() {
        model.appendRecord(LogModel::Level::Info, "from worker thread");
    });
    worker.join();

    // The append was queued to this (the model's) thread; run the event loop until it lands.
    REQUIRE(geck::test::pumpUntil([&model]() { return model.rowCount() != 0; }));
    REQUIRE(model.rowCount() == 1);
    CHECK(model.index(0, LogModel::MessageColumn).data().toString() == "from worker thread");
}

TEST_CASE("LogModelSink forwards spdlog records with mapped levels", "[log_panel]") {
    LogModel model;
    auto sink = std::make_shared<LogModelSink>(&model);
    spdlog::logger logger("log-panel-test", sink);
    logger.set_level(spdlog::level::trace);

    logger.trace("t");
    logger.debug("d");
    logger.info("i");
    logger.warn("w");
    logger.error("e");
    logger.critical("c");
    drainLog();

    REQUIRE(model.rowCount() == 6);
    auto levelAt = [&model](int row) {
        return static_cast<LogModel::Level>(
            model.index(row, LogModel::MessageColumn).data(LogModel::LEVEL_ROLE).toInt());
    };
    CHECK(levelAt(0) == LogModel::Level::Debug);
    CHECK(levelAt(1) == LogModel::Level::Debug);
    CHECK(levelAt(2) == LogModel::Level::Info);
    CHECK(levelAt(3) == LogModel::Level::Warning);
    CHECK(levelAt(4) == LogModel::Level::Error);
    CHECK(levelAt(5) == LogModel::Level::Error);
    CHECK(model.index(3, LogModel::MessageColumn).data().toString() == "w");
}

TEST_CASE("LogModelSink drops records after detach", "[log_panel]") {
    LogModel model;
    auto sink = std::make_shared<LogModelSink>(&model);
    spdlog::logger logger("log-panel-detach-test", sink);

    logger.info("before");
    sink->detach();
    logger.info("after");
    drainLog();

    REQUIRE(model.rowCount() == 1);
    CHECK(model.index(0, LogModel::MessageColumn).data().toString() == "before");
}

TEST_CASE("LogFilterProxy filters by minimum level and message text", "[log_panel]") {
    LogModel model;
    model.appendRecord(LogModel::Level::Debug, "loading tiles");
    model.appendRecord(LogModel::Level::Info, "map opened");
    model.appendRecord(LogModel::Level::Warning, "12 tiles.lst entries could not be resolved");
    model.appendRecord(LogModel::Level::Error, "failed to resolve sprite");
    drainLog();

    LogFilterProxy proxy;
    proxy.setSourceModel(&model);
    REQUIRE(proxy.rowCount() == 4);

    proxy.setMinimumLevel(LogModel::Level::Warning);
    REQUIRE(proxy.rowCount() == 2);
    CHECK(proxy.index(0, LogModel::MessageColumn).data().toString().startsWith("12 tiles.lst"));

    proxy.setSearchText("RESOLVE");
    REQUIRE(proxy.rowCount() == 2); // case-insensitive: "resolved" + "resolve"

    proxy.setMinimumLevel(LogModel::Level::Debug);
    proxy.setSearchText("tiles");
    REQUIRE(proxy.rowCount() == 2); // "loading tiles" + the warning

    proxy.setSearchText("no such message");
    REQUIRE(proxy.rowCount() == 0);
}
