#pragma once

#include <iosfwd>
#include <map>
#include <string>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

struct GenerateOptions {
    std::string scriptPath;                    // Luau generation script to run
    std::string outPath;                       // .map file to write
    std::string inPath;                        // optional: existing .map to load and decorate (empty = fresh empty map)
    int elevation = 0;                         // elevation (0-2) the script edits
    int count = 1;                             // batch size: > 1 writes out_1.map .. out_N.map with consecutive seeds
    std::map<std::string, std::string> args;   // parameters exposed to the script as `args`
    std::map<std::string, std::string> stamps; // name -> stamp .json path, for api:placeStamp(name, ...)
};

// Run a Luau generation script headlessly and write the result to `outPath`. The script edits a
// fresh empty map, or — with `inPath` set — a loaded copy of an existing map, so a generator can
// decorate real content. It drives the same MapScriptApi the editor's console uses, in data-only
// mode (no GL), so it can both paint tiles and place objects. With `count` > 1 the script runs
// once per map against a fresh copy, writing `<out>_1.map` .. `<out>_N.map` seeded consecutively
// from the base seed (`--arg seed=N` or a random one, reported per map) so the batch both varies
// and reproduces. Progress/errors go to `out`.
//
// Requires a scripting-enabled build (GECK_ENABLE_SCRIPTING); otherwise reports that and
// returns 2. Returns 0 on success, non-zero on a read/script/write failure.
int generateMap(resource::GameResources& resources, const GenerateOptions& options, std::ostream& out);

} // namespace geck::cli
