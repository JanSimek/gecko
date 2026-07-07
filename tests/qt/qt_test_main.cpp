#include <cstdio>

#include <QByteArray>
#include <QApplication>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {

// Trace each test case to stderr, flushed immediately. Catch2's own report is buffered until
// the run ends, so when the suite hangs in CI (where ctest runs qt_tests as one opaque test)
// the timeout dump would otherwise not say WHICH test hung — this trace's last line does.
class TestNameTracer : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testCaseStarting(const Catch::TestCaseInfo& info) override {
        std::fprintf(stderr, "[qt_tests] %s\n", info.name.c_str());
        std::fflush(stderr);
    }
};

} // namespace

CATCH_REGISTER_LISTENER(TestNameTracer)

int main(int argc, char** argv) {
    QTemporaryDir testHome;
    if (!testHome.isValid()) {
        return 1;
    }

    qputenv("HOME", testHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);

#if defined(Q_OS_MACOS)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }
#elif defined(Q_OS_LINUX)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && qEnvironmentVariableIsEmpty("DISPLAY")
        && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }
#endif

    QApplication app(argc, argv);
    app.setApplicationName("gecko-qt-tests");
    app.setOrganizationName("gecko");
    Q_INIT_RESOURCE(icons);

    Catch::Session session;
    const int result = session.run(argc, argv);

    return result;
}
