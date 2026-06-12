#pragma once

#include <string>

namespace geck {

class MapScriptApi;
class ObjectCommandController;

struct ScriptResult {
    bool ok = false;
    std::string error;
};

/// Runs a sandboxed Luau script with a MapScriptApi bound as the global `api`. The whole
/// run is recorded as a SINGLE undo entry (batched on the controller), so a generation
/// script collapses to one Ctrl-Z regardless of how many hexes it touches.
///
/// Built only when GECK_ENABLE_SCRIPTING is on; nothing else in the editor sees Luau, and
/// this header stays free of any Lua/Luau type so callers and tests don't either.
class LuaScriptRuntime {
public:
    /// Bind `api`, sandbox, compile + run `source`. Returns ok=false with a message on a
    /// compile or runtime error; partial edits made before the error are still flushed as
    /// the one undo entry, so the user can Ctrl-Z a failed run.
    ScriptResult run(const std::string& source,
        MapScriptApi& api,
        ObjectCommandController& controller,
        const std::string& description = "Run script");
};

} // namespace geck
