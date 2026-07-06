#include <spdlog/spdlog.h>
#include <QtCore/qglobal.h>
#include <QtCore/qlogging.h>
#include <QtCore/qstring.h>

#include "Application.h"

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

    geck::Application app{ argc, argv };
    app.run();
    return 0;
}
