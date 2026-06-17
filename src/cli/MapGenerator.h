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
    int elevation = 0;                         // elevation (0-2) the script edits
    std::map<std::string, std::string> args;   // parameters exposed to the script as `args`
    std::map<std::string, std::string> stamps; // name -> stamp .json path, for api:placeStamp(name, ...)
};

// Run a Luau generation script headlessly against a fresh empty map and write the result to
// `outPath`. The script drives the same MapScriptApi the editor's console uses, in data-only
// mode (no GL), so it can both paint tiles and place objects. Progress/errors go to `out`.
//
// Requires a scripting-enabled build (GECK_ENABLE_SCRIPTING); otherwise reports that and
// returns 2. Returns 0 on success, non-zero on a read/script/write failure.
int generateMap(resource::GameResources& resources, const GenerateOptions& options, std::ostream& out);

} // namespace geck::cli
