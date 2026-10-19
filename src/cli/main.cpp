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
#include "cli/Party.h"
#include "cli/MapExamples.h"
#include "cli/GvarRefs.h"
#include "cli/MapGenerator.h"
#include "cli/MapRender.h"
#include "cli/MapReachability.h"
#include "cli/PatternExtract.h"
#include "cli/ProtoExport.h"
#include "cli/ResourceInspect.h"
#include "cli/ScriptIntrospect.h"
#include "cli/ScriptLint.h"
#include "cli/ScriptRefs.h"
#include "reader/TextParsing.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
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
    bool party = false;
    bool examples = false;
    bool findGvar = false;
    bool dumpGrid = false;
    bool lintScript = false;
    bool scriptRefs = false;
    std::string findGvarName;
    geck::cli::LintOptions lint;
    bool lintNotes = false; // --notes: also for --all, where notes are off by default
    geck::cli::ScriptRefsOptions refs;
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
    geck::cli::WorldMapOptions worldOpts;
    geck::cli::ExamplesOptions examplesOpts;
    int encountersX = -1; ///< map encounters --x/--y: describe this worldmap position instead
    int encountersY = -1;
};

void printUsage(const char* program) {
    std::cerr << "Usage:\n"
              << "  " << program << " map analyze [--json|--palette] --data <dir-or-.dat> [--data <...>] [map ...]\n"
              << "      Reports ground-tile and object (scenery/wall/critter/...) usage across maps,\n"
              << "      per map and aggregated. With no map arguments, every map under maps/ is analysed.\n"
              << "      --json emits machine-readable output (for the MCP) instead of the human report;\n"
              << "      --palette emits just the weighted floor + scenery palette a generator script needs.\n"
              << "  " << program << " map generate --script <file.luau> --out <file.map>\n"
              << "      [--in <file.map>] [--elevation 0|1|2] [--count N] [--time-budget SECONDS]\n"
              << "      [--arg key=value ...] [--stamp name=file.json ...]\n"
              << "      --data <dir-or-.dat> [--data <...>]\n"
              << "      Runs a Luau generation script against a map and writes the result — a fresh\n"
              << "      empty map by default. --in loads an existing map (VFS path or a file on disk)\n"
              << "      for the script to decorate/edit instead. --count N generates N maps in one\n"
              << "      run, written as <out>_1.map .. <out>_N.map with consecutive seeds from the\n"
              << "      base (--arg seed=N, or a random base reported so the batch can reproduce).\n"
              << "      --arg passes parameters to the script (read as args.key); --stamp pre-loads a\n"
              << "      pattern stamp the script places with api:placeStamp(name, anchorHex, variant).\n"
              << "      --time-budget stops a script that runs longer than SECONDS per map (default: none).\n"
              << "  " << program << " map render --map <file.map> --out <file.png>\n"
              << "      [--elevation 0|1|2] [--max-dim N] [--roof] [--schematic|--objects|--semantic]\n"
              << "      [--show-blockers] [--show-unreachable] [--full] [--crop-hex H [--crop-extent PX]]\n"
              << "      --data <dir-or-.dat> [...]\n"
              << "      Renders a map to a PNG (needs an off-screen GL context). --max-dim caps the\n"
              << "      longest side (default 1600); --roof draws the roof layer over the floor; --full\n"
              << "      frames the whole iso grid (the full playable area) instead of cropping to content.\n"
              << "      --schematic flat-colours floor tiles by id + marks objects by category (with a\n"
              << "      colour legend); --objects mutes the floor to grey so the object markers pop\n"
              << "      (for checking scatter); --semantic greys the floor and colours markers by role\n"
              << "      (exit grids, critters by team, scripted ringed); --show-blockers also marks FLAT.\n"
              << "      --show-unreachable (natural style) shades hexes cut off from the player start and\n"
              << "      every exit grid, the visual form of `map reachability`.\n"
              << "      --crop-hex H zooms into a square around hex H (0..39999), ±--crop-extent pixels\n"
              << "      (default 220), upscaled to --max-dim — to read individual pieces (a rim corner,\n"
              << "      a scatter cluster) that are unreadable in a whole-map render.\n"
              << "  " << program << " map extract-pattern --map <file.map> --out <file.json> --name <name>\n"
              << "      [--pids id,id,...] [--anchor <hex>] [--radius N] [--elevation 0|1|2]\n"
              << "      [--include-floor] [--include-roof] --data <dir-or-.dat> [--data <...>]\n"
              << "      Captures a structure into a reusable stamp. Locate it with --pids (proto ids\n"
              << "      from analyze) or --anchor <hex>; --radius (default 2) grows the capture region.\n"
              << "      --include-floor captures the ground; --include-roof captures the roof layer (a\n"
              << "      tent/building roof is tiles, not an object — without it the stamp is topless).\n"
              << "      Feed the .json to generate --stamp.\n"
              << "  " << program << " map describe-script --index <n> [--locale english] [--analyze]\n"
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
              << "  " << program << " map graph [map ...] [--no-scripts] --data <dir-or-.dat> [--data <...>]\n"
              << "      The exit-grid connectivity graph: how maps link via exit grids WITHIN a location\n"
              << "      (+ worldmap hand-off edges), named via maps.txt/map.msg. Not inter-city travel\n"
              << "      (that's the world map / city.txt). No map arguments = every map. With an .ssl source\n"
              << "      tree mounted it adds scriptEdges: load_map transitions (caravans, escorts, dialogue\n"
              << "      outcomes) from the maps placing each script; --no-scripts skips them.\n"
              << "  " << program << " map world [--render <file.png>] [--no-markers]\n"
              << "      --data <dir-or-.dat> [--data <...>]\n"
              << "      The worldmap layer from city.txt: every area (location) with its world position,\n"
              << "      size, the maps it contains, and the straight-line distances between all areas.\n"
              << "      --render draws the worldmap itself to a PNG instead — the wrldmp tile grid with\n"
              << "      the area circles blended over it the way the engine does; --no-markers omits them.\n"
              << "  " << program << " map encounters [--x N --y N] --data <dir-or-.dat> [--data <...>]\n"
              << "      The worldmap terrain types, encounter frequencies, groups and tables from worldmap.txt.\n"
              << "      --x/--y (worldmap pixels) report the encounter odds at one position instead.\n"
              << "  " << program << " map gvars --data <dir-or-.dat> [--data <...>]\n"
              << "      The global-variable dictionary (vault13.gam): each GVAR index -> name + default, plus the\n"
              << "      roles the data files give it (quest, ending, death ending, karma, holodisk).\n"
              << "  " << program << " map quests --data <dir-or-.dat> [--data <...>]\n"
              << "      The quest registry (quests.txt): each quest's area, tracking gvar, thresholds, text.\n"
              << "  " << program << " map endings --data <dir-or-.dat> [--data <...>]\n"
              << "      The endgame slides (endgame.txt) and death endings (enddeath.txt) with their conditions\n"
              << "      and narration subtitles.\n"
              << "  " << program << " map examples [--kind K] [--pid N ...] [--name S] [--script S] [--calls S]\n"
              << "      [--carries S] [--near S [--near-radius N]] [--radius N] [--limit N] [map ...]\n"
              << "      --data <dir-or-.dat> [--data <...>]\n"
              << "      Shipped examples of a placement: every object matching all the filters, across the maps\n"
              << "      (or the ones named), with its script, inventory and what stands within --radius hexes.\n"
              << "      --calls keeps objects whose script calls that opcode/procedure (needs .ssl sources).\n"
              << "  " << program << " map party --data <dir-or-.dat> [--data <...>]\n"
              << "      The companions (party.txt): pid, combat options, level-up protos.\n"
              << "  " << program << " map find-gvar <GVAR_NAME> --data <dir-or-.dat> [--data <...>]\n"
              << "      Scripts that read/write a global variable (needs an .ssl source tree mounted): set vs get.\n"
              << "  " << program << " map script-refs <gvar|call|procedure|macro|pid|map> <NAME> --data <dir-or-.dat> [--data <...>]\n"
              << "      Every use of a gvar, an opcode or procedure call, a procedure reference, a macro, a constant\n"
              << "      PID (pid: number, 0x hex or PID_*) or a load_map target (map: file name or maps.txt index)\n"
              << "      in code the compiled scripts run (needs an .ssl source tree mounted); '*' lists all pid/map.\n"
              << "  " << program << " map lint-script <name> | --index <n> | --map <map> | --all [--notes] [--locale english]\n"
              << "      --data <dir-or-.dat> [--data <...>]\n"
              << "      Problems the compiler misses: local variables past scripts.lst's count, messages missing\n"
              << "      from the dialog .msg, global variables out of range (and, with --all, read but never set),\n"
              << "      timers without a handler, objects created from missing protos or onto impossible hexes,\n"
              << "      unreadable code. --map checks the scripts a map places, and how it places them. With\n"
              << "      --notes (default for one script): procedures that never run, unused local variables.\n"
              << "      A script without a mounted .ssl source is checked from its compiled .int (fromBytecode):\n"
              << "      the same checks but syntax and unused procedures, by procedure rather than line.\n"
              << "  " << program << " script lint <file.ssl> [file.ssl ...] [--data <dir-or-.dat> ...] [--notes] [--json]\n"
              << "      The same checks on .ssl files on disk, printed as file:line: severity: message [check];\n"
              << "      exits 1 when any file has an error. --data adds the checks that need game data.\n"
              << "  " << program << " frm info <name-or-fid> --data <dir-or-.dat> [--data <...>]\n"
              << "      FRM metadata as JSON: resolvedArtPath, fid, directionCount, framesPerDirection, and a\n"
              << "      per-frame array of {direction,frame,width,height,offsetX,offsetY}. Accepts an art name\n"
              << "      (ext2grd1 or art/misc/ext2grd1.frm) or a FID (0x05000021 / decimal).\n"
              << "  " << program << " frm render <name-or-fid> --out <file.png> [--dir N] [--frame N] [--transparent]\n"
              << "      --data <dir-or-.dat> [--data <...>]\n"
              << "      Render the sprite to a PNG (needs an off-screen GL context). Default: a grid of all\n"
              << "      6 directions x all frames on a checkerboard. --dir N renders one direction; --frame N\n"
              << "      one frame index. Reports the output path, image size, and grid layout (DxF).\n"
              << "      --transparent drops the checkerboard and keeps real alpha, for extracting a sprite\n"
              << "      as an asset rather than looking at it.\n"
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
              << "  " << program << " proto export [--kind item|critter] [--item-type <type>]\n"
              << "      --data <dir-or-.dat> [--data <...>]\n"
              << "      Every item and critter proto listed in proto/items/items.lst and\n"
              << "      proto/critters/critters.lst, with the full stats its .pro stores, as JSON (the MCP\n"
              << "      export_protos tool). --kind limits it to items or critters; --item-type to one item\n"
              << "      type: armor, container, drug, weapon, ammo, misc or key.\n"
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

// Checked unsigned parse for `--flag <value>` (decimal or 0x-hex). Prints why and returns false on
// anything else, a sign or whitespace included, rather than wrapping a negative to a huge value.
bool parseUnsignedFlag(const std::string& flag, const std::string& value, std::uint64_t& out, const char* program) {
    const auto parsed = geck::text::parseInteger(value);
    if (!parsed || *parsed < 0) {
        std::cerr << "error: " << flag << " expects a non-negative integer, got: " << value << "\n";
        printUsage(program);
        return false;
    }
    out = static_cast<std::uint64_t>(*parsed);
    return true;
}

bool isKnownSubcommand(const std::vector<std::string>& args) {
    return args.size() >= 2 && args[0] == "map"
        && (args[1] == "analyze" || args[1] == "generate" || args[1] == "render" || args[1] == "extract-pattern"
            || args[1] == "describe-script" || args[1] == "reachability" || args[1] == "describe-map"
            || args[1] == "graph" || args[1] == "world" || args[1] == "encounters"
            || args[1] == "gvars" || args[1] == "quests" || args[1] == "endings" || args[1] == "party" || args[1] == "examples" || args[1] == "find-gvar"
            || args[1] == "dump-grid" || args[1] == "lint-script" || args[1] == "script-refs");
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
            std::uint64_t value = 0;
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
// lint-script: --index <n>, --locale <l>, --all, --notes, or the script's name.
int consumeLintArg(const std::vector<std::string>& args, std::size_t i, CliArgs& out, const char* program) {
    const std::string& arg = args[i];
    if (arg == "--index") {
        return parseIntFlag(arg, args[i + 1], out.lint.programIndex, program) ? 2 : 0;
    }
    if (arg == "--locale") {
        out.lint.locale = args[i + 1];
        return 2;
    }
    if (arg == "--map") {
        out.lint.map = args[i + 1];
        return 2;
    }
    if (arg == "--all") {
        out.lint.all = true;
        return 1;
    }
    if (arg == "--notes") {
        out.lintNotes = true;
        return 1;
    }
    if (arg.rfind("--", 0) == 0 || !out.lint.name.empty()) {
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    out.lint.name = arg;
    return 1;
}

// One `map examples` argument (its value already checked to exist): a filter flag, or a map to limit to.
int consumeExamplesArg(const std::vector<std::string>& args, std::size_t i, geck::cli::ExamplesOptions& out, const char* program) {
    const std::string& arg = args[i];
    const std::map<std::string, std::string*, std::less<>> texts{ { "--kind", &out.kind }, { "--name", &out.name },
        { "--script", &out.script }, { "--calls", &out.scriptCalls }, { "--carries", &out.carries }, { "--near", &out.nearName } };
    if (const auto text = texts.find(arg); text != texts.end()) {
        *text->second = args[i + 1];
        return 2;
    }
    const std::map<std::string, int*, std::less<>> numbers{ { "--near-radius", &out.nearRadius },
        { "--radius", &out.contextRadius }, { "--limit", &out.limit } };
    if (const auto number = numbers.find(arg); number != numbers.end()) {
        return parseIntFlag(arg, args[i + 1], *number->second, program) ? 2 : 0;
    }
    if (arg == "--pid") {
        int pid = 0;
        if (!parseIntFlag(arg, args[i + 1], pid, program)) {
            return 0;
        }
        out.pids.push_back(static_cast<uint32_t>(pid));
        return 2;
    }
    if (arg.starts_with("--")) {
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    out.maps.push_back(arg);
    return 1;
}

int consumeArg(const std::vector<std::string>& args, std::size_t i, CliArgs& out, const char* program) {
    const std::string& arg = args[i];
    const bool valueFlag = arg == "--data"
        || (out.generate && (arg == "--script" || arg == "--out" || arg == "--in" || arg == "--elevation" || arg == "--count" || arg == "--time-budget" || arg == "--arg" || arg == "--stamp"))
        || (out.render && (arg == "--map" || arg == "--out" || arg == "--elevation" || arg == "--max-dim" || arg == "--crop-hex" || arg == "--crop-extent"))
        || (out.extract && (arg == "--map" || arg == "--out" || arg == "--name" || arg == "--elevation" || arg == "--pids" || arg == "--anchor" || arg == "--radius"))
        || (out.describeScript && (arg == "--index" || arg == "--locale"))
        || (out.lintScript && (arg == "--index" || arg == "--locale" || arg == "--map"))
        || (out.world && arg == "--render")
        || (out.encounters && (arg == "--x" || arg == "--y"))
        || (out.examples
            && (arg == "--kind" || arg == "--pid" || arg == "--name" || arg == "--script" || arg == "--calls" || arg == "--carries"
                || arg == "--near" || arg == "--near-radius" || arg == "--radius" || arg == "--limit"))
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
    if (out.generate && arg == "--time-budget") {
        int seconds = 0;
        if (!parseIntFlag(arg, args[i + 1], seconds, program)) {
            return 0;
        }
        if (seconds < 0 || seconds > 24 * 60 * 60) {
            std::cerr << "error: --time-budget expects 0..86400 seconds, got: " << args[i + 1] << "\n";
            printUsage(program);
            return 0;
        }
        out.gen.timeBudgetMs = static_cast<unsigned>(seconds) * 1000U;
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
        return parseIntFlag(arg, args[i + 1], out.ren.elevation, program) ? 2 : 0;
    }
    if (out.render && arg == "--max-dim") {
        std::uint64_t maxDim = 0;
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

    if (out.render && arg == "--show-unreachable") {
        out.ren.showUnreachable = true;
        return 1;
    }
    if (out.render && arg == "--crop-hex") {
        return parseIntFlag(arg, args[i + 1], out.ren.cropCenterHex, program) ? 2 : 0;
    }
    if (out.render && arg == "--crop-extent") {
        return parseIntFlag(arg, args[i + 1], out.ren.cropExtentPx, program) ? 2 : 0;
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
    if (out.describeScript && arg == "--analyze") {
        out.desc.analyze = true;
        return 1;
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
    if (out.graph && arg == "--no-scripts") {
        out.graphOpts.scripts = false;
        return 1;
    }
    if (out.graph) { // trailing positional args are the maps to include (empty = all)
        out.graphOpts.maps.push_back(arg);
        return 1;
    }
    if (out.world) {
        if (arg == "--render") {
            out.worldOpts.renderPath = args[i + 1];
            return 2;
        }
        if (arg == "--no-markers") {
            out.worldOpts.markers = false;
            return 1;
        }
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (out.examples) {
        return consumeExamplesArg(args, i, out.examplesOpts, program);
    }
    if (out.encounters && arg == "--x") {
        return parseIntFlag(arg, args[i + 1], out.encountersX, program) ? 2 : 0;
    }
    if (out.encounters && arg == "--y") {
        return parseIntFlag(arg, args[i + 1], out.encountersY, program) ? 2 : 0;
    }
    if (out.encounters) {
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (out.gvars || out.quests || out.endings || out.party) { // these take no arguments beyond --data (handled above)
        std::cerr << "error: unexpected argument: " << arg << "\n";
        printUsage(program);
        return 0;
    }
    if (out.lintScript) {
        return consumeLintArg(args, i, out, program);
    }
    if (out.scriptRefs) { // two positionals: the kind, then the name
        if (arg.rfind("--", 0) == 0 || !out.refs.name.empty()) {
            std::cerr << "error: script-refs takes a kind and a name: " << arg << "\n";
            printUsage(program);
            return 0;
        }
        (out.refs.kind.empty() ? out.refs.kind : out.refs.name) = arg;
        return 1;
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
    out.party = args[1] == "party";
    out.examples = args[1] == "examples";
    out.findGvar = args[1] == "find-gvar";
    out.dumpGrid = args[1] == "dump-grid";
    out.lintScript = args[1] == "lint-script";
    out.scriptRefs = args[1] == "script-refs";

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
    if (out.lintScript && !out.lint.all && out.lint.map.empty() && out.lint.name.empty() && out.lint.programIndex < 0) {
        std::cerr << "error: lint-script requires a script name, --index <n>, --map <map> or --all\n";
        printUsage(program);
        return 2;
    }
    out.lint.includeNotes = (!out.lint.all && out.lint.map.empty()) || out.lintNotes;
    if (out.scriptRefs && out.refs.name.empty()) {
        std::cerr << "error: script-refs requires a kind (gvar, call, procedure, macro, pid, map) and a name\n";
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
    bool checkerboard = true;
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
        if (arg == "--transparent") {
            out.checkerboard = false;
            ++i;
            continue;
        }
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
    opts.checkerboard = fa.checkerboard;
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
    geck::cli::mountDataPaths(resources, fa.dataPaths, program);
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
    geck::cli::mountDataPaths(resources, ra.dataPaths, program);
    return dispatchResource(resources, ra);
}

// `script lint <files> [--data <path>]... [--notes] [--json] [--locale <l>]`
int runScriptCommand(const std::vector<std::string>& args, const char* program) {
    std::vector<std::filesystem::path> files;
    std::vector<std::string> dataPaths;
    bool notes = false;
    bool json = false;
    std::string locale = "english";
    for (std::size_t i = 2; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if ((arg == "--data" || arg == "--locale") && i + 1 < args.size()) {
            (arg == "--data" ? dataPaths.emplace_back() : locale) = args[++i];
        } else if (arg == "--notes") {
            notes = true;
        } else if (arg == "--json") {
            json = true;
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "error: unexpected argument: " << arg << "\n";
            printUsage(program);
            return 2;
        } else {
            files.emplace_back(arg);
        }
    }
    if (files.empty()) {
        std::cerr << "error: script lint needs at least one .ssl file\n";
        printUsage(program);
        return 2;
    }
    spdlog::set_level(spdlog::level::off);
    geck::resource::GameResources resources;
    geck::cli::mountDataPaths(resources, dataPaths, program);
    return geck::cli::lintFiles(resources, files, notes, locale, json, std::cout);
}

// --- proto subcommand -------------------------------------------------------------------------
// `proto export` dumps every proto in the item and critter .lst files. Self-contained like
// `resource`: two optional filters plus its own --data list.
struct ProtoArgs {
    std::string kind;
    std::string itemType;
    std::vector<std::string> dataPaths;
};

// Parse the tokens after `proto export` into `out`. Returns false (after printing why) on a bad flag.
bool parseProtoArgs(const std::vector<std::string>& args, const char* program, ProtoArgs& out) {
    for (std::size_t i = 2; i < args.size(); i += 2) {
        const std::string& arg = args[i];
        if (arg != "--data" && arg != "--kind" && arg != "--item-type") {
            std::cerr << "error: unexpected argument: " << arg << "\n";
            printUsage(program);
            return false;
        }
        if (i + 1 >= args.size()) {
            std::cerr << "error: " << arg << " needs a value\n";
            printUsage(program);
            return false;
        }
        if (arg == "--data") {
            out.dataPaths.push_back(args[i + 1]);
        } else if (arg == "--kind") {
            out.kind = args[i + 1];
        } else {
            out.itemType = args[i + 1];
        }
    }
    return true;
}

// Run a `proto export` command end to end (parse, validate, mount, export). Returns the exit code.
int runProtoCommand(const std::vector<std::string>& args, const char* program) {
    ProtoArgs pa;
    if (!parseProtoArgs(args, program, pa)) {
        return 2;
    }
    geck::cli::ProtoExportOptions opts;
    if (const auto error = geck::cli::parseProtoFilter(pa.kind, pa.itemType, opts)) {
        std::cerr << "error: " << *error << "\n";
        printUsage(program);
        return 2;
    }
    if (pa.dataPaths.empty()) {
        std::cerr << "error: at least one --data <path> is required\n";
        printUsage(program);
        return 2;
    }

    spdlog::set_level(spdlog::level::off);
    geck::resource::GameResources resources;
    geck::cli::mountDataPaths(resources, pa.dataPaths, program);
    return geck::cli::exportProtos(resources, opts, std::cout);
}

// The command families that parse their own arguments; nullopt when `args` names none of them.
std::optional<int> runCommandFamily(const std::vector<std::string>& args, const char* program) {
    if (args.size() < 2) {
        return std::nullopt;
    }
    // The `frm` family is parsed and run on its own path (its own positional + flags), separate from
    // the `map` subcommands below; so is the `resource` family (data inspection).
    if (args[0] == "frm" && isFrmAction(args[1])) {
        return runFrmCommand(args, program);
    }
    if (args[0] == "resource" && isResourceAction(args[1])) {
        return runResourceCommand(args, program);
    }
    if (args[0] == "proto" && args[1] == "export") {
        return runProtoCommand(args, program);
    }
    if (args[0] == "script" && args[1] == "lint") {
        return runScriptCommand(args, program);
    }
    return std::nullopt;
}

// The map subcommands that write files or read scripts; nullopt for the analysis ones.
std::optional<int> runProducingCommand(geck::resource::GameResources& resources, const CliArgs& cli) {
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
    if (cli.lintScript) {
        return geck::cli::lintScripts(resources, cli.lint, std::cout);
    }
    if (cli.scriptRefs) {
        return geck::cli::findScriptRefs(resources, cli.refs, std::cout);
    }
    return std::nullopt;
}

// The map and world analysis subcommands; analyze is the default.
int runAnalysisCommand(geck::resource::GameResources& resources, const CliArgs& cli) {
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
        if (!cli.worldOpts.renderPath.empty()) {
            return geck::cli::renderWorldMap(resources, cli.worldOpts, std::cout);
        }
        return geck::cli::buildWorldMap(resources, std::cout);
    }
    if (cli.encounters) {
        if ((cli.encountersX < 0) != (cli.encountersY < 0)) {
            std::cerr << "error: encounters needs both --x and --y\n";
            return 2;
        }
        if (cli.encountersX >= 0) {
            return geck::cli::describeWorldPosition(resources, cli.encountersX, cli.encountersY, std::cout);
        }
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
    if (cli.party) {
        return geck::cli::buildParty(resources, std::cout);
    }
    if (cli.examples) {
        return geck::cli::findExamples(resources, cli.examplesOpts, std::cout);
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

int runCli(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (const auto exitCode = runCommandFamily(args, argv[0]); exitCode.has_value()) {
        return *exitCode;
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
    // The bundled resources (blank.frm, scripts, …) let e.g. the natural map render find
    // art/tiles/blank.frm, which isn't in master.dat.
    geck::cli::mountDataPaths(resources, cli.dataPaths, argv[0]);
    if (const auto exitCode = runProducingCommand(resources, cli); exitCode.has_value()) {
        return *exitCode;
    }
    return runAnalysisCommand(resources, cli);
}

} // namespace

int main(int argc, char** argv) {
    // A bad --data path or an unreadable map raises from deep in the resource layer; report it
    // instead of letting it reach std::terminate.
    try {
        return runCli(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
