#include <spdlog/spdlog.h>
#include <QtCore/qglobal.h>

#include "Application.h"

int main(int argc, char** argv) {
    spdlog::set_pattern("[%^%l%$] %v");
    // The icons resource lives in the gecko_app static library, so the executable
    // must register it explicitly to keep toolbar and menu icons available.
    Q_INIT_RESOURCE(icons);

    geck::Application app{ argc, argv };
    app.run();
    return 0;
}
