#pragma once

#include "format/maps/MapsTxt.h"

#include <string>
#include <vector>

namespace geck::writer {

/// A problem found in a @ref MapsTxt. `Error`s would make fallout2-ce silently truncate or
/// fail to load the registry, so the editor must hard-block a save while any `Error` is present.
struct MapsTxtIssue {
    enum class Severity {
        Error,
        Warning,
        Info,
    };

    Severity severity = Severity::Error;
    int section = -1; ///< the `[Map N]` index, or -1 for a document-level issue
    std::string message;
};

/// Validate against the engine's expectations (fallout2-ce worldmap.cc): the `[Map N]` indices form a
/// gapless run 0..N (a gap silently truncates the registry); every section has `lookup_name` and
/// `map_name`; `ambient_sfx` has at most 7 entries; `can_rest_here`, if present, lists exactly 3
/// values; no duplicate section index. Unknown keys are reported as `Info` (never blocking).
std::vector<MapsTxtIssue> validateMapsTxt(const MapsTxt& doc);

/// True if any issue is an `Error` (i.e. the save must be blocked).
bool hasErrors(const std::vector<MapsTxtIssue>& issues);

} // namespace geck::writer
