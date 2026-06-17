#pragma once

#include <string>
#include <vector>

namespace geck {

/// One entry of the generation-script `api:` surface — the Lua name, its signature and a one-line
/// description. The single in-code source of truth for the scripting reference (no hand-maintained
/// doc to drift); a test asserts these names match what LuaScriptRuntime actually binds.
struct ScriptApiEntry {
    const char* name;
    const char* signature;
    const char* doc;
};

/// Every function bound on the `api` global, in the order a script author would meet them.
const std::vector<ScriptApiEntry>& scriptApiEntries();

/// The reference as Markdown: the functions plus the two non-obvious runtime behaviours (runs are
/// auto-seeded and auto-batched) and the error model. What the MCP `script_api` tool returns so an
/// agent can write a generation script without reading the C++.
std::string scriptApiReference();

} // namespace geck
