#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace geck {

namespace resource {
    class GameResources;
}
struct MapObject;

/**
 * Optimistic movement reachability over the 200x200 hex grid, shared by the headless
 * `reachability` analysis (gecko-cli / MCP) and the editor's "unreachable areas" overlay so the
 * two can never disagree.
 *
 * A hex is blocked per the engine's per-instance flag rule (`blocksMovementByInstance`, mirroring
 * fallout2-ce `_obj_blocking_at`); openable doors are treated as passable, so the result
 * over-estimates reachability rather than crying wolf. Entry points are the player start (when they
 * spawn on this elevation) plus every exit grid — you arrive at an exit's position when entering
 * from the adjacent map.
 */
namespace reachability {

    /// The engine's instance-flag movement-blocking rule: a CRITTER/SCENERY/WALL that is neither
    /// `OBJECT_HIDDEN` nor `OBJECT_NO_BLOCK`. Items, misc and tiles never block. (Doors are scenery
    /// but `blockedMask` treats them as passable.)
    bool blocksMovementByInstance(std::uint32_t objectType, std::uint32_t flags);

    /// The up-to-6 parity-correct neighbour positions of a hex (empty if `hex` is off-grid).
    std::vector<int> hexNeighbors(int hex);

    /// Connected components of the WIDTH*HEIGHT hex grid given a `blocked` mask (1 = impassable).
    /// Returns a component id per hex (-1 for blocked hexes); `sizes[id]` is each component's hex
    /// count and `samples[id]` one representative hex in it.
    std::vector<int> hexComponents(const std::vector<char>& blocked, std::vector<int>& sizes, std::vector<int>& samples);

    /// Impassable-hex mask for one elevation's objects, using the instance-flag rule, then treating
    /// (openable) doors as passable. Multihex blockers also seal their neighbours.
    std::vector<char> blockedMask(resource::GameResources& resources,
        const std::vector<std::shared_ptr<MapObject>>& objects);

    /// Entry hexes for one elevation: the player start (when `playerDefaultElevation == elevation`)
    /// plus every exit-grid marker's position.
    std::vector<int> entryHexes(int playerDefaultElevation, int playerDefaultPosition, int elevation,
        const std::vector<std::shared_ptr<MapObject>>& objects);

    /// Which walkable components the given seeds land in (a seed on a blocked hex still enters via a
    /// walkable neighbour). Indexed by component id.
    std::vector<char> seedComponentFlags(const std::vector<int>& seeds, const std::vector<char>& blocked,
        const std::vector<int>& component, std::size_t componentCount);

    /// The walkable component a marker hex sits in (or, if on a blocked hex, its first walkable
    /// neighbour's). -1 if it touches no walkable hex.
    int componentOf(int hex, const std::vector<char>& blocked, const std::vector<int>& component);

    /// Whether a hex is in (or borders) a seed-reachable component.
    bool reaches(int hex, const std::vector<char>& blocked, const std::vector<int>& component,
        const std::vector<char>& seedFlags);

    /// Everything a consumer needs for one elevation, computed once so the CLI serializer and the
    /// editor overlay share a single flood-fill.
    struct ElevationResult {
        std::vector<char> blocked;              ///< per hex, 1 = impassable
        std::vector<int> component;             ///< per hex, component id (-1 if blocked)
        std::vector<int> componentSizes;        ///< hex count per component id
        std::vector<int> entrySeeds;            ///< player start (when here) + exit-grid hexes
        std::vector<char> entryReachable;       ///< by component id: reachable from ANY entry point
        bool playerHere = false;                ///< does the player spawn on this elevation?
        std::vector<char> playerStartReachable; ///< by component id: reachable from the player start
                                                ///< only (empty when !playerHere)

        /// No player start and no exit grid here (reached via stairs/elevators) — reachability can't
        /// be determined, so consumers should shade/report nothing rather than everything.
        bool hasEntryPoints() const { return !entrySeeds.empty(); }
    };

    /// Compute reachability for one elevation from its objects and the map's player-start header.
    ElevationResult analyzeElevation(resource::GameResources& resources, int playerDefaultElevation,
        int playerDefaultPosition, int elevation, const std::vector<std::shared_ptr<MapObject>>& objects);

    /// Walkable hexes not connected to any entry point — what the editor overlay shades. Empty when
    /// there are no entry points (reachability undetermined on this elevation).
    std::vector<int> unreachableWalkableHexes(const ElevationResult& result);

} // namespace reachability
} // namespace geck
