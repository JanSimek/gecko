#include "cli/BundledResources.h"
#include "cli/FrmInspect.h"
#include "cli/MapAnalyzer.h"
#include "cli/MapDescribe.h"
#include "cli/MapGraph.h"
#include "cli/WorldMap.h"
#include "cli/WorldEncounters.h"
#include "cli/GlobalVars.h"
#include "cli/Quests.h"
#include "cli/Endings.h"
#include "cli/GvarRefs.h"
#include "cli/MapGenerator.h"
#include "cli/MapRender.h"
#include "cli/MapReachability.h"
#include "cli/PatternExtract.h"
#include "cli/ResourceInspect.h"
#include "cli/ScriptIntrospect.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <iostream>
#include <limits>
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
    bool describeMap = false;
    bool graph = false;
    bool world = false;
    bool encounters = false;
    bool gvars = false;
    bool quests = false;
    bool endings = false;
    bool findGvar = false;
    bool dumpGrid = false;
    std::string findGvarName;
    std::vector<std::string> dataPaths;
    geck::cli::AnalyzeOptions analyze;
    geck::cli::DumpGridOptions dumpOpts;
    geck::cli::GenerateOptions gen;
    geck::cli::RenderOptions ren;
    geck::cli::ExtractOptions ext;
    geck::cli::DescribeScriptOptions desc;
    geck::cli::ReachabilityOptions reach;
    geck::cli::DescribeMapOptions describeMapOpts;
    geck::cli::MapGraphOptions graphOpts;
};

void printUsage(const char* program) {
    std::cerr << "Usage:\n"
              << "  " << program << " map analyze [--json|--palette] --data <dir-or-.dat> [--data <...>] [map ...]\n"
              << "      Reports ground-tile and object (scenery/wall/critter/...) usage across maps,\n"
              << "      per map and aggregated. With no map arguments, every map under maps/ is analysed.\n"
              << "      --json emits machine-readable output (for the MCP) instead of the human report;\n"
              << "      --palette emits just the weighted floor + scenery palette a generator script needs.\n"
              << "  " << program << " map generate --script <file.luau> --out <file.map>\n"
              << "      [--in <file.map>] [--elevation 0|1|2] [--count N]\n"
              << "      [--arg key=value ...] [--stamp name=file.json ...]\n"
              << "      --data <dir-or-.dat> [--data <...>]\n"
              << "      Runs a Luau generation script against a map and writes the result — a fresh\n"
              << "      empty map by default. --in loads an existing map (VFS path or a file on disk)\n"
              << "      for the script to decorate/edit instead. --count N generates N maps in one\n"
              << "      run, written as <out>_1.map .. <out>_N.map with consecutive seeds from the\n"
              << "      base (--arg seed=N, or a random base reported so the batch can reproduce).\n"
              << "      --arg passes parameters to the script (read as args.key); --stamp pre-loads a\n"
              << "      pattern stamp the script places with api:placeStamp(name, anchorHex, variant).\n"
              << "  " << program << " map render --map <file.map> --out <file.png>\n"
              << "      [--elevation 0|1|2] [--max-dim N] [--roof] [--schematic|--objects|--semantic]\n"
              << "      [--show-blockers] [--show-unreachable] [--full] --data <dir-or-.dat> [...]\n"
              << "      Renders a map to a PNG (needs an off-screen GL context). --max-dim caps the\n"
              << "      longest side (default 1600); --roof draws the roof layer over the floor; --full\n"
              << "      frames the whole iso grid (the full playable area) instead of cropping to content.\n"
              << "      --schematic flat-colours floor tiles by id + marks objects by category (with a\n"
              << "      colour legend); --objects mutes the floor to grey so the object markers pop\n"
              << "      (for checking scatter); --semantic greys the floor and colours markers by role\n"
              << "      (exit grids, critters by team, scripted ringed); --show-blockers also marks FLAT.\n"
              << "      --show-unreachable (natural style) shades hexes cut off from the player start and\n"
              << "      every exit grid, the visual form of `map reachability`.\n"
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
              << "      Per-elevation reachability from the entry points (player start + exit grids;\n"
              << "      optimistic: doors passable): reachable/walkable hexes, exits, orphaned content.\n"
              << "  " << program << " map describe-map --map <path> --data <dir-or-.dat> [--data <...>]\n"
              << "      One digest composing analyze + reachability for a map (header, biome, structures,\n"
              << "      critter roster with AI + scripts, exits, reachability), as JSON.\n"
              << "  " << program << " map dump-grid --map <path> [--elevation 0|1|2] [--roof]\n"
              << "      [--no-floor] [--no-objects] --data <dir-or-.dat> [--data <...>]\n"
              << "      Dumps the RAW spatial layout as JSON: per elevation the floor (and --roof) tile-id\n"
              << "      grid (row-major, 100 wide; emptyTile marks empty) and every object's\n"
              << "      {pid,number,type,name,hex,col,row,dir,flat}. The per-cell data behind analyze's\n"
              << "      adjacency/clusters — for learning exact tile placement + scatter density.\n"
              << "  " << program << " map graph [map ...] --data <dir-or-.dat> [--data <...>]\n"
              << "      The exit-grid connectivity graph: how maps link via exit grids WITHIN a location\n"
              << "      (+ worldmap hand-off edges), named via maps.txt/map.msg. Not inter-city travel\n"
              << "      (that's the world map / city.txt). No map arguments = every map.\n"
              << "  " << program << " map world --data <dir-or-.dat> [--data <...>]\n"
              << "      The worldmap layer from city.txt: every area (location) with its world position,\n"
              << "      size, the maps it contains, and the straight-line distances between all areas.\n"
              << "  " << program << " map encounters --data <dir-or-.dat> [--data <...>]\n"
              << "      The worldmap terrain types and random-encounter group tables from worldmap.txt.\n"
              << "  " << program << " map gvars --data <dir-or-.dat> [--data <...>]\n"
              << "      The global-variable dictionary (vault13.gam): each GVAR index -> name + default.\n"
              << "  " << program << " map quests --data <dir-or-.dat> [--data <...>]\n"
              << "      The quest registry (quests.txt): each quest's area, tracking gvar, thresholds, text.\n"
              << "  " << program << " map endings --data <dir-or-.dat> [--data <...>]\n"
              << "      The endgame win-condition table (endgame.txt): each slide's gvar==value, art, narrator.\n"
              << "  " << program << " map find-gvar <GVAR_NAME> --data <dir-or-.dat> [--data <...>]\n"
              << "      Scripts that read/write a global variable (needs an .ssl source tree mounted): set vs get.\n"
              << "  " << program << " frm info <name-or-fid> --data <dir-or-.dat> [--data <...>]\n"
              << "      FRM metadata as JSON: resolvedArtPath, fid, directionCount, framesPerDirection, and a\n"
              << "      per-frame array of {direction,frame,width,height,offsetX,offsetY}. Accepts an art name\n"
              << "      (ext2grd1 or art/misc/ext2grd1.frm) or a FID (0x05000021 / decimal).\n"
              << "  " << program << " frm render <name-or-fid> --out <file.png> [--dir N] [--frame N]\n"
              << "      --data <dir-or-.dat> [--data <...>]\n"
              << "      Render the sprite to a PNG (needs an off-screen GL context). Default: a grid of all\n"
              << "      6 directions x all frames on a checkerboard. --dir N renders one direction; --frame N\n"
              << "      one frame index. Reports the output path, image size, and grid layout (DxF).\n"
              << "  " << program << " frm resolve <fid> --data <dir-or-.dat> [--data <...>]\n"
              << "      Decode a FID (0x.. hex or decimal) to JSON {fid, type, index, artPath}.\n"
              << "  " << program << " frm list <glob> --data <dir-or-.dat> [--data <...>]\n"
              << "      Art entries whose name matches <glob> (e.g. ext2grd*), each {name, artPath, fid}.\n"
              << "  " << program << " resource find <path> --data <dir-or-.dat> [--data <...>]\n"
              << "      Locate a VFS path (e.g. art/tiles/gras030.frm) in the mounted data: JSON\n"
              << "      {path, found, source:{kind,path,label}} — which .dat/dir provides it, or found=false.\n"
              << "  " << program << " resource list <glob> --data <dir-or-.dat> [--data <...>]\n"
              << "      Mounted entries matching <glob> ('*'/'?', e.g. art/tiles/gras*), each tagged with its\n"
              << "      source. Browse a DAT/data set without extracting (result is capped; 'truncated' flags it).\n"
              << "  " << program << " resource missing <map> --data <dir-or-.dat> [--data <...>]\n"
              << "      Art a map references but that does NOT resolve in the mounted data: missing tiles\n"
              << "      (by tiles.lst id) and object art (by FID). Diagnoses 'why won't this map load fully'.\n"
              << "  --data may be a Fallout 2 data directory or a .dat archive; repeat to mount several.\n";
}

// Checked integer parse for `--flag <value>` arguments. std::stoi throws std::invalid_argument /
// std::out_of_range on bad input, which would otherwise terminate the CLI; this catches that and
// reports a clean "invalid value" error so the subcommand fails gracefully. Returns false (after
// printing why) on a non-numeric / out-of-range / trailing-garbage value.
bool parseIntFlag(const std::string& flag, const std::string& value, int& out, const char* program) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed, 10);
        if (consumed != value.size()) {
            std::cerr << "error: " << flag << " expects an integer, got: " << value << "\n";
            printUsage(program);
            return false;
        }
        out = parsed;
        return true;
    } catch (const std::exception&) {
        std::cerr << "error: " << flag << " expects an integer, got: " << value << "\n";
        printUsage(program);
        return false;
    }
}

// Checked unsigned parse for `--flag <value>` (base 0: 0x.. hex, else decimal). Same rationale as
// parseIntFlag: std::stoul throws on bad input and would terminate the CLI.
bool parseUnsignedFlag(const std::string& flag, const std::string& value, unsigned long& out, const char* program) {
    // Require a leading digit. This rejects a leading '+'/'-' and, crucially, leading
    // whitespace: std::stoul skips whitespace and then accepts a sign, so " -1" would
    // otherwise slip past a bare front()=='-' check and wrap to a huge value.
    if (value.empty() || value.front() < '0' || value.front() > '9') {
        std::cerr << "error: " << flag << " expects a non-negative integer, got: " << value << "\n";
        printUsage(program);
        return false;
    }
    try {
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(value, &consumed, 0);
        if (consumed != value.size()) {
            std::cerr << "error: " << flag << " expects a non-negative integer, got: " << value << "\n";
            printUsage(program);
            return false;
        }
        out = parsed;
        return true;
    } catch (const std::exception&) {
        std::cerr << "error: " << flag << " expects a non-negative integer, got: " << value << "\n";
        printUsage(program);
        return false;
    }
}

bool isKnownSubcommand(const std::vector<std::string>& args) {
    return args.size() >= 2 && args[0] == "map"
        && (args[1] == "analyze" || args[1] == "generate" || args[1] == "render" || args[1] == "extract-pattern"
            || args[1] == "describe-script" || args[1] == "reachability" || args[1] == "describe-map"
            || args[1] == "graph" || args[1] == "world" || args[1] == "encounters"
            || args[1] == "gvars" || args[1] == "quests" || args[1] == "endings" || args[1] == "find-gvar"
            || args[1] == "dump-grid");
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

// Parse a comma-separated list of proto ids/PIDs into `out` (decimal or 0x-hex). Returns false
// (after printing why) on a non-numeric token, so the caller fails gracefully instead of crashing.
bool parsePids(const std::string& csv, std::vector<std::uint32_t>& out, const char* program) {
    std::size_t start = 0;
    while (start < csv.size()) {
        const std::size_t comma = csv.find(',', start);
        const std::string token = csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!token.empty()) {
            unsigned long value = 0;
            if (!parseUnsignedFlag("--pids", token, value, program)) {
                return false;
            }
            if (value > std::numeric_limits<std::uint32_t>::max()) {
                std::cerr << "error: --pids value out of range (max 0xFFFFFFFF), got: " << token << "\n";
                printUsage(program);
                return false;
            }
            out.push_back(static_cast<std::uint32_t>(value));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return true;
}

// Consume the argument(s) starting at args[i]. Returns how many tokens were consumed (1 for a
// flag-less map/positional, 2 for a flag + its value), or 0 on error (after printing the reason).
// Keeping the token count explicit lets the caller advance the index instead of mutating it here.
int consumeArg(const std::vector<std::string>& args, std::size_t i, CliArgs& out, const char* program) {
    const std::string& arg = args[i];
    const bool valueFlag = arg == "--data"
        || (out.generate && (arg == "--script" || arg == "--out" || arg == "--in" || arg == "--elevation" || arg == "--count" || arg == "--arg" || arg == "--stamp"))
        || (out.render && (arg == "--map" || arg == "--out" || arg == "--elevation" || arg == "--max-dim"))
        || (out.extract && (arg == "--map" || arg == "--out" || arg == "--name" || arg == "--elevation" || arg == "--pids" || arg == "--anchor" || arg == "--radius"))
        || (out.describeScript && (arg == "--index" || arg == "--locale"))
        || (out.reachability && arg == "--map")
        || (out.describeMap && arg == "--map")
        || (out.dumpGrid && (arg == "--map" || arg == "--elevation"));

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
    if (out.generate && arg == "--in") {
        out.gen.inPath = args[i + 1];
        return 2;
    }
    if (out.generate && arg == "--elevation") {
        return parseIntFlag(arg, args[i + 1], out.gen.elevation, program) ? 2 : 0;
    }
    if (out.generate && arg == "--count") {
        return parseIntFlag(arg, args[i + 1], out.gen.count, program) ? 2 : 0;
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
        return parseIntFlag(arg, args[i + 1], out.ren.elevation, program) ? 2 : 0;
    }
    if (out.render && arg == "--max-dim") {
        unsigned long maxDim = 0;
        if (!parseUnsignedFlag(arg, args[i + 1], maxDim, program)) {
            return 0;
        }
        if (maxDim > std::numeric_limits<unsigned int>::max()) {
            std::cerr << "error: --max-dim value out of range, got: " << args[i + 1] << "\n";
            printUsage(program);
            return 0;
        }
        out.ren.maxDimension = static_cast<unsigned int>(maxDim);
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
    if (out.render && arg == "--semantic") {
        out.ren.semantic = true;
        return 1;
    }
    if (out.render && arg == "--show-blockers") {
        out.ren.showBlockers = true;
        return 1;
    }
    if (out.render && arg == "--full") {
        out.ren.fullExtent = true;
        return 1;
    }
    if (out.render && arg == "--exit-dots") {
        out.ren.exitDots = true;
        return 1;
    }
    if (out.render && arg == "--show-unreachable") {
        out.ren.showUnreachable = true;
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
        return parseIntFlag(arg, args[i + 1], out.ext.elevation, program) ? 2 : 0;
    }
    if (out.extract && arg == "--pids") {
        return parsePids(args[i + 1], out.ext.pids, program) ? 2 : 0;
    }
    if (out.extract && arg == "--anchor") {
        return parseIntFlag(arg, args[i + 1], out.ext.anchorHex, program) ? 2 : 0;
    }
    if (out.extract && arg == "--radius") {
        return parseIntFlag(arg, args[i + 1], out.ext.radius, program) ? 2 : 0;
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
        return parseIntFlag(arg, args[i + 1], out.desc.programIndex, program) ? 2 : 0;
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
    if (out.describeMap && arg == "--map") {
        out.describeMapOpts.mapPath = args[i + 1];
        return 2;
    }
    if (out.describeMap) {
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (out.dumpGrid && arg == "--map") {
        out.dumpOpts.map = args[i + 1];
        return 2;
    }
    if (out.dumpGrid && arg == "--elevation") {
        return parseIntFlag(arg, args[i + 1], out.dumpOpts.elevation, program) ? 2 : 0;
    }
    if (out.dumpGrid && arg == "--roof") {
        out.dumpOpts.roof = true;
        return 1;
    }
    if (out.dumpGrid && arg == "--no-floor") {
        out.dumpOpts.floor = false;
        return 1;
    }
    if (out.dumpGrid && arg == "--no-objects") {
        out.dumpOpts.objects = false;
        return 1;
    }
    if (out.dumpGrid) {
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (out.graph) { // trailing positional args are the maps to include (empty = all)
        out.graphOpts.maps.push_back(arg);
        return 1;
    }
    if (out.world) { // world takes no arguments beyond --data (handled above)
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (out.encounters) { // encounters takes no arguments beyond --data (handled above)
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (out.gvars || out.quests || out.endings) { // these take no arguments beyond --data (handled above)
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (out.findGvar) { // a single positional: the GVAR_* name to search for
        if (arg.rfind("--", 0) == 0 || !out.findGvarName.empty()) {
            std::cerr << "error: find-gvar takes exactly one GVAR name: " << arg << "\n";
            printUsage(program);
            return 0;
        }
        out.findGvarName = arg;
        return 1;
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
    out.describeMap = args[1] == "describe-map";
    out.graph = args[1] == "graph";
    out.world = args[1] == "world";
    out.encounters = args[1] == "encounters";
    out.gvars = args[1] == "gvars";
    out.quests = args[1] == "quests";
    out.endings = args[1] == "endings";
    out.findGvar = args[1] == "find-gvar";
    out.dumpGrid = args[1] == "dump-grid";

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
    if (out.dumpGrid && out.dumpOpts.map.empty()) {
        std::cerr << "error: dump-grid requires --map <path>\n";
        printUsage(program);
        return 2;
    }
    return std::nullopt;
}

// --- frm subcommand ---------------------------------------------------------------------------
// The `frm` family (info/render/resolve/list) is self-contained: one positional target plus a few
// flags, and its own --data list. Kept apart from the `map` parser above so neither grows complex.
struct FrmArgs {
    std::string action; // info | render | resolve | list
    std::string target; // art name / FID / glob
    std::string outPath;
    int direction = -1;
    int frame = -1;
    std::vector<std::string> dataPaths;
};

bool isFrmAction(const std::string& action) {
    return action == "info" || action == "render" || action == "resolve" || action == "list";
}

// Apply a recognized `--flag value` pair to `out`. Returns false (after printing why) when the
// numeric flags (--dir / --frame) get a non-integer value, so the caller fails gracefully instead
// of letting std::stoi throw and terminate the CLI.
bool applyFrmValueFlag(const std::string& flag, const std::string& value, FrmArgs& out, const char* program) {
    if (flag == "--data") {
        out.dataPaths.push_back(value);
    } else if (flag == "--out") {
        out.outPath = value;
    } else if (flag == "--dir") {
        return parseIntFlag(flag, value, out.direction, program);
    } else if (flag == "--frame") {
        return parseIntFlag(flag, value, out.frame, program);
    }
    return true;
}

// Parse the tokens after `frm <action>` into `out`. Returns false (after printing why) on a bad flag.
bool parseFrmArgs(const std::vector<std::string>& args, const char* program, FrmArgs& out) {
    for (std::size_t i = 2; i < args.size();) {
        const std::string& arg = args[i];
        const bool valueFlag = arg == "--data" || arg == "--out" || arg == "--dir" || arg == "--frame";
        if (valueFlag && i + 1 >= args.size()) {
            std::cerr << "error: " << arg << " needs a value\n";
            printUsage(program);
            return false;
        }
        if (valueFlag) {
            if (!applyFrmValueFlag(arg, args[i + 1], out, program)) {
                return false;
            }
            i += 2;
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "error: unexpected argument: " << arg << "\n";
            printUsage(program);
            return false;
        } else if (out.target.empty()) {
            out.target = arg;
            ++i;
        } else {
            std::cerr << "error: frm " << out.action << " takes one positional argument: " << arg << "\n";
            printUsage(program);
            return false;
        }
    }
    return true;
}

// Mount the data paths plus the bundled fallback resources the way main() does for `map`.
void mountData(geck::resource::GameResources& resources, const std::vector<std::string>& dataPaths, const char* argv0) {
    for (const auto& path : dataPaths) {
        resources.files().addDataPath(path);
    }
    if (const auto bundled = geck::cli::findBundledResources(argv0); !bundled.empty()) {
        resources.files().addDataPath(bundled);
    }
}

int dispatchFrm(geck::resource::GameResources& resources, const FrmArgs& fa) {
    if (fa.action == "info") {
        return geck::cli::frmInfo(resources, fa.target, std::cout);
    }
    if (fa.action == "resolve") {
        return geck::cli::resolveFidCommand(resources, fa.target, std::cout);
    }
    if (fa.action == "list") {
        return geck::cli::listFrms(resources, fa.target, std::cout);
    }
    geck::cli::FrmRenderOptions opts;
    opts.target = fa.target;
    opts.outPath = fa.outPath;
    opts.direction = fa.direction;
    opts.frame = fa.frame;
    return geck::cli::frmRender(resources, opts, std::cout);
}

// Run a `frm ...` command end to end (parse, validate, mount, dispatch). Returns the exit code.
int runFrmCommand(const std::vector<std::string>& args, const char* program) {
    FrmArgs fa;
    fa.action = args[1];
    if (!parseFrmArgs(args, program, fa)) {
        return 2;
    }
    if (fa.target.empty()) {
        std::cerr << "error: frm " << fa.action << " requires a <name-or-fid> argument\n";
        printUsage(program);
        return 2;
    }
    if (fa.dataPaths.empty()) {
        std::cerr << "error: at least one --data <path> is required\n";
        printUsage(program);
        return 2;
    }
    if (fa.action == "render" && fa.outPath.empty()) {
        std::cerr << "error: frm render requires --out <file.png>\n";
        printUsage(program);
        return 2;
    }

    spdlog::set_level(spdlog::level::off);
    geck::resource::GameResources resources;
    mountData(resources, fa.dataPaths, program);
    return dispatchFrm(resources, fa);
}

// --- resource subcommand ----------------------------------------------------------------------
// The `resource` family (find/list/missing) inspects the mounted data itself — which source
// provides a path, what matches a glob, what a map references but can't resolve. Self-contained
// like `frm`: one positional (path / glob / map) plus its own --data list.
struct ResourceArgs {
    std::string action; // find | list | missing
    std::string target; // path / glob / map
    std::vector<std::string> dataPaths;
};

bool isResourceAction(const std::string& action) {
    return action == "find" || action == "list" || action == "missing";
}

// Parse the tokens after `resource <action>` into `out`. Returns false (after printing why) on a
// bad flag or a second positional.
bool parseResourceArgs(const std::vector<std::string>& args, const char* program, ResourceArgs& out) {
    for (std::size_t i = 2; i < args.size();) {
        const std::string& arg = args[i];
        if (arg == "--data") {
            if (i + 1 >= args.size()) {
                std::cerr << "error: --data needs a value\n";
                printUsage(program);
                return false;
            }
            out.dataPaths.push_back(args[i + 1]);
            i += 2;
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "error: unexpected argument: " << arg << "\n";
            printUsage(program);
            return false;
        } else if (out.target.empty()) {
            out.target = arg;
            ++i;
        } else {
            std::cerr << "error: resource " << out.action << " takes one positional argument: " << arg << "\n";
            printUsage(program);
            return false;
        }
    }
    return true;
}

int dispatchResource(geck::resource::GameResources& resources, const ResourceArgs& ra) {
    if (ra.action == "find") {
        return geck::cli::resourceFind(resources, ra.target, std::cout);
    }
    if (ra.action == "list") {
        return geck::cli::resourceList(resources, ra.target, std::cout);
    }
    return geck::cli::resourceMissing(resources, ra.target, std::cout);
}

// Run a `resource ...` command end to end (parse, validate, mount, dispatch). Returns the exit code.
int runResourceCommand(const std::vector<std::string>& args, const char* program) {
    ResourceArgs ra;
    ra.action = args[1];
    if (!parseResourceArgs(args, program, ra)) {
        return 2;
    }
    if (ra.target.empty()) {
        const char* what = ra.action == "find" ? "<path>" : (ra.action == "list" ? "<glob>" : "<map>");
        std::cerr << "error: resource " << ra.action << " requires a " << what << " argument\n";
        printUsage(program);
        return 2;
    }
    if (ra.dataPaths.empty()) {
        std::cerr << "error: at least one --data <path> is required\n";
        printUsage(program);
        return 2;
    }

    spdlog::set_level(spdlog::level::off);
    geck::resource::GameResources resources;
    mountData(resources, ra.dataPaths, program);
    return dispatchResource(resources, ra);
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);

    // The `frm` family is parsed and run on its own path (its own positional + flags), separate from
    // the `map` subcommands below.
    if (args.size() >= 2 && args[0] == "frm" && isFrmAction(args[1])) {
        return runFrmCommand(args, argv[0]);
    }

    // The `resource` family (data-inspection) is likewise parsed and run on its own path.
    if (args.size() >= 2 && args[0] == "resource" && isResourceAction(args[1])) {
        return runResourceCommand(args, argv[0]);
    }

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
    if (cli.describeMap) {
        return geck::cli::describeMap(resources, cli.describeMapOpts, std::cout);
    }
    if (cli.graph) {
        return geck::cli::buildMapGraph(resources, cli.graphOpts, std::cout);
    }
    if (cli.world) {
        return geck::cli::buildWorldMap(resources, std::cout);
    }
    if (cli.encounters) {
        return geck::cli::buildWorldEncounters(resources, std::cout);
    }
    if (cli.gvars) {
        return geck::cli::buildGlobalVars(resources, std::cout);
    }
    if (cli.quests) {
        return geck::cli::buildQuests(resources, std::cout);
    }
    if (cli.endings) {
        return geck::cli::buildEndings(resources, std::cout);
    }
    if (cli.findGvar) {
        if (cli.findGvarName.empty()) {
            std::cerr << "error: find-gvar requires a GVAR name (e.g. find-gvar GVAR_ARROYO_RETURN_GECK)\n";
            return 2;
        }
        return geck::cli::findGvarRefs(resources, cli.findGvarName, std::cout);
    }
    if (cli.dumpGrid) {
        return geck::cli::dumpMapGrid(resources, cli.dumpOpts, std::cout);
    }
    return geck::cli::analyzeMaps(resources, cli.analyze, std::cout);
}
