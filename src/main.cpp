#include <spdlog/spdlog.h>
#include <QtCore/qglobal.h>
#include <QtCore/qlogging.h>
#include <QtCore/qstring.h>
#include <QtCore/QDir>
#include <QtCore/QLockFile>

#include "Application.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// Route Qt's own messages (qt.qpa.*, QObject warnings, ...) through spdlog so they carry
// the same timestamps and reach the in-app Log panel instead of only stderr.
void qtMessageToSpdlog(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const std::string text = context.category && *context.category
        ? std::string(context.category) + ": " + message.toStdString()
        : message.toStdString();
    switch (type) {
        case QtDebugMsg:
            spdlog::debug("{}", text);
            break;
        case QtInfoMsg:
            spdlog::info("{}", text);
            break;
        case QtWarningMsg:
            spdlog::warn("{}", text);
            break;
        default:
            spdlog::error("{}", text);
            break;
    }
}

} // namespace

int main(int argc, char** argv) {
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    qInstallMessageHandler(qtMessageToSpdlog);
    // The icons resource lives in the gecko_app static library, so the executable
    // must register it explicitly to keep toolbar and menu icons available.
    Q_INIT_RESOURCE(icons);

    // Single-instance guard: two gecko.exe processes racing for the one shared settings.json
    // (a plain read/write with no locking of its own - see Settings::load/save) is a confirmed
    // way to get intermittent, hard-to-reproduce data-path corruption - a stray/orphaned second
    // instance (a "closed" window whose process never actually exited) silently competing with a
    // fresh launch is exactly the kind of thing that explains one launch working and the very
    // next one not, with no visible cause. Refuse a second instance outright instead of letting
    // this class of bug exist at all. QDir::temp() rather than an app-specific path: this must
    // work before QCoreApplication's organizationName/applicationName are set (Application's own
    // constructor is what sets them, and hasn't run yet at this point).
    QLockFile instanceLock(QDir::temp().filePath("gecko_single_instance.lock"));
    instanceLock.setStaleLockTime(30000); // self-heals from a real crash after 30s; long enough for a slow launch
    if (!instanceLock.tryLock(100)) {
        qint64 otherPid = -1;
        QString hostname, appname;
        instanceLock.getLockInfo(&otherPid, &hostname, &appname);
#ifdef _WIN32
        MessageBoxW(nullptr,
            L"gecko is already running. Only one instance can run at a time - a second one would "
            L"silently race the first for the same settings file, which is exactly what's been "
            L"causing wrong/missing data on some launches.\n\nClose the other gecko window first.",
            L"gecko - Already Running", MB_OK | MB_ICONWARNING);
#endif
        return 1;
    }

    geck::Application app{ argc, argv };
    app.run();
    return 0;
}
