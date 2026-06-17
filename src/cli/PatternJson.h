#pragma once

#include <string>

#include "pattern/Pattern.h"

namespace geck::cli {

/// Serialize a Pattern to the prefab JSON the editor's PatternSerializer reads (same keys: name,
/// version, variants[].{label,anchorHex,objects[],floor[],roof[]}). Qt-free and hand-rolled — the
/// editor's serializer uses Qt's JSON, which the headless tools can't link. Values are verbatim
/// (engine PIDs/FIDs/direction/flags/tile-ids), matching the engine-data-fidelity rule.
std::string serializePattern(const pattern::Pattern& pattern);

} // namespace geck::cli
