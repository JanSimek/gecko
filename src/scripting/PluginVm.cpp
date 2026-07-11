#include "scripting/PluginVm.h"

#include "scripting/MapScriptApi.h"

namespace geck {

PluginVm::PluginVm(Config config, MapScriptApi& api)
    : _config(std::move(config))
    , _api(api) {
}

bool PluginVm::start(const ScriptArgs& args) {
    _startArgs = args;
    _consecutiveFaults = 0;
    _disabled = false;
    _lastError.clear();

    LuaSandboxHost::Options options;
    options.memoryLimitBytes = _config.memoryLimitBytes;
    options.persistentEnv = true;
    options.readOnlyApi = !_config.allowMapWrite;

    std::string error;
    // The console string is a member, satisfying the host's print-output lifetime contract:
    // it outlives every runLoaded() and persists across dispatches (bounded by trimConsole).
    _started = _host.initialize(_api, _startArgs, _console, error, options);
    if (!_started) {
        _console += "[" + _config.name + "] failed to start: " + error + "\n";
        trimConsole();
    }
    return _started;
}

bool PluginVm::dispatch(const std::string& source) {
    if (_disabled) {
        return false;
    }
    if (!_started) {
        recordFault("dispatch before start()");
        return false;
    }

    std::string error;
    if (!_host.loadSource(source, error)) {
        recordFault(error);
        return false;
    }
    if (!_host.runLoaded(error, _config.dispatchBudgetMs)) {
        recordFault(error);
        return false;
    }

    _consecutiveFaults = 0;
    trimConsole();
    return true;
}

bool PluginVm::enable() {
    if (!_disabled) {
        return true;
    }
    // A fresh state, deliberately: the faulting one may hold half-mutated plugin globals, and
    // "re-enable" must mean "known-good", not "resume the wreckage".
    if (!start(_startArgs)) {
        // start() cleared the flag optimistically; a VM whose state could not even be created
        // must stay disabled, or dispatches would burn the fault budget on "not started".
        _disabled = true;
        return false;
    }
    _console += "[" + _config.name + "] re-enabled with a fresh state\n";
    trimConsole();
    return true;
}

void PluginVm::recordFault(const std::string& error) {
    // A failed run's local wreckage is unreachable after the unwind but still counts against
    // the heap cap until collected — collect now so recovery from an OOM fault is
    // deterministic instead of depending on when Luau's incremental GC would get there.
    _host.collectGarbage();
    _lastError = error;
    ++_consecutiveFaults;
    _console += "[" + _config.name + "] fault " + std::to_string(_consecutiveFaults) + "/"
        + std::to_string(_config.maxConsecutiveFaults) + ": " + error + "\n";
    if (_consecutiveFaults >= _config.maxConsecutiveFaults) {
        _disabled = true;
        _console += "[" + _config.name + "] disabled after repeated faults\n";
    }
    trimConsole();
}

void PluginVm::trimConsole() {
    if (_console.size() <= _config.consoleCapBytes) {
        return;
    }
    // Keep the tail (the newest output), snapped to the next line start so the console never
    // begins mid-line.
    std::size_t cut = _console.size() - _config.consoleCapBytes;
    const std::size_t lineStart = _console.find('\n', cut);
    if (lineStart != std::string::npos) {
        cut = lineStart + 1;
    }
    _console.erase(0, cut);
}

} // namespace geck
