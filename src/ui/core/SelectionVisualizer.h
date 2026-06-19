#pragma once

#include <vector>

#include "selection/SelectionState.h"

namespace geck {

class EditorSession;

/**
 * @brief Tracks the visual selection state that the renderer outlines.
 *
 * Extracted from EditorWidget. Objects mark themselves selected; floor/roof tiles
 * and hexes are tracked by index here so RenderingEngine can outline them from
 * geometry. EditorWidget feeds selection changes in via apply()/clear()/refresh()
 * and hands the index lists to the renderer via the accessors.
 */
class SelectionVisualizer {
public:
    explicit SelectionVisualizer(EditorSession& session)
        : _session(session) {
    }

    /// Record the visual selection for @p selection (replaces, does not accumulate —
    /// callers clear() first, matching the prior EditorWidget behaviour).
    void apply(const selection::SelectionState& selection);
    /// Drop all tracked tile/hex visuals.
    void clear();
    /// Drop only the tracked hex positions (used when only the hex selection is reset).
    void clearHexPositions();
    /// Re-derive the visuals from the session's current selection.
    void refresh();

    const std::vector<int>& floorVisuals() const { return _floorVisuals; }
    const std::vector<int>& roofVisuals() const { return _roofVisuals; }
    const std::vector<int>& hexPositions() const { return _hexPositions; }

private:
    void applyRoofTile(int tileIndex);

    EditorSession& _session;
    std::vector<int> _floorVisuals;
    std::vector<int> _roofVisuals;
    std::vector<int> _hexPositions;
};

} // namespace geck
