#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <spdlog/spdlog.h>
#include <iostream>

#include "Application.h"
#include "util/ResourceManager.h"
#include "util/QtDialogs.h"

int main(int argc, char** argv) {
    spdlog::set_pattern("[%^%l%$] %v");
    geck::Application app{ argc, argv };
    app.run();

    return 0;
}
