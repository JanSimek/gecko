#include "scripting/LuaScriptRuntime.h"

#include "editing/commands/ObjectCommandController.h"
#include "scripting/LuaSandboxHost.h"

namespace geck {

ScriptResult LuaScriptRuntime::run(const std::string& source, MapScriptApi& api,
    ObjectCommandController& controller, const std::string& description, const ScriptArgs& args,
    unsigned timeBudgetMs) {
    ScriptResult result;

    LuaSandboxHost host;
    if (!host.initialize(api, args, result.output, result.error)) {
        return result;
    }
    if (!host.loadSource(source, result.error)) {
        return result;
    }

    {
        // The whole run is one undo entry: a committing run's api mutators buffer into this batch and
        // endBatch() (on scope exit) collapses them — even if the script errors part-way. When the
        // caller has installed a plan sink (a fill preview), the mutators record into the sink and
        // commit nothing, so this batch stays empty and endBatch() pushes nothing.
        ScopedUndoBatch batch(controller, description);
        result.ok = host.runLoaded(result.error, timeBudgetMs);
    }

    return result;
}

} // namespace geck
