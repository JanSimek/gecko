#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <spdlog/spdlog.h>
#include <iostream>

#include "Application.h"
#include "util/ResourceManager.h"

int main(int argc, char** argv) {
    try {
        spdlog::set_pattern("[%^%l%$] %v");

        geck::Application app{ argc, argv };
        app.run();
        return 0;

    } catch (const std::system_error& e) {
        spdlog::error("System error in main: {} (code: {})", e.what(), e.code().value());
        return -1;
    } catch (const std::exception& e) {
        spdlog::error("Exception in main: {}", e.what());
        return -1;
    } catch (...) {
        spdlog::error("Unknown exception in main");
        return -1;
    }
}
