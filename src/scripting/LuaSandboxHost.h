#pragma once

#include "scripting/ScriptTypes.h"

#include <chrono>
#include <cstddef>
#include <string>

struct lua_State;

namespace geck {

class MapScriptApi;

/// Owns one sandboxed Luau state and the common host bindings used by generation scripts.
///
/// Today LuaScriptRuntime creates one host per run. The class is intentionally stateful so the same
/// setup can later back resident plugin VMs: print output can be repointed between dispatches, while
/// the sandbox/API binding stays with the Lua state.
///
/// The host stores the *address* of the print-output string, so that string must outlive the
/// runLoaded() calls it collects, or be replaced first via setPrintOutput().
class LuaSandboxHost {
public:
    /// The Luau heap a script may hold. A larger request fails with Luau's "not enough memory" error
    /// instead of taking the host (gecko-mcp, the editor) down with it.
    static constexpr std::size_t HEAP_LIMIT_BYTES = std::size_t{ 1024 } * 1024 * 1024;
    /// print() output kept per run; the rest is dropped behind a one-line note.
    static constexpr std::size_t PRINT_LIMIT_BYTES = std::size_t{ 4 } * 1024 * 1024;

    LuaSandboxHost() = default;
    ~LuaSandboxHost();

    // Non-copyable and non-movable: `this` is baked into the print closure's upvalue and into the
    // state's interrupt userdata, so a relocated host would leave both dangling.
    LuaSandboxHost(const LuaSandboxHost&) = delete;
    LuaSandboxHost& operator=(const LuaSandboxHost&) = delete;
    LuaSandboxHost(LuaSandboxHost&&) = delete;
    LuaSandboxHost& operator=(LuaSandboxHost&&) = delete;

    /// Create the Luau state, bind `api`, expose `args`, sandbox globals, and seed math.random.
    /// Returns false only when the state cannot be created. Re-initializing closes the old state.
    bool initialize(MapScriptApi& api, const ScriptArgs& args, std::string& output, std::string& error);

    /// Redirect subsequent print() calls to `output`, which must outlive the runLoaded() calls that
    /// collect into it. initialize() sets the initial target, so call this only after it.
    void setPrintOutput(std::string& output);

    /// Compile source and leave the loaded chunk on the stack for runLoaded(). A chunk left by an
    /// earlier loadSource() that was never run is discarded.
    bool loadSource(const std::string& source, std::string& error);

    /// Run the previously loaded chunk. `timeBudgetMs == 0` means untimed. Once the budget expires
    /// the run is over: a script cannot pcall its way past the deadline and keep going.
    bool runLoaded(std::string& error, unsigned timeBudgetMs = 0);

private:
    static void* allocate(void* ud, void* ptr, std::size_t oldSize, std::size_t newSize);
    static int capturePrint(lua_State* L);
    static void timeBudgetInterrupt(lua_State* L, int gc);

    void disarmInterrupt() noexcept;
    void close() noexcept;
    void appendPrintValue(lua_State* L, int index);

    lua_State* _state = nullptr;
    std::string* _output = nullptr;
    bool _printTruncated = false;
    std::size_t _heapBytes = 0;
    bool _hasLoadedChunk = false;

    // Watchdog state for the in-flight runLoaded(). `_timedOut` is sticky for the whole run: see
    // timeBudgetInterrupt() for why the deadline must keep firing rather than disarming once.
    std::chrono::steady_clock::time_point _deadline;
    bool _timedOut = false;
};

} // namespace geck
