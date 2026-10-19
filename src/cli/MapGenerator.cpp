#include "cli/MapGenerator.h"

#include "cli/PatternJson.h"

#include <ostream>

#include "resource/GameResources.h"

#ifdef GECK_SCRIPTING_ENABLED
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <ranges>
#include <vector>

#include <SFML/Graphics/Sprite.hpp>

#include <nlohmann/json.hpp>

#include "cli/MapLoad.h"
#include "cli/MapReachability.h"
#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "format/map/Tile.h"
#include "format/pro/Pro.h"
#include "scripting/LuaScriptRuntime.h"
#include "scripting/MapScriptApi.h"
#include "scripting/ScriptSeed.h"
#include "editing/commands/CommandHost.h"
#include "editing/commands/ObjectCommandController.h"
#include "rendering/MapSpriteLoader.h"
#include "util/UndoStack.h"
#include "resource/MapFileIO.h"
#include "util/FileIo.h"
#endif

namespace geck::cli {

#ifdef GECK_SCRIPTING_ENABLED
namespace {

    // "out/town.map", 3 -> "out/town_3.map" — the per-map output name of a batch run.
    std::string numberedOutPath(const std::string& outPath, int n) {
        const std::filesystem::path path(outPath);
        const auto numbered = std::format("{}_{}{}", path.stem().string(), n, path.extension().string());
        return (path.parent_path() / numbered).string();
    }

    // Sanity-check a written map the way the reachability tool does: an exit grid that can't be
    // walked to from the player start is almost always a generation bug (a sealed room, or a
    // start placed inside solid terrain). Best-effort — a check failure never fails the generate.
    void warnOnUnreachableExits(resource::GameResources& resources, const std::string& mapPath, std::ostream& out) {
        try {
            const auto report = reachabilityJson(resources, mapPath);
            if (!report) {
                return;
            }
            for (const auto& elevation : report->at("reachability")) {
                int unreachable = 0;
                for (const auto& exit : elevation.value("exits", nlohmann::ordered_json::array())) {
                    // null = the player starts on another elevation; only an explicit false is a finding.
                    const auto reachable = exit.find("reachableFromPlayerStart");
                    if (reachable != exit.end() && reachable->is_boolean() && !reachable->get<bool>()) {
                        ++unreachable;
                    }
                }
                if (unreachable > 0) {
                    out << "warning: " << unreachable << " exit-grid hex(es) on elevation "
                        << elevation.value("elevation", 0) << " are unreachable from the player start.\n";
                }
            }
        } catch (const std::exception&) {
            // The map was written; a failed sanity check must not turn success into failure.
        }
    }

    // One generation run: build the headless editing context over a fresh empty map (or a fresh
    // load of options.inPath), run the script with `args`, and write the result to `outPath`.
    // Same return contract as generateMap.
    int generateOne(resource::GameResources& resources, const GenerateOptions& options,
        const std::string& source, const std::string& outPath,
        const std::map<std::string, std::string>& args, std::ostream& out) {
        // A headless editing context: the same wiring the GUI builds, but with no-op rendering
        // callbacks and no GL. The script edits this map's `elevation` and we serialize the result.
        HexagonGrid hexgrid;
        MapSpriteLoader spriteLoader{ resources, hexgrid };
        std::vector<std::shared_ptr<Object>> objects;
        std::vector<sf::Sprite> overlays;
        UndoStack undoStack;
        std::unique_ptr<Map> map;
        if (options.inPath.empty()) {
            map = std::make_unique<Map>(outPath);
            map->setMapFile(std::make_unique<Map::MapFile>(Map::createEmptyMapFile()));
        } else {
            // Decorate an existing map: load a fresh copy per run (VFS path or a file on disk).
            map = loadMap(resources, options.inPath);
            if (map == nullptr) {
                out << "error: cannot read input map: " << options.inPath << "\n";
                return 2;
            }
            // A .map only stores the elevations its header enables; painting an absent one would
            // edit a tile block that doesn't exist. Surface that instead of writing a broken map.
            if (!map->getMapFile().tiles.contains(options.elevation)) {
                out << "error: " << options.inPath << " has no elevation " << options.elevation
                    << " (present:";
                for (const int level : map->getMapFile().tiles | std::views::keys) {
                    out << " " << level;
                }
                out << ").\n";
                return 2;
            }
        }

        // Headless host: no rendering/UI, just the trivial tile/elevation accessors. Outlives
        // `controller`, which holds a reference to it.
        CallbackCommandHost host(
            [] { /* refreshObjects: nothing to render */ },
            [] { /* undoStackChanged: no UI */ },
            [&map](int elevation) -> std::vector<Tile>& { return map->getMapFile().tiles[elevation]; },
            [&options] { return options.elevation; },
            [](int, bool, int) { /* updateTileSprite: no rendering */ },
            [] { /* reloadTiles: no rendering */ });

        ObjectCommandController controller(
            resources, map, hexgrid, spriteLoader, objects, overlays, undoStack, host);

        // Data-only mode: objects are recorded as map data without building sprites (no GL).
        MapScriptApi api(resources, hexgrid, controller, *map, options.elevation, /*buildSprites*/ false);

        // Pre-load the stamps the script will place (api:placeStamp(name, ...)). Fail early on a bad one.
        for (const auto& [name, path] : options.stamps) {
            std::string error;
            if (const auto stamp = loadPattern(path, &error)) {
                api.addStamp(name, *stamp);
            } else {
                out << "stamp error: " << error << "\n";
                return 1;
            }
        }

        LuaScriptRuntime runtime;
        const ScriptResult result = runtime.run(source, api, controller, "generate", args, options.timeBudgetMs);
        if (!result.output.empty()) {
            out << result.output;
            if (result.output.back() != '\n') {
                out << '\n';
            }
        }
        if (!result.ok) {
            out << "script error: " << result.error << "\n";
            return 1;
        }
        out << "painted " << api.paintedTiles() << " tiles, placed " << api.placedObjects() << " objects.\n";

        // Writes the .EDG sidecar too, so `--in` keeps the map edge of the map it decorated.
        if (!resource::writeMap(resources, *map, outPath).has_value()) {
            out << "error: failed to write map: " << outPath << "\n";
            return 1;
        }
        out << "wrote " << outPath << "\n";
        warnOnUnreachableExits(resources, outPath, out);
        return 0;
    }

} // namespace
#endif

int generateMap(resource::GameResources& resources, const GenerateOptions& options, std::ostream& out) {
#ifndef GECK_SCRIPTING_ENABLED
    (void)resources;
    (void)options;
    out << "generate requires a scripting-enabled build. Reconfigure with -DGECK_ENABLE_SCRIPTING=ON.\n";
    return 2;
#else
    if (options.elevation < 0 || options.elevation > 2) {
        out << "error: elevation must be 0, 1 or 2.\n";
        return 2;
    }
    if (options.count < 1) {
        out << "error: --count must be at least 1.\n";
        return 2;
    }
    const std::string source = io::readFile(options.scriptPath);
    if (source.empty()) {
        out << "error: cannot read script (or it is empty): " << options.scriptPath << "\n";
        return 2;
    }

    if (options.count == 1) {
        return generateOne(resources, options, source, options.outPath, options.args, out);
    }

    // Batch: run the script once per map, each against a fresh copy, with consecutive seeds from
    // a base — so the maps differ from each other, yet the whole batch reproduces from one
    // `--arg seed=N`, read the way a single run reads it.
    std::optional<uint32_t> argSeed;
    if (const auto it = options.args.find("seed"); it != options.args.end()) {
        argSeed = parseScriptSeed(it->second);
    }
    const uint32_t baseSeed = argSeed.value_or(randomScriptSeed());
    if (!argSeed.has_value()) {
        out << "batch seed base " << baseSeed << " (re-run with --arg seed=" << baseSeed << " to reproduce)\n";
    }

    for (int i = 1; i <= options.count; ++i) {
        auto runArgs = options.args;
        const uint32_t seed = (baseSeed + static_cast<uint32_t>(i) - 1) & SCRIPT_SEED_MASK;
        runArgs["seed"] = std::to_string(seed);
        out << "-- map " << i << "/" << options.count << " (seed " << seed << ")\n";
        if (const int rc = generateOne(resources, options, source, numberedOutPath(options.outPath, i), runArgs, out); rc != 0) {
            return rc;
        }
    }
    return 0;
#endif
}

} // namespace geck::cli
