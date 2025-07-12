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

    // TODO: get real path to the binary
    //std::filesystem::path resources_path = std::filesystem::weakly_canonical(argv[0]).parent_path() / geck::Application::RESOURCES_DIR;
    std::filesystem::path resources_path = std::filesystem::current_path() / geck::Application::RESOURCES_DIR;

    geck::Application app{ argc, argv, resources_path, "" };

    app.run();

    return 0;
}
