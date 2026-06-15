#include "cli/MapAnalyzer.h"
#include "cli/MapGenerator.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage(const char* program) {
    std::cerr << "Usage:\n"
              << "  " << program << " map analyze --data <dir-or-.dat> [--data <...>] [map ...]\n"
              << "      Reports ground-tile and object (scenery/wall/critter/...) usage across maps,\n"
              << "      per map and aggregated. With no map arguments, every maps/*.map is analysed.\n"
              << "  " << program << " map generate --script <file.luau> --out <file.map>\n"
              << "      [--elevation 0|1|2] --data <dir-or-.dat> [--data <...>]\n"
              << "      Runs a Luau generation script against an empty map and writes the result.\n"
              << "  --data may be a Fallout 2 data directory or a .dat archive; repeat to mount several.\n";
}

// Pulls the value following `flag` out of args at index i (which must point at the flag),
// advancing i past the value. Returns false (and prints) if the value is missing.
bool takeValue(const std::vector<std::string>& args, std::size_t& i, const char* flag, std::string& outValue) {
    if (i + 1 >= args.size()) {
        std::cerr << "error: " << flag << " needs a value\n";
        return false;
    }
    outValue = args[++i];
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);

    if (args.size() < 2 || args[0] != "map" || (args[1] != "analyze" && args[1] != "generate")) {
        printUsage(argv[0]);
        return 2;
    }
    const bool generate = args[1] == "generate";

    std::vector<std::string> dataPaths;
    geck::cli::AnalyzeOptions analyzeOptions;
    geck::cli::GenerateOptions generateOptions;

    for (std::size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--data") {
            std::string value;
            if (!takeValue(args, i, "--data", value)) {
                printUsage(argv[0]);
                return 2;
            }
            dataPaths.push_back(value);
        } else if (generate && args[i] == "--script") {
            if (!takeValue(args, i, "--script", generateOptions.scriptPath)) {
                return 2;
            }
        } else if (generate && args[i] == "--out") {
            if (!takeValue(args, i, "--out", generateOptions.outPath)) {
                return 2;
            }
        } else if (generate && args[i] == "--elevation") {
            std::string value;
            if (!takeValue(args, i, "--elevation", value)) {
                return 2;
            }
            generateOptions.elevation = std::stoi(value);
        } else if (!generate) {
            analyzeOptions.maps.push_back(args[i]);
        } else {
            std::cerr << "error: unexpected argument: " << args[i] << "\n";
            printUsage(argv[0]);
            return 2;
        }
    }

    if (dataPaths.empty()) {
        std::cerr << "error: at least one --data <path> is required\n";
        printUsage(argv[0]);
        return 2;
    }
    if (generate && (generateOptions.scriptPath.empty() || generateOptions.outPath.empty())) {
        std::cerr << "error: generate requires --script and --out\n";
        printUsage(argv[0]);
        return 2;
    }

    // Keep stdout clean: the resource/reader layers log to the default (stderr) sink, including
    // an [error] for every unknown script PID encountered while parsing maps — expected noise
    // here, so silence all levels.
    spdlog::set_level(spdlog::level::off);

    geck::resource::GameResources resources;
    for (const auto& path : dataPaths) {
        resources.files().addDataPath(path);
    }

    if (generate) {
        return geck::cli::generateMap(resources, generateOptions, std::cout);
    }
    return geck::cli::analyzeMaps(resources, analyzeOptions, std::cout);
}
