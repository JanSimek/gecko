#include "cli/BundledResources.h"
#include "cli/MapAnalyzer.h"
#include "cli/MapGenerator.h"
#include "cli/MapRender.h"
#include "cli/MapReachability.h"
#include "cli/PatternExtract.h"
#include "cli/ScriptIntrospect.h"
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
    bool extract = false;
    bool describeScript = false;
    bool reachability = false;
    std::vector<std::string> dataPaths;
    geck::cli::AnalyzeOptions analyze;
    geck::cli::GenerateOptions gen;
    geck::cli::RenderOptions ren;
    geck::cli::ExtractOptions ext;
    geck::cli::DescribeScriptOptions desc;
    geck::cli::ReachabilityOptions reach;
};

void printUsage(const char* program) {
    std::cerr << "Usage:\n"
              << "  " << program << " map analyze [--json|--palette] --data <dir-or-.dat> [--data <...>] [map ...]\n"
              << "      Reports ground-tile and object (scenery/wall/critter/...) usage across maps,\n"
              << "      per map and aggregated. With no map arguments, every map under maps/ is analysed.\n"
              << "      --json emits machine-readable output (for the MCP) instead of the human report;\n"
              << "      --palette emits just the weighted floor + scenery palette a generator script needs.\n"
              << "  " << program << " map generate --script <file.luau> --out <file.map>\n"
              << "      [--elevation 0|1|2] [--arg key=value ...] [--stamp name=file.json ...]\n"
              << "      --data <dir-or-.dat> [--data <...>]\n"
              << "      Runs a Luau generation script against an empty map and writes the result.\n"
              << "      --arg passes parameters to the script (read as args.key); --stamp pre-loads a\n"
              << "      pattern stamp the script places with api:placeStamp(name, anchorHex, variant).\n"
              << "  " << program << " map render --map <file.map> --out <file.png>\n"
              << "      [--elevation 0|1|2] [--max-dim N] [--roof] [--schematic|--objects]\n"
              << "      [--show-blockers] --data <dir-or-.dat> [...]\n"
              << "      Renders a map to a PNG (needs an off-screen GL context). --max-dim caps the\n"
              << "      longest side (default 1600); --roof draws the roof layer over the floor.\n"
              << "      --schematic flat-colours floor tiles by id + marks objects by category (with a\n"
              << "      colour legend); --objects mutes the floor to grey so the object markers pop\n"
              << "      (for checking scatter); --show-blockers also marks FLAT objects.\n"
              << "  " << program << " map extract-pattern --map <file.map> --out <file.json> --name <name>\n"
              << "      [--pids id,id,...] [--anchor <hex>] [--radius N] [--elevation 0|1|2]\n"
              << "      [--include-floor] [--include-roof] --data <dir-or-.dat> [--data <...>]\n"
              << "      Captures a structure into a reusable stamp. Locate it with --pids (proto ids\n"
              << "      from analyze) or --anchor <hex>; --radius (default 2) grows the capture region.\n"
              << "      --include-floor captures the ground; --include-roof captures the roof layer (a\n"
              << "      tent/building roof is tiles, not an object — without it the stamp is topless).\n"
              << "      Feed the .json to generate --stamp.\n"
              << "  " << program << " map describe-script --index <n> [--locale english]\n"
              << "      --data <dir-or-.dat> [--data <...>]\n"
              << "      Describe a script by its scripts.lst program index (the script_id analyze reports):\n"
              << "      filename, the .ssl source (if a source tree like FRP scripts_src is mounted via\n"
              << "      --data) and the dialog .msg lines, as JSON.\n"
              << "  " << program << " map reachability --map <path> --data <dir-or-.dat> [--data <...>]\n"
              << "      Per-elevation reachability from the entry points (player start + exit grids, doors\n"
              << "      passable): reachable vs walkable hexes and any critters/items cut off (orphaned).\n"
              << "  --data may be a Fallout 2 data directory or a .dat archive; repeat to mount several.\n";
}

bool isKnownSubcommand(const std::vector<std::string>& args) {
    return args.size() >= 2 && args[0] == "map"
        && (args[1] == "analyze" || args[1] == "generate" || args[1] == "render" || args[1] == "extract-pattern"
            || args[1] == "describe-script" || args[1] == "reachability");
}

bool generateMissingRequired(const CliArgs& cli) {
    return cli.generate && (cli.gen.scriptPath.empty() || cli.gen.outPath.empty());
}

bool renderMissingRequired(const CliArgs& cli) {
    return cli.render && (cli.ren.mapPath.empty() || cli.ren.outPath.empty());
}

bool extractMissingRequired(const CliArgs& cli) {
    return cli.extract && (cli.ext.mapPath.empty() || cli.ext.outPath.empty() || cli.ext.name.empty());
}

// Parse a comma-separated list of proto ids/PIDs into `out` (decimal or 0x-hex).
void parsePids(const std::string& csv, std::vector<std::uint32_t>& out) {
    std::size_t start = 0;
    while (start < csv.size()) {
        const std::size_t comma = csv.find(',', start);
        const std::string token = csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!token.empty()) {
            out.push_back(static_cast<std::uint32_t>(std::stoul(token, nullptr, 0)));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
}

// Consume the argument(s) starting at args[i]. Returns how many tokens were consumed (1 for a
// flag-less map/positional, 2 for a flag + its value), or 0 on error (after printing the reason).
// Keeping the token count explicit lets the caller advance the index instead of mutating it here.
int consumeArg(const std::vector<std::string>& args, std::size_t i, CliArgs& out, const char* program) {
    const std::string& arg = args[i];
    const bool valueFlag = arg == "--data"
        || (out.generate && (arg == "--script" || arg == "--out" || arg == "--elevation" || arg == "--arg" || arg == "--stamp"))
        || (out.render && (arg == "--map" || arg == "--out" || arg == "--elevation" || arg == "--max-dim"))
        || (out.extract && (arg == "--map" || arg == "--out" || arg == "--name" || arg == "--elevation" || arg == "--pids" || arg == "--anchor" || arg == "--radius"))
        || (out.describeScript && (arg == "--index" || arg == "--locale"))
        || (out.reachability && arg == "--map");

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
    if (out.generate && arg == "--stamp") {
        // name=path -> loadable in the script via api:placeStamp(name, ...)
        const std::string& kv = args[i + 1];
        const auto eq = kv.find('=');
        if (eq == std::string::npos || eq == 0) {
            std::cerr << "error: --stamp expects name=path with a non-empty name, got: " << kv << "\n";
            printUsage(program);
            return 0;
        }
        out.gen.stamps[kv.substr(0, eq)] = kv.substr(eq + 1);
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
    if (out.render && arg == "--objects") {
        out.ren.objects = true;
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
    if (out.extract && arg == "--map") {
        out.ext.mapPath = args[i + 1];
        return 2;
    }
    if (out.extract && arg == "--out") {
        out.ext.outPath = args[i + 1];
        return 2;
    }
    if (out.extract && arg == "--name") {
        out.ext.name = args[i + 1];
        return 2;
    }
    if (out.extract && arg == "--elevation") {
        out.ext.elevation = std::stoi(args[i + 1]);
        return 2;
    }
    if (out.extract && arg == "--pids") {
        parsePids(args[i + 1], out.ext.pids);
        return 2;
    }
    if (out.extract && arg == "--anchor") {
        out.ext.anchorHex = std::stoi(args[i + 1]);
        return 2;
    }
    if (out.extract && arg == "--radius") {
        out.ext.radius = std::stoi(args[i + 1]);
        return 2;
    }
    if (out.extract && arg == "--include-floor") {
        out.ext.includeFloor = true;
        return 1;
    }
    if (out.extract && arg == "--include-roof") {
        out.ext.includeRoof = true;
        return 1;
    }
    if (out.extract) {
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (out.describeScript && arg == "--index") {
        out.desc.programIndex = std::stoi(args[i + 1]);
        return 2;
    }
    if (out.describeScript && arg == "--locale") {
        out.desc.locale = args[i + 1];
        return 2;
    }
    if (out.describeScript) {
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (out.reachability && arg == "--map") {
        out.reach.mapPath = args[i + 1];
        return 2;
    }
    if (out.reachability) {
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (arg == "--json") { // analyze only: machine-readable output for the MCP
        out.analyze.json = true;
        return 1;
    }
    if (arg == "--palette") { // analyze only: just the weighted generation palette
        out.analyze.palette = true;
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
    out.extract = args[1] == "extract-pattern";
    out.describeScript = args[1] == "describe-script";
    out.reachability = args[1] == "reachability";

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
    if (extractMissingRequired(out)) {
        std::cerr << "error: extract-pattern requires --map, --out and --name\n";
        printUsage(program);
        return 2;
    }
    if (out.describeScript && out.desc.programIndex < 0) {
        std::cerr << "error: describe-script requires --index <n> (a 0-based scripts.lst program index)\n";
        printUsage(program);
        return 2;
    }
    if (out.reachability && out.reach.mapPath.empty()) {
        std::cerr << "error: reachability requires --map <path>\n";
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
    // Mount the bundled resources (blank.frm, scripts, …) the way the editor does, so e.g. the
    // natural map render finds art/tiles/blank.frm even though it isn't in master.dat.
    if (const auto bundled = geck::cli::findBundledResources(argv[0]); !bundled.empty()) {
        resources.files().addDataPath(bundled);
    }

    if (cli.generate) {
        return geck::cli::generateMap(resources, cli.gen, std::cout);
    }
    if (cli.render) {
        return geck::cli::renderMap(resources, cli.ren, std::cout);
    }
    if (cli.extract) {
        return geck::cli::extractPattern(resources, cli.ext, std::cout);
    }
    if (cli.describeScript) {
        return geck::cli::describeScript(resources, cli.desc, std::cout);
    }
    if (cli.reachability) {
        return geck::cli::analyzeReachability(resources, cli.reach, std::cout);
    }
    return geck::cli::analyzeMaps(resources, cli.analyze, std::cout);
}
