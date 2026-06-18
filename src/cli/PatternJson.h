#pragma once

#include <optional>
#include <string>

#include "pattern/Pattern.h"

namespace geck::cli {

/// Serialize a Pattern to the prefab JSON the editor's PatternSerializer reads (same keys: name,
/// version, variants[].{label,anchorHex,objects[],floor[],roof[]}). Qt-free and hand-rolled — the
/// editor's serializer uses Qt's JSON, which the headless tools can't link. Values are verbatim
/// (engine PIDs/FIDs/direction/flags/tile-ids), matching the engine-data-fidelity rule.
std::string serializePattern(const pattern::Pattern& pattern);

/// Load a stamp JSON file (as written by serializePattern or the editor) back into a Pattern, so
/// generate's placeStamp can place it. Returns std::nullopt and (if `error` is non-null) a reason on
/// an unreadable file, malformed JSON, or a missing `variants` array.
std::optional<pattern::Pattern> loadPattern(const std::string& path, std::string* error = nullptr);

} // namespace geck::cli
