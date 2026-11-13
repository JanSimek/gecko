#pragma once

#include <QTest>

#include <chrono>
#include <functional>

namespace geck::test {

/// Processes Qt events until `done()` holds or `timeout` passes, and returns whether it held. Wrap
/// it in REQUIRE/CHECK: QVERIFY / QTRY_* report to QTestLib, which Catch2 never hears from.
inline bool pumpUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    return QTest::qWaitFor(done, static_cast<int>(timeout.count()));
}

} // namespace geck::test
