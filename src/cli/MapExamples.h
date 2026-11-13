#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// What to look for across the shipped maps. Every set filter must hold; at least one of kind, pids,
/// name, script, scriptCalls or carries must be set.
struct ExamplesOptions {
    std::string kind;           ///< item, critter, scenery, wall, misc or exitgrid
    std::vector<uint32_t> pids; ///< any of these protos
    std::string name;           ///< substring of the proto name, any case
    std::string script;         ///< substring of the attached script's scripts.lst name or description, any case
    std::string scriptCalls;    ///< the attached script's live code calls this opcode or procedure
    std::string carries;        ///< its inventory holds an item whose name contains this, any case
    /// An object whose name contains this stands within nearRadius. (Not `near`: windef.h defines it.)
    std::string nearName;
    int nearRadius = 5;
    std::vector<std::string> maps; ///< limit to these maps (file names, ".map" optional); empty = all
    int contextRadius = 3;         ///< neighbours reported within this many hexes
    int limit = 20;                ///< matches returned (the totals count them all)
};

/// Find shipped examples of a placement: every object across the mounted maps (inventories included)
/// that matches `options`, each with where it stands, its script, what it carries, and what stands
/// around it within `contextRadius` hexes grouped by proto. Answers "how do shipped maps do X" in one
/// call — e.g. critters whose script calls gdialog_mod_barter, and the counters and containers they
/// stand behind. Emits a JSON object to `out`; returns 0 on success (including no matches), non-zero
/// for an empty query, no maps, or a scriptCalls filter with no .ssl source mounted.
int findExamples(resource::GameResources& resources, const ExamplesOptions& options, std::ostream& out);

} // namespace geck::cli
