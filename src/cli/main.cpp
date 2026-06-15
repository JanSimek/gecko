#include "cli/MapAnalyzer.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " map analyze --data <dir-or-.dat> [--data <...>] [map ...]\n"
              << "  Reports ground-tile and object (scenery/wall/critter/...) usage across maps,\n"
              << "  per map and aggregated. With no map arguments, every maps/*.map is analysed.\n"
              << "  --data may be a Fallout 2 data directory or a .dat archive; repeat to mount several.\n";
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);

    if (args.size() < 2 || args[0] != "map" || args[1] != "analyze") {
        printUsage(argv[0]);
        return 2;
    }

    std::vector<std::string> dataPaths;
    geck::cli::AnalyzeOptions options;
    for (std::size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--data") {
            if (i + 1 >= args.size()) {
                std::cerr << "error: --data needs a path\n";
                printUsage(argv[0]);
                return 2;
            }
            dataPaths.push_back(args[++i]);
        } else {
            options.maps.push_back(args[i]);
        }
    }

    if (dataPaths.empty()) {
        std::cerr << "error: at least one --data <path> is required\n";
        printUsage(argv[0]);
        return 2;
    }

    // Keep the report on stdout clean: the resource/reader layers log to the default (stderr)
    // sink, including an [error] for every unknown script PID encountered while parsing maps —
    // expected noise here, so silence all levels.
    spdlog::set_level(spdlog::level::off);

    geck::resource::GameResources resources;
    for (const auto& path : dataPaths) {
        resources.files().addDataPath(path);
    }

    return geck::cli::analyzeMaps(resources, options, std::cout);
}
