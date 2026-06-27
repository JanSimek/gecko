#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "util/Constants.h"
#include "util/HexLine.h"

// Groups committed exit-grid markers into RUNS at RENDER time so the editor can draw the diagonal
// bands more legibly — WITHOUT changing the saved data (one marker/hex stays as placed). A run is a
// chain of hex-adjacent markers sharing one direction; ordering it along the screen band identifies
// the run's TOP (whose bar overshoots past the first hex and is clipped) and the DIAGONAL→HORIZONTAL
// junctions (bridged with one extra texture each). Qt/SFML-free, so it is unit-testable headlessly.
namespace geck::exitgrid_runs {

/// One placed exit-grid marker, reduced to what run-grouping needs: its hex, its direction (0..7) and
/// its trigger-hex screen centre (y DOWNWARD).
struct Marker {
    int hex = -1;
    int dir = -1;
    int screenX = 0;
    int screenY = 0;
};

/// dir 4..7 are the two diagonal pairs ("/" = 4/5, "\" = 6/7); 0..3 are the cardinals.
inline bool isDiagonalDir(int dir) {
    return dir >= ExitGrid::DIR_FWD_A && dir <= ExitGrid::DIR_BACK_B;
}
inline bool isHorizontalDir(int dir) {
    return dir == ExitGrid::DIR_BOTTOM || dir == ExitGrid::DIR_TOP;
}

/// Per-marker render directives derived from the run structure.
struct MarkerInfo {
    bool isRunTop = false; ///< the topmost (smallest screen-Y) diagonal marker in its run
};

/// A diagonal→horizontal junction: a diagonal run END whose hex is adjacent to a horizontal marker.
/// The renderer bridges the corner gap by drawing one extra diagonal bar (at `diagonalHex`'s art)
/// and one extra horizontal bar (at `horizontalHex`'s art) over the seam.
struct Junction {
    int diagonalHex = -1;   ///< the diagonal run's end marker hex
    int horizontalHex = -1; ///< the adjacent horizontal marker hex
};

struct RunAnalysis {
    std::unordered_map<int, MarkerInfo> byHex; ///< per-marker directives, keyed by hex
    std::vector<Junction> junctions;
};

namespace detail {

    /// Group same-direction DIAGONAL markers into hex-adjacent runs (connected components over the
    /// 6-neighbour adjacency, restricted to markers sharing the run's direction).
    inline std::vector<std::vector<int>> groupDiagonalRuns(
        const std::vector<Marker>& markers,
        const std::unordered_map<int, std::size_t>& indexByHex) {
        std::vector<std::vector<int>> runs;
        std::vector<bool> visited(markers.size(), false);
        for (std::size_t i = 0; i < markers.size(); ++i) {
            if (visited[i] || !isDiagonalDir(markers[i].dir)) {
                continue;
            }
            // Flood-fill this marker's connected, same-direction run.
            std::vector<int> run;
            std::vector<std::size_t> stack{ i };
            visited[i] = true;
            while (!stack.empty()) {
                const std::size_t cur = stack.back();
                stack.pop_back();
                run.push_back(static_cast<int>(cur));
                for (const int nb : hexline::hexNeighbors(markers[cur].hex)) {
                    const auto it = indexByHex.find(nb);
                    if (it == indexByHex.end()) {
                        continue;
                    }
                    const std::size_t j = it->second;
                    if (!visited[j] && markers[j].dir == markers[cur].dir) {
                        visited[j] = true;
                        stack.push_back(j);
                    }
                }
            }
            runs.push_back(std::move(run));
        }
        return runs;
    }

} // namespace detail

/// Analyse the placed markers: mark each diagonal run's TOP marker and collect diagonal→horizontal
/// junctions. Pure: depends only on hex adjacency and the markers' screen positions.
inline RunAnalysis analyse(const std::vector<Marker>& markers) {
    RunAnalysis out;
    std::unordered_map<int, std::size_t> indexByHex;
    indexByHex.reserve(markers.size());
    for (std::size_t i = 0; i < markers.size(); ++i) {
        indexByHex[markers[i].hex] = i;
    }

    for (const std::vector<int>& run : detail::groupDiagonalRuns(markers, indexByHex)) {
        if (run.empty()) {
            continue;
        }
        // The TOP of a band is its smallest-screen-Y member (highest on screen); its bar is the only
        // one with no predecessor above to overlap, so it is the one whose overshoot we clip.
        int topIdx = run.front();
        for (const int idx : run) {
            if (markers[idx].screenY < markers[topIdx].screenY) {
                topIdx = idx;
            }
        }
        for (const int idx : run) {
            out.byHex[markers[idx].hex].isRunTop = (idx == topIdx);
        }

        // A junction: a diagonal run member hex-adjacent to a horizontal marker. We bridge from the
        // diagonal END nearest the horizontal run, so prefer the member with the most horizontal
        // neighbours; record one bridge per (diagonal, horizontal) adjacency.
        for (const int idx : run) {
            for (const int nb : hexline::hexNeighbors(markers[idx].hex)) {
                const auto it = indexByHex.find(nb);
                if (it == indexByHex.end()) {
                    continue;
                }
                if (isHorizontalDir(markers[it->second].dir)) {
                    out.junctions.push_back({ markers[idx].hex, nb });
                }
            }
        }
    }
    return out;
}

} // namespace geck::exitgrid_runs
