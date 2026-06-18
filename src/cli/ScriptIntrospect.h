#pragma once

#include <iosfwd>
#include <string>

namespace geck {
namespace resource {
    class GameResources;
}

namespace cli {

    struct DescribeScriptOptions {
        int programIndex = -1;          ///< 0-based scripts.lst index (== a critter/object's MapScript.script_id); -1 = unset
        std::string locale = "english"; ///< dialog .msg locale subdirectory
    };

    /// Describe a Fallout 2 script by its scripts.lst program index (0-based, as analyze reports it
    /// for a critter/object): resolve the filename, read the `.ssl` source if a source tree is mounted
    /// (e.g. the FRP scripts_src), and load the dialog `.msg`. Resolution keys off the filename
    /// basename (case-insensitive) — the name the engine itself uses — not SCRIPT_REALNAME. Emits a
    /// JSON object to `out`; returns 0 on success, nonzero with a message on a hard error (no
    /// scripts.lst, index out of range).
    int describeScript(resource::GameResources& resources, const DescribeScriptOptions& options, std::ostream& out);

} // namespace cli
} // namespace geck
