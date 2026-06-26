#pragma once

#include "format/gam/Gam.h"

#include <string>
#include <vector>

namespace geck::writer {

/// A problem found in a @ref Gam. An `Error` means an edit would corrupt the `.gam` (the engine re-reads
/// a BASE map's globals from `MAP_GLOBAL_VARS:`), so the editor must hard-block the save while any
/// `Error` is present — mirroring @ref MapsTxtIssue / @ref validateMapsTxt.
struct GamIssue {
    enum class Severity {
        Error,
        Warning,
        Info,
    };

    Severity severity = Severity::Error;
    std::string message;
};

/// Validate that the `.gam` can be written and re-read without corrupting its map-global variables.
///
/// The key check (Error) is **round-trip integrity**: serialise the document, re-parse it, and confirm
/// the re-parsed `mapGlobalVars()` matches the document's (same count, names, and values). Any divergence
/// means an edit broke the file structure, so the save must be blocked. Also (Error) every map-global
/// value's `std::to_string` round-trips back to the same int, and each variable line still matches the
/// engine's `^\s*(\w+)\s*:=\s*(-?\d+)\s*;` shape. Structure findings are non-blocking: a missing
/// `MAP_GLOBAL_VARS:` header when the doc carries map-global vars is a Warning; duplicate variable names
/// are an Info (the engine indexes positionally and ignores the names).
std::vector<GamIssue> validateGam(const Gam& doc);

/// True if any issue is an `Error` (i.e. the save must be blocked).
bool hasErrors(const std::vector<GamIssue>& issues);

} // namespace geck::writer
