#pragma once

#include <map>
#include <string>

namespace geck {

/// Caller-supplied script parameters, exposed to Luau as the global table `args` (string keys ->
/// string values; a script does `tonumber(args.seed)` as needed).
using ScriptArgs = std::map<std::string, std::string>;

struct ScriptResult {
    bool ok = false;
    std::string error;
    /// Captured text from the script's print() calls (newline-separated), shown in the console.
    std::string output;
};

} // namespace geck
