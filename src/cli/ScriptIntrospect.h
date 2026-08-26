#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace geck {
namespace resource {
    class GameResources;
}

namespace cli {

    /// # Script index bases
    ///
    /// Three different numbers name the same Fallout 2 script, and only one of them is what this
    /// module takes. Getting this wrong does not fail loudly — it returns the *neighbouring*
    /// script — so it is spelled out here once:
    ///
    /// | Number                                 | Base    | Where it lives                                                     |
    /// |----------------------------------------|---------|--------------------------------------------------------------------|
    /// | `programIndex` — what gecko speaks     | 0-based | the `scripts.lst` array index, and an object's `MapScript::script_id` |
    /// | map header `script_id`                 | 1-based | `MapFile::MapHeader::script_id`                                    |
    /// | SSL `SCRIPT_*` (FRP `headers/scripts.h`) | 1-based | the `scripts.lst` *line* number; also sfall `set_script()` ids      |
    ///
    /// gecko speaks the engine's internal 0-based index everywhere, because that is the number
    /// actually stored in map files and the one the engine indexes `scripts.lst` with
    /// (fallout2-ce `scripts.cc`, `scriptsGetFileName`). This is deliberate, and required by the
    /// engine-fidelity rule: stored ids are preserved exactly as the engine stores them rather than
    /// re-based for convenience. The 1-based forms are the engine's own inputs, and the engine
    /// itself decrements them — so gecko normalises on the way in, in the same two places:
    ///
    ///  - map header: `MapAnalyzer` resolves `script_id - 1` (fallout2-ce `map.cc`,
    ///    `script->index = gMapHeader.scriptIndex - 1`);
    ///  - sfall `set_script()`: the opcode decrements before validating (fallout2-ce
    ///    `sfall_opcodes.cc`).
    ///
    /// The trap in practice: `SCRIPT_EPAC17 (1413)` in `headers/scripts.h` is `programIndex`
    /// **1412**. Passing the constant through unadjusted names `epac18` — a plausible wrong answer,
    /// never an error. So every entry point here also accepts a `name`, which is unambiguous, and
    /// every result echoes `sslConstant` (== `programIndex + 1`) so the two can be cross-checked at
    /// a glance.

    struct DescribeScriptOptions {
        /// 0-based scripts.lst index (== a critter/object's `MapScript::script_id`); -1 = unset.
        /// Ignored when `name` is set.
        int programIndex = -1;
        /// scripts.lst basename, case-insensitive and with any extension ignored ("epac17",
        /// "EPAC17.int"). Preferred over `programIndex`: no index base to get wrong.
        std::string name;
        std::string locale = "english"; ///< dialog .msg locale subdirectory
    };

    /// Describe a Fallout 2 script, selected by `name` or by 0-based `programIndex` (see the index-base
    /// note above): resolve the filename, read the `.ssl` source if a source tree is mounted (e.g. the
    /// FRP scripts_src), and load the dialog `.msg`. Resolution keys off the filename basename
    /// (case-insensitive) — the name the engine itself uses — not SCRIPT_REALNAME. Emits a JSON object
    /// to `out`; returns 0 on success, nonzero with a message on a hard error (no scripts.lst, unknown
    /// name, index out of range).
    int describeScript(resource::GameResources& resources, const DescribeScriptOptions& options, std::ostream& out);

    struct FindScriptOptions {
        /// 0-based scripts.lst index; -1 = unset. Ignored when `name` is set.
        int programIndex = -1;
        /// scripts.lst basename, or a fragment of one. An exact (case-insensitive) basename match
        /// selects that script; otherwise every entry containing the fragment is returned as a
        /// candidate list and no map scan runs.
        std::string name;
        /// Scope the placement scan to these map VFS paths (e.g. "maps/epa1.map"); empty = every
        /// mounted map.
        std::vector<std::string> maps;
        /// Skip the map scan and only resolve the script's identity. Cheap; use when all you need is
        /// the index<->name mapping.
        bool resolveOnly = false;
    };

    /// Find a script by name (or index) and report where the shipped maps actually place it: the map's
    /// own header script, plus every `map_scripts` entry, per section. The inverse of describe_script —
    /// "which map is this NPC on?" rather than "what does this script say?". Emits a JSON object to
    /// `out`; returns 0 on success (including a no-placements or candidate-list result), nonzero on a
    /// hard error (no scripts.lst, unknown name, index out of range).
    int findScript(resource::GameResources& resources, const FindScriptOptions& options, std::ostream& out);

    struct FindTextOptions {
        std::string pattern; ///< required; substring by default, ECMAScript regex when `regex`
        bool regex = false;
        bool caseSensitive = false;
        /// "dialog" (per-script dialog .msg), "game" (game/*.msg — item, perk, quest text), "source"
        /// (.ssl, needs a source tree mounted), or "all".
        std::string scope = "all";
        std::string locale = "english";
        int limit = 200; ///< max matches emitted; the result flags `truncated` when it bites
    };

    /// Search the mounted game text for `pattern` and report every hit with the script it belongs to,
    /// so "which script mentions X?" is one call instead of a grep over a checkout. Emits a JSON object
    /// to `out`; returns 0 on success (including no matches), nonzero on a hard error (bad regex).
    int findText(resource::GameResources& resources, const FindTextOptions& options, std::ostream& out);

} // namespace cli
} // namespace geck
