#include <catch2/catch_test_macros.hpp>

#include <thread>

#include <QCoreApplication>
#include <QElapsedTimer>

#include <spdlog/logger.h>

#include "ui/logging/LogFilterProxy.h"
#include "ui/logging/LogModel.h"
#include "ui/logging/LogModelSink.h"

using namespace geck;

TEST_CASE("LogModel stores records with level, message, and time", "[qt][log]") {
    LogModel model;
    REQUIRE(model.rowCount() == 0);

    model.appendRecord(LogModel::Level::Warning, "tile art missing",
        QDateTime::fromMSecsSinceEpoch(0));

    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.columnCount() == LogModel::ColumnCount);
    CHECK(model.index(0, LogModel::LevelColumn).data().toString() == "warning");
    CHECK(model.index(0, LogModel::MessageColumn).data().toString() == "tile art missing");
    CHECK(model.index(0, LogModel::MessageColumn).data(LogModel::LEVEL_ROLE).toInt()
        == static_cast<int>(LogModel::Level::Warning));
    CHECK(!model.index(0, LogModel::TimeColumn).data().toString().isEmpty());
}

TEST_CASE("LogModel evicts the oldest records beyond the cap", "[qt][log]") {
    LogModel model;
    for (int i = 0; i < LogModel::MAX_RECORDS + 10; ++i) {
        model.appendRecord(LogModel::Level::Info, QString::number(i));
    }

    REQUIRE(model.rowCount() == LogModel::MAX_RECORDS);
    CHECK(model.index(0, LogModel::MessageColumn).data().toString() == "10");
    CHECK(model.index(model.rowCount() - 1, LogModel::MessageColumn).data().toString()
        == QString::number(LogModel::MAX_RECORDS + 9));
}

TEST_CASE("LogModel clear empties the store", "[qt][log]") {
    LogModel model;
    model.appendRecord(LogModel::Level::Error, "boom");
    model.clear();
    REQUIRE(model.rowCount() == 0);
}

TEST_CASE("LogModel delivers records appended from another thread", "[qt][log]") {
    LogModel model;

    std::thread worker([&model]() {
        model.appendRecord(LogModel::Level::Info, "from worker thread");
    });
    worker.join();

    // The append was queued to this (the model's) thread; run the event loop until it lands.
    QElapsedTimer timer;
    timer.start();
    while (model.rowCount() == 0 && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    REQUIRE(model.rowCount() == 1);
    CHECK(model.index(0, LogModel::MessageColumn).data().toString() == "from worker thread");
}

TEST_CASE("LogModelSink forwards spdlog records with mapped levels", "[qt][log]") {
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

TEST_CASE("LogModelSink drops records after detach", "[qt][log]") {
    LogModel model;
    auto sink = std::make_shared<LogModelSink>(&model);
    spdlog::logger logger("log-panel-detach-test", sink);

    logger.info("before");
    sink->detach();
    logger.info("after");

    REQUIRE(model.rowCount() == 1);
    CHECK(model.index(0, LogModel::MessageColumn).data().toString() == "before");
}

TEST_CASE("LogFilterProxy filters by minimum level and message text", "[qt][log]") {
    LogModel model;
    model.appendRecord(LogModel::Level::Debug, "loading tiles");
    model.appendRecord(LogModel::Level::Info, "map opened");
    model.appendRecord(LogModel::Level::Warning, "12 tiles.lst entries could not be resolved");
    model.appendRecord(LogModel::Level::Error, "failed to resolve sprite");

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
