#include "cli/MapAnalyzer.h"
#include "cli/MapGenerator.h"
#include "cli/MapRender.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct CliArgs {
    bool generate = false;
    bool render = false;
    std::vector<std::string> dataPaths;
    geck::cli::AnalyzeOptions analyze;
    geck::cli::GenerateOptions gen;
    geck::cli::RenderOptions ren;
};

void printUsage(const char* program) {
    std::cerr << "Usage:\n"
              << "  " << program << " map analyze [--json] --data <dir-or-.dat> [--data <...>] [map ...]\n"
              << "      Reports ground-tile and object (scenery/wall/critter/...) usage across maps,\n"
              << "      per map and aggregated. With no map arguments, every map under maps/ is analysed.\n"
              << "      --json emits machine-readable output (for the MCP) instead of the human report.\n"
              << "  " << program << " map generate --script <file.luau> --out <file.map>\n"
              << "      [--elevation 0|1|2] [--arg key=value ...] --data <dir-or-.dat> [--data <...>]\n"
              << "      Runs a Luau generation script against an empty map and writes the result.\n"
              << "      --arg passes parameters to the script (read as args.key).\n"
              << "  " << program << " map render --map <file.map> --out <file.png>\n"
              << "      [--elevation 0|1|2] [--max-dim N] [--roof] [--schematic] --data <dir-or-.dat> [...]\n"
              << "      Renders a map to a PNG (needs an off-screen GL context). --max-dim caps the\n"
              << "      longest side (default 1600); --roof draws the roof layer over the floor.\n"
              << "      --schematic flat-colours floor tiles by id + marks objects by category and\n"
              << "      prints the colour legend (join with analyze --json to read the picture);\n"
              << "      --show-blockers also marks FLAT objects (invisible engine blockers).\n"
              << "  --data may be a Fallout 2 data directory or a .dat archive; repeat to mount several.\n";
}

bool isKnownSubcommand(const std::vector<std::string>& args) {
    return args.size() >= 2 && args[0] == "map"
        && (args[1] == "analyze" || args[1] == "generate" || args[1] == "render");
}

bool generateMissingRequired(const CliArgs& cli) {
    return cli.generate && (cli.gen.scriptPath.empty() || cli.gen.outPath.empty());
}

bool renderMissingRequired(const CliArgs& cli) {
    return cli.render && (cli.ren.mapPath.empty() || cli.ren.outPath.empty());
}

// Consume the argument(s) starting at args[i]. Returns how many tokens were consumed (1 for a
// flag-less map/positional, 2 for a flag + its value), or 0 on error (after printing the reason).
// Keeping the token count explicit lets the caller advance the index instead of mutating it here.
int consumeArg(const std::vector<std::string>& args, std::size_t i, CliArgs& out, const char* program) {
    const std::string& arg = args[i];
    const bool valueFlag = arg == "--data"
        || (out.generate && (arg == "--script" || arg == "--out" || arg == "--elevation" || arg == "--arg"))
        || (out.render && (arg == "--map" || arg == "--out" || arg == "--elevation" || arg == "--max-dim"));

    if (valueFlag && i + 1 >= args.size()) {
        std::cerr << "error: " << arg << " needs a value\n";
        printUsage(program);
        return 0;
    }

    if (arg == "--data") {
        out.dataPaths.push_back(args[i + 1]);
        return 2;
    }
    if (out.generate && arg == "--script") {
        out.gen.scriptPath = args[i + 1];
        return 2;
    }
    if (out.generate && arg == "--out") {
        out.gen.outPath = args[i + 1];
        return 2;
    }
    if (out.generate && arg == "--elevation") {
        out.gen.elevation = std::stoi(args[i + 1]);
        return 2;
    }
    if (out.generate && arg == "--arg") {
        // key=value -> exposed to the script as args.key
        const std::string& kv = args[i + 1];
        const auto eq = kv.find('=');
        if (eq == std::string::npos || eq == 0) {
            std::cerr << "error: --arg expects key=value with a non-empty key, got: " << kv << "\n";
            printUsage(program);
            return 0;
        }
        out.gen.args[kv.substr(0, eq)] = kv.substr(eq + 1);
        return 2;
    }
    if (out.generate) {
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (out.render && arg == "--map") {
        out.ren.mapPath = args[i + 1];
        return 2;
    }
    if (out.render && arg == "--out") {
        out.ren.outPath = args[i + 1];
        return 2;
    }
    if (out.render && arg == "--elevation") {
        out.ren.elevation = std::stoi(args[i + 1]);
        return 2;
    }
    if (out.render && arg == "--max-dim") {
        out.ren.maxDimension = static_cast<unsigned int>(std::stoul(args[i + 1]));
        return 2;
    }
    if (out.render && arg == "--roof") {
        out.ren.showRoof = true;
        return 1;
    }
    if (out.render && arg == "--schematic") {
        out.ren.schematic = true;
        return 1;
    }
    if (out.render && arg == "--show-blockers") {
        out.ren.showBlockers = true;
        return 1;
    }
    if (out.render) {
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (arg == "--json") { // analyze only: machine-readable output for the MCP
        out.analyze.json = true;
        return 1;
    }
    out.analyze.maps.push_back(arg);
    return 1;
}

// Parse argv into `out`. Returns an exit code to return from main, or nullopt to proceed.
std::optional<int> parseArgs(const std::vector<std::string>& args, const char* program, CliArgs& out) {
    if (!isKnownSubcommand(args)) {
        printUsage(program);
        return 2;
    }
    out.generate = args[1] == "generate";
    out.render = args[1] == "render";

    for (std::size_t i = 2; i < args.size();) {
        const int consumed = consumeArg(args, i, out, program);
        if (consumed == 0) {
            return 2;
        }
        i += static_cast<std::size_t>(consumed);
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
    if (renderMissingRequired(out)) {
        std::cerr << "error: render requires --map and --out\n";
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
    if (cli.render) {
        return geck::cli::renderMap(resources, cli.ren, std::cout);
    }
    return geck::cli::analyzeMaps(resources, cli.analyze, std::cout);
}
