#include "scripting/ScriptApiReference.h"

#include <sstream>

namespace geck {

const std::vector<ScriptApiEntry>& scriptApiEntries() {
    // Built from the single GECK_SCRIPT_API list that LuaScriptRuntime also binds from.
#define GECK_SCRIPT_API_ENTRY(name, sig, doc) { #name, sig, doc },
    static const std::vector<ScriptApiEntry> entries = { GECK_SCRIPT_API(GECK_SCRIPT_API_ENTRY) };
#undef GECK_SCRIPT_API_ENTRY
    return entries;
}

std::string scriptApiReference() {
    std::ostringstream out;
    out << "# Generation-script `api` reference\n\n"
        << "Functions on the global `api` inside a `generate` Luau script. Two grids, both numbered\n"
        << "`position = row * width + col`: hexes are 200x200 (objects), tiles 100x100 (floor/roof).\n\n"
        << "| Function | Signature | Description |\n|---|---|---|\n";
    for (const ScriptApiEntry& entry : scriptApiEntries()) {
        out << "| `api:" << entry.name << "` | `" << entry.signature << "` | " << entry.doc << " |\n";
    }
    out << "\n## Runtime behaviour (not obvious from the functions)\n\n"
        << "- **Auto-seeded.** Each run seeds `math.random` from `--arg seed=N`, or a fresh random seed\n"
        << "  when none is given (so a run is random unless you pin the seed). The resolved seed is in\n"
        << "  `args.seed` — print it to let a good layout be reproduced.\n"
        << "- **Auto-batched.** The whole run is collapsed into one undo entry; do **not** call\n"
        << "  begin/endBatch yourself.\n"
        << "- **Errors raise.** A genuine failure (no data mounted, a bad map path, an unknown proto\n"
        << "  type) raises a Lua error that stops the run; \"not applicable\" stays a value (e.g.\n"
        << "  `placeProto` -> false, `tileId` -> -1).\n"
        << "- **`args`** holds the `--arg key=value` parameters as strings (use `tonumber` as needed).\n";
    return out.str();
}

} // namespace geck
