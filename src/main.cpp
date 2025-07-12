#include <cxxopts.hpp>
#include <spdlog/spdlog.h>

#include "Application.h"
#include "util/ResourceManager.h"
#include "util/QtDialogs.h"

int main(int argc, char** argv) {
    cxxopts::Options options{ "GECK::Mapper", "Fallout 2 map editor" };

    spdlog::set_pattern("[%^%l%$] %v");

    // TODO: get real path to the binary
    //std::filesystem::path resources_path = std::filesystem::weakly_canonical(argv[0]).parent_path() / geck::Application::RESOURCES_DIR;
    std::filesystem::path resources_path = std::filesystem::current_path() / geck::Application::RESOURCES_DIR;

    options.add_options()(
        "d,data", "path to the Fallout 2 directory or individual data files, e.g. master.dat and critter.dat",
        cxxopts::value<std::vector<std::string>>()->default_value(resources_path.string()))(
        "m,map", "path to the map file to load", cxxopts::value<std::string>())(
        "debug", "show debug messages")(
        "h,help", "print usage");

    auto result = options.parse(argc, argv);

    for (const auto& unknownOption : result.unmatched()) {
        spdlog::warn("Unrecognized command line argument: {}", unknownOption);
    }

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        exit(0);
    }
    if (result.count("debug")) {
        spdlog::set_pattern("[%^%l%$] [thread %t] %v");
        spdlog::set_level(spdlog::level::debug);
    }
    if (result.count("data")) {
        for (const auto& data : result["data"].as<std::vector<std::string>>()) {
            geck::ResourceManager::getInstance().addDataPath(data);
        }
    } else {
        auto dir = geck::QtDialogs::selectFolder("Select Fallout 2 \"data\" directory which contains maps", resources_path.string());
        if (!dir.empty()) {
            spdlog::info("User selected data directory: {}", dir);
            geck::ResourceManager::getInstance().addDataPath(dir);
        }
    }

    std::string map = !result.count("map") ? std::string() : result["map"].as<std::string>();

    geck::Application app{ argc, argv, resources_path, map };

    app.run();

    return 0;
}
