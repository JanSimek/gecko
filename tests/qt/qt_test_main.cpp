#include <cstdio>

#ifdef _WIN32
#include <crtdbg.h>
#include <Windows.h> // capital W: the Windows SDK's on-disk file name
#endif

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
#ifdef _WIN32
    // Fail loudly, never modally: on a headless CI runner a debug-CRT assert/abort dialog (e.g.
    // from a debug-Qt Q_ASSERT via qFatal) blocks forever with its message stuck in a buffered
    // pipe — the suite "hangs" with no diagnostic. Route CRT reports to stderr, keep abort()
    // non-interactive, suppress the OS fault boxes, and unbuffer stderr so the text gets out.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    // Debug CRT only: in the release CRT these are no-op macros that discard their arguments
    // (leaving `report` unreferenced, which /WX rejects) — and there are no report dialogs to
    // redirect there anyway.
    for (int report : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT }) {
        _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
    }
#endif
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#endif

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
