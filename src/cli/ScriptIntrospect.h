#pragma once

#include <iosfwd>
#include <string>

namespace geck {
namespace resource {
    class GameResources;
}

namespace cli {

    struct DescribeScriptOptions {
        int programIndex = -1;          ///< 1-based scripts.lst index (a map/object's script_id)
        std::string locale = "english"; ///< dialog .msg locale subdirectory
    };

    /// Describe a Fallout 2 script by its scripts.lst program index: resolve the filename + its
    /// scripts.lst description, read the `.ssl` source if a source tree is mounted (e.g. the FRP
    /// scripts_src), and load the dialog `.msg`. Resolution keys off the filename basename
    /// (case-insensitive) — the name the engine itself uses — not SCRIPT_REALNAME. Emits a JSON
    /// object to `out`; returns 0 on success, nonzero with a message on a hard error (no scripts.lst,
    /// index out of range).
    int describeScript(resource::GameResources& resources, const DescribeScriptOptions& options, std::ostream& out);

} // namespace cli
} // namespace geck
