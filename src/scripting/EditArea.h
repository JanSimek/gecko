#pragma once

#include <vector>

namespace geck {

/// The target of an area fill: the hexes and floor/roof tiles a user selection covers, projected to
/// plain index lists the host builds from SelectionManager / SelectionState. A fill reads these via
/// MapScriptApi's area-queries and writes only inside them.
///
/// Each list MUST be sorted ascending: areaContains*() binary-searches them, and a seeded fill
/// iterates them in this canonical order, so the same selection + seed reproduces the same result.
/// Hex indices are 0..39999 (200x200); tile indices are 0..9999 (100x100) — the two grids are
/// distinct (see CLAUDE.md), so never cross-validate one against the other's range.
struct EditArea {
    std::vector<int> hexes;
    std::vector<int> floorTiles;
    std::vector<int> roofTiles;
};

} // namespace geck
