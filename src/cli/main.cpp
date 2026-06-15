#include "cli/MapAnalyzer.h"
#include "cli/MapGenerator.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct CliArgs {
    bool generate = false;
    std::vector<std::string> dataPaths;
    geck::cli::AnalyzeOptions analyze;
    geck::cli::GenerateOptions gen;
};

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

bool isKnownSubcommand(const std::vector<std::string>& args) {
    return args.size() >= 2 && args[0] == "map" && (args[1] == "analyze" || args[1] == "generate");
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

// Handle a generate-only flag at args[i]: 1 = consumed, 0 = not a generate flag, -1 = error.
int takeGenerateFlag(const std::vector<std::string>& args, std::size_t& i, geck::cli::GenerateOptions& gen) {
    if (args[i] == "--script") {
        return takeValue(args, i, "--script", gen.scriptPath) ? 1 : -1;
    }
    if (args[i] == "--out") {
        return takeValue(args, i, "--out", gen.outPath) ? 1 : -1;
    }
    if (args[i] == "--elevation") {
        std::string value;
        if (!takeValue(args, i, "--elevation", value)) {
            return -1;
        }
        gen.elevation = std::stoi(value);
        return 1;
    }
    return 0;
}

bool generateMissingRequired(const CliArgs& cli) {
    return cli.generate && (cli.gen.scriptPath.empty() || cli.gen.outPath.empty());
}

// Parse argv into `out`. Returns an exit code to return from main, or nullopt to proceed.
std::optional<int> parseArgs(const std::vector<std::string>& args, const char* program, CliArgs& out) {
    if (!isKnownSubcommand(args)) {
        printUsage(program);
        return 2;
    }
    out.generate = args[1] == "generate";

    for (std::size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--data") {
            std::string value;
            if (!takeValue(args, i, "--data", value)) {
                printUsage(program);
                return 2;
            }
            out.dataPaths.push_back(value);
        } else if (out.generate) {
            const int consumed = takeGenerateFlag(args, i, out.gen);
            if (consumed <= 0) {
                if (consumed == 0) {
                    std::cerr << "error: unexpected argument: " << args[i] << "\n";
                }
                printUsage(program);
                return 2;
            }
        } else {
            out.analyze.maps.push_back(args[i]);
        }
    }

    if (out.dataPaths.empty()) {
        std::cerr << "error: at least one --data <path> is required\n";
        printUsage(program);
        return 2;
    }
    if (generateMissingRequired(out)) {
        std::cerr << "error: generate requires --script and --out\n";
        printUsage(program);
        return 2;
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);

    CliArgs cli;
    if (const auto exitCode = parseArgs(args, argv[0], cli); exitCode.has_value()) {
        return *exitCode;
    }

    // Keep stdout clean: the resource/reader layers log to the default (stderr) sink, including
    // an [error] for every unknown script PID encountered while parsing maps — expected noise
    // here, so silence all levels.
    spdlog::set_level(spdlog::level::off);

    geck::resource::GameResources resources;
    for (const auto& path : cli.dataPaths) {
        resources.files().addDataPath(path);
    }

    if (cli.generate) {
        return geck::cli::generateMap(resources, cli.gen, std::cout);
    }
    return geck::cli::analyzeMaps(resources, cli.analyze, std::cout);
}
