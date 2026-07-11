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
    LuaSandboxHost() = default;
    ~LuaSandboxHost();

    // Non-copyable and non-movable: `this` is baked into the print closure's upvalue and into the
    // state's interrupt userdata, so a relocated host would leave both dangling.
    LuaSandboxHost(const LuaSandboxHost&) = delete;
    LuaSandboxHost& operator=(const LuaSandboxHost&) = delete;
    LuaSandboxHost(LuaSandboxHost&&) = delete;
    LuaSandboxHost& operator=(LuaSandboxHost&&) = delete;

    struct Options {
        /// Hard ceiling on the Luau heap (tracking allocator); 0 = unlimited. Growth past the
        /// limit fails the allocation, which Luau raises as a "not enough memory" script error
        /// — the state itself survives. Bounds only the LUA heap: C++-side results created by
        /// api calls are bounded separately (the plan-sink cap).
        std::size_t memoryLimitBytes = 0;
        /// Run chunks inside one sandboxed THREAD environment (luaL_sandboxthread) instead of
        /// on the readonly-global main state: the chunk's global writes go to a private,
        /// writable env that PERSISTS across loadSource/runLoaded cycles — the resident plugin
        /// model. Per-run generation scripts leave this off and keep readonly globals.
        bool persistentEnv = false;
    };

    /// Create the Luau state, bind `api`, expose `args`, sandbox globals, and seed math.random.
    /// Returns false only when the state cannot be created. Re-initializing closes the old state.
    /// (Two overloads rather than a defaulted Options argument: a `= {}` default for a nested
    /// class with member initializers is ill-formed inside the enclosing class definition.)
    bool initialize(MapScriptApi& api, const ScriptArgs& args, std::string& output, std::string& error);
    bool initialize(MapScriptApi& api, const ScriptArgs& args, std::string& output, std::string& error,
        const Options& options);

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
    static int capturePrint(lua_State* L);
    static void timeBudgetInterrupt(lua_State* L, int gc);

    void disarmInterrupt() noexcept;
    void close() noexcept;
    void appendPrintValue(lua_State* L, int index);

    /// The state chunks load/run on: the persistent-env thread when enabled, else the main state.
    lua_State* execState() const { return _thread != nullptr ? _thread : _state; }
    static void* trackingAlloc(void* ud, void* ptr, size_t osize, size_t nsize);

    lua_State* _state = nullptr;
    lua_State* _thread = nullptr; // anchored on _state's stack; freed with the state
    std::string* _output = nullptr;
    bool _hasLoadedChunk = false;

    // Tracking-allocator accounting for Options::memoryLimitBytes (0 = unlimited).
    std::size_t _memoryLimit = 0;
    std::size_t _memoryUsed = 0;

    // Watchdog state for the in-flight runLoaded(). `_timedOut` is sticky for the whole run: see
    // timeBudgetInterrupt() for why the deadline must keep firing rather than disarming once.
    std::chrono::steady_clock::time_point _deadline;
    bool _timedOut = false;
};

} // namespace geck
