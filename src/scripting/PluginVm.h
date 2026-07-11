#pragma once

#include "scripting/LuaSandboxHost.h"
#include "scripting/ScriptTypes.h"

#include <cstddef>
#include <string>

namespace geck {

class MapScriptApi;

/// One resident plugin's Luau VM: a LuaSandboxHost with a persistent writable environment
/// (globals survive across dispatches), a Lua-heap cap, a per-dispatch time budget, a bounded
/// console that collects the plugin's print() output across its lifetime, and fault-based
/// auto-disable — a plugin that keeps erroring (timeout, OOM, raised errors, a detached api)
/// is switched off instead of degrading the editor.
///
/// The VM borrows its MapScriptApi: the owner (the future PluginManager) constructs one per
/// plugin against the current editor and retarget()s it on map switches / detach()es it on
/// teardown; a dispatch against a detached api fails as an ordinary fault.
class PluginVm {
public:
    struct Config {
        std::string name;                               ///< For console prefixes and disable messages.
        std::size_t memoryLimitBytes = 8 * 1024 * 1024; ///< Lua-heap cap (0 = unlimited).
        unsigned dispatchBudgetMs = 1000;               ///< Per-dispatch watchdog (0 = untimed).
        int maxConsecutiveFaults = 3;                   ///< Auto-disable threshold.
        std::size_t consoleCapBytes = 64 * 1024;        ///< Bounded print console (keeps the tail).
    };

    PluginVm(Config config, MapScriptApi& api);

    PluginVm(const PluginVm&) = delete;
    PluginVm& operator=(const PluginVm&) = delete;

    /// Bring the VM up (or back up after disable()+enable()): fresh sandboxed state with the
    /// persistent env. False (with a console line) when the state cannot be created.
    bool start(const ScriptArgs& args = {});

    /// Compile + run one chunk in the plugin's persistent environment under the dispatch
    /// budget. Failures append to the console, count one fault (consecutive successes reset
    /// the count), and auto-disable the VM at the threshold. Returns false on failure or
    /// while disabled.
    bool dispatch(const std::string& source);

    /// True once faults reached the threshold; dispatches are refused until enable().
    [[nodiscard]] bool disabled() const { return _disabled; }
    /// Re-arm a disabled VM with a FRESH state (the faulting state may hold corrupt plugin
    /// globals, so persistence deliberately does not survive re-enabling).
    bool enable();

    [[nodiscard]] int consecutiveFaults() const { return _consecutiveFaults; }
    [[nodiscard]] const std::string& lastError() const { return _lastError; }

    /// The plugin's bounded print()/fault console (tail-trimmed at Config::consoleCapBytes).
    [[nodiscard]] const std::string& console() const { return _console; }

private:
    void recordFault(const std::string& error);
    void trimConsole();

    Config _config;
    MapScriptApi& _api;
    LuaSandboxHost _host;
    ScriptArgs _startArgs;
    std::string _console;
    std::string _lastError;
    int _consecutiveFaults = 0;
    bool _started = false;
    bool _disabled = false;
};

} // namespace geck
