#include "cli/MapGenerator.h"

#include <fstream>
#include <ostream>
#include <sstream>

#include "resource/GameResources.h"

#ifdef GECK_SCRIPTING_ENABLED
#include <functional>
#include <memory>
#include <vector>

#include <SFML/Graphics/Sprite.hpp>

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "format/map/Tile.h"
#include "format/pro/Pro.h"
#include "scripting/LuaScriptRuntime.h"
#include "scripting/MapScriptApi.h"
#include "ui/editing/ObjectCommandController.h"
#include "ui/rendering/MapSpriteLoader.h"
#include "util/ProHelper.h"
#include "util/UndoStack.h"
#include "writer/map/MapWriter.h"
#endif

namespace geck::cli {

#ifdef GECK_SCRIPTING_ENABLED
namespace {

    std::string readFile(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
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
    const std::string source = readFile(options.scriptPath);
    if (source.empty()) {
        out << "error: cannot read script (or it is empty): " << options.scriptPath << "\n";
        return 2;
    }

    // A headless editing context: the same wiring the GUI builds, but with no-op rendering
    // callbacks and no GL. The script edits this map's `elevation` and we serialize the result.
    HexagonGrid hexgrid;
    MapSpriteLoader spriteLoader{ resources, hexgrid };
    std::vector<std::shared_ptr<Object>> objects;
    std::vector<sf::Sprite> overlays;
    UndoStack undoStack;
    auto map = std::make_unique<Map>(options.outPath);
    map->setMapFile(std::make_unique<Map::MapFile>(Map::createEmptyMapFile()));

    ObjectCommandController controller(
        resources, map, hexgrid, spriteLoader, objects, overlays, undoStack,
        [] { /* refreshObjects: nothing to render */ },
        [] { /* onStackChanged: no UI */ },
        [&map](int elevation) -> std::vector<Tile>& { return map->getMapFile().tiles[elevation]; },
        [&options] { return options.elevation; },
        [](int, bool, int) { /* updateTileSprite: no rendering */ },
        [] { /* reloadTiles: no rendering */ });

    // Data-only mode: objects are recorded as map data without building sprites (no GL).
    MapScriptApi api(resources, hexgrid, controller, *map, options.elevation, /*buildSprites*/ false);
    LuaScriptRuntime runtime;
    const ScriptResult result = runtime.run(source, api, controller, "generate");
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

    // Serialize. Scenery/item objects read their subtype from the proto during writing, so the
    // writer needs the same proto provider the reader uses.
    const std::function<Pro*(int32_t)> proLoad = [&resources](int32_t pid) -> Pro* {
        try {
            return resources.repository().load<Pro>(ProHelper::basePath(resources, static_cast<uint32_t>(pid)));
        } catch (...) {
            return nullptr;
        }
    };
    MapWriter writer{ proLoad };
    writer.openFile(options.outPath);
    if (!writer.write(map->getMapFile())) {
        out << "error: failed to write map: " << options.outPath << "\n";
        return 1;
    }
    out << "wrote " << options.outPath << "\n";
    return 0;
#endif
}

} // namespace geck::cli
