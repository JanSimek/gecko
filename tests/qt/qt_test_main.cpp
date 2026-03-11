#include <QByteArray>
#include <QApplication>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <catch2/catch_session.hpp>

#include "util/ResourceManager.h"

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

    geck::ResourceManager::getInstance().clearAllDataPaths();
    geck::ResourceManager::getInstance().cleanup();

    return result;
}
