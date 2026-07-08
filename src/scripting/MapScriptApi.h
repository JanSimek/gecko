#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "pattern/Pattern.h"

namespace geck {

class HexagonGrid;
class Map;
struct MapObject;
struct EditArea;
class ObjectCommandController;
namespace pattern {
    struct FillPlan;
}
namespace resource {
    class GameResources;
}

/// Raised by the query/builder methods on a genuine failure (no data mounted, an unreadable map
/// path, an unknown proto type/id). A dedicated type so callers can catch it specifically; it still
/// derives from std::exception, so LuaBridge converts it to a Lua error the runtime reports.
class ScriptError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Host API for editing the map: queries plus undoable mutators that route through
/// ObjectCommandController. Pure C++ with no scripting-runtime dependency, so it is usable
/// (and unit-testable) without Lua.
///
/// A run that touches many hexes must be wrapped in beginBatch()/endBatch() (or a
/// ScopedUndoBatch) so it collapses into ONE undo entry instead of one-per-hex — the
/// UndoStack has a hard command cap.
class MapScriptApi {
public:
    /// Binds to a live editing session at `elevation`. References are borrowed and must
    /// outlive the api.
    ///
    /// `buildSprites` selects how placed objects are recorded. The GUI leaves it true so each
    /// object also gets an SFML sprite for rendering (needs a GL context). Headless callers
    /// (gecko-cli, CI) pass false: objects are recorded as map data only — no sprite, no GL —
    /// which is all the .map format stores anyway.
    MapScriptApi(resource::GameResources& resources,
        const HexagonGrid& hexgrid,
        ObjectCommandController& controller,
        Map& map,
        int elevation,
        bool buildSprites = true);

    // --- Queries (no mutation) ---------------------------------------------------
    bool isValidHex(int hex) const;
    /// The up-to-6 on-grid hex neighbours (cube-coordinate, parity-correct).
    std::vector<int> hexNeighbors(int hex) const;
    /// The 0..5 direction of the step from `fromHex` to an adjacent `toHex`; -1 if not neighbours.
    int hexDir(int fromHex, int toHex) const;
    /// Floor/roof tile id at `tileIndex` on this elevation, or EMPTY_TILE if out of range.
    uint16_t getFloor(int tileIndex) const;
    uint16_t getRoof(int tileIndex) const;
    /// Resolve a ground-tile FRM name (e.g. "edg5000" or "edg5000.frm", case-insensitive) to
    /// its index in art/tiles/tiles.lst — the value paintFloor()/paintRoof() expect. Returns -1
    /// if the tile list is unavailable or the name is unknown, so scripts address tiles by name
    /// instead of magic numbers.
    int tileId(const std::string& name) const;
    /// Every tiles.lst entry whose name starts with `prefix` (case-insensitive, e.g. "cav" for
    /// the cave floor family), as name (without ".frm") -> paintable id — so a script can pick
    /// from a tile family without hardcoding ids. Raises like tileId() when tiles.lst is
    /// unavailable; an unmatched prefix is a legitimate empty result.
    std::map<std::string, int> tilesByPrefix(const std::string& prefix) const;
    /// The distinct scenery proto PIDs used by a reference map (e.g. "maps/desert1.map") — the
    /// curated palette that map is actually built from, ready to hand to placeProto(). A PID is the
    /// engine's unique proto identifier ((type << 24) | index), so this is exact, unlike resolving
    /// by the non-unique display name. Use it to scatter the same scenery a shipped map does.
    /// Empty if the map can't be read (unknown path or no data mounted). Scenery only — walls and
    /// the MISC markers (scroll blockers / exit grids) are excluded by type, and flat scenery
    /// (invisible movement-blockers / floor markers, which carry OBJECT_FLAT) is excluded too, so
    /// only upright decorations remain and a generator never scatters a blocker.
    std::vector<int> mapScenery(const std::string& mapPath) const;
    /// Like mapScenery(), but keyed by PID with the *number of times* the reference map places each
    /// proto — its real frequency. A generator scatters proportional to these counts (so common
    /// vegetation dominates and a rare car stays rare) and can match the reference's object density,
    /// instead of picking uniformly among distinct PIDs. Same scenery / non-flat filtering as
    /// mapScenery(); empty if the map can't be read.
    std::map<int, int> mapSceneryHistogram(const std::string& mapPath) const;
    /// The distinct floor-tile ids a reference map uses (the values paintFloor() expects), most-used
    /// first — so a generator can fill with that map's dominant ground (e.g. cave rock for a cave,
    /// sand for a desert). Empty floors are skipped; empty if the map can't be read.
    std::vector<int> mapFloorTiles(const std::string& mapPath) const;
    /// A reference map's FULL floor grid at `elevation`: 10,000 tile ids in tile-index order
    /// (row-major, 100 wide), EMPTY_TILE included — the per-cell data a generator learns exact
    /// layout from (rock patterns, walkable regions). Raises if the map can't be read or has no
    /// such elevation.
    std::vector<int> mapFloorAt(const std::string& mapPath, int elevation) const;
    /// A reference map's objects of one proto type ("wall"/"scenery"/"misc"/...) at `elevation`,
    /// flattened as (pid, hex, direction) triples — so a generator can learn engine-authored
    /// placement (e.g. which cave-wall variant lines which boundary shape). Raises on an unknown
    /// type, an unreadable map, or a missing elevation; an elevation with no such objects is a
    /// legitimate empty result.
    std::vector<int> mapObjectsAt(const std::string& mapPath, int elevation, const std::string& typeName) const;
    /// Does the proto block movement (its NO_BLOCK flag is clear)? What a generator checks before
    /// treating scatter as walkable-safe, and how it tells blockers from decoration. Raises when
    /// the proto can't be loaded (no data mounted / unknown pid) — a wrong answer here would
    /// silently break map walkability.
    bool protoBlocks(int pid) const;
    /// True if the proto is OBJECT_FLAT — ground-hugging fill/rubble art drawn below standing
    /// objects, as opposed to a wall face. Lets a generator tell boundary walls from the flat rock
    /// texture that carpets solid ground. Raises when the proto can't be loaded (like protoBlocks).
    bool protoFlat(int pid) const;
    /// The proto's art FID (what placeProto resolves and stores) — lets a generator identify a
    /// proto's art via resolve_fid. Raises when the proto can't be loaded (like protoBlocks).
    int protoFid(int pid) const;
    /// Every map file in the mounted data (VFS paths, e.g. "maps/desert1.map"), sorted. Lets a
    /// generator pick a reference map at random when none was given.
    std::vector<std::string> listMaps() const;
    /// Coherent 2D value noise in [0,1] at (x, y): smooth and deterministic (same input -> same
    /// output). Sample it as a density field to scatter objects in natural clumps and clearings
    /// instead of an even sprinkle — scale the coordinates to set the clump size and offset them
    /// by a per-run amount for variation. A general primitive the MCP can drive too.
    double noise2d(double x, double y) const;
    /// The proto's engine display name (from the type's .msg, keyed by the proto's message_id),
    /// e.g. "Scrub". Empty if the proto or its message can't be resolved (no data / unknown pid).
    /// Lets a caller identify what a PID *is* — e.g. tell a decoration from a structural feature —
    /// which is what the MCP needs to curate a palette rather than scatter blindly.
    std::string protoName(int pid) const;
    /// Build a proto PID from a readable type name ("item"/"critter"/"scenery"/"wall"/"tile"/"misc",
    /// singular or plural) and the proto's id `number` — the `number` field `map analyze --json`
    /// reports, i.e. the PID's low 24 bits (e.g. proto("scenery", 102) == 0x02000066; note it is one
    /// less than the NNN in the 00000NNN.pro filename). A pure constructor (no data needed), so a
    /// script can name its protos — `SCRUB = 102 ... api:proto("scenery", SCRUB)` — instead of
    /// writing opaque hex. Raises on an unknown type or an out-of-range number.
    uint32_t proto(const std::string& typeName, int number) const;

    // --- Coordinates -------------------------------------------------------------
    // Two grids, both numbered row-major as `position = row * width + col` (the engine's storage
    // layout): hexes are 200x200 (objects/placement), floor & roof tiles are 100x100. These
    // convert (col, row) <-> the linear index the other calls take, so scripts read in 2D. An
    // off-grid (col, row) yields -1 (and the *XY ops below then no-op), so bounds are easy to skip.
    /// (col, row) -> hex position [0, 40000); -1 if off the 200x200 hex grid.
    int hexIndex(int col, int row) const;
    /// (col, row) -> tile index [0, 10000); -1 if off the 100x100 tile grid.
    int tileIndex(int col, int row) const;
    int hexCol(int hex) const;   ///< column of a hex position, or -1 if off-grid
    int hexRow(int hex) const;   ///< row of a hex position, or -1 if off-grid
    int tileCol(int tile) const; ///< column of a tile index, or -1 if off-grid
    int tileRow(int tile) const; ///< row of a tile index, or -1 if off-grid
    /// Floor/roof tile id at (col, row), or EMPTY_TILE if off-grid — the (col, row) form of getFloor.
    uint16_t getFloorXY(int col, int row) const;
    uint16_t getRoofXY(int col, int row) const;
    /// Tile indices inside the inclusive rectangle spanned by (col0, row0)-(col1, row1),
    /// ascending. Corners may come in any order and the rectangle is clamped to the 100x100
    /// grid, so a fully off-grid rectangle is a legitimate empty result.
    std::vector<int> tilesInRect(int col0, int row0, int col1, int row1) const;
    /// The floor tile visually under a hex (-1 off-grid) — the exact bridge between the 200x200
    /// hex grid (objects, movement) and the 100x100 tile grid (floor art), computed through the
    /// same screen geometry the renderer and the eyedropper use, NOT the naive col/2 halving.
    /// This is what lets wall/blocker placement follow a floor boundary precisely.
    int hexTile(int hex) const;
    /// The hexes standing on a floor tile — hexTile's inverse (empty if `tileIndex` is off-grid).
    std::vector<int> tileHexes(int tileIndex) const;
    /// The hex border of a screen-space rectangle centred on `centerHex` (half-extents in pixels)
    /// — the same gap-free iso staircase walk placeExitGridRect places its markers on, exposed as
    /// a query so scripts can ring an area with anything (scroll blockers, walls). Ascending,
    /// deduplicated. Raises like placeExitGridRect on a bad centre or non-positive extent.
    std::vector<int> hexesOnScreenRect(int centerHex, int screenHalfWidth, int screenHalfHeight) const;

    // --- Selection area (host-set per run; the queries below read it) -------------
    /// True if a selection area is bound to this run (see setArea).
    bool hasArea() const;
    /// The selection's hex / floor-tile / roof-tile indices, ascending (empty if no area is bound).
    std::vector<int> areaHexes() const;
    std::vector<int> areaFloorTiles() const;
    std::vector<int> areaRoofTiles() const;
    /// Is `hex` inside the selection? (binary search — the area lists are sorted.)
    bool areaContainsHex(int hex) const;
    /// Is `tileIndex` inside the selection's floor tiles?
    bool areaContainsTile(int tileIndex) const;

    // --- Deterministic helpers (seeded; for reproducible scatter) -----------------
    /// PID of an object occupying `hex` in the COMMITTED map (0 if none). It does NOT see placements
    /// already recorded in the current plan sink, so intra-fill occupancy is the caller's to track.
    uint32_t objectAt(int hex) const;
    /// Coherent 3D value noise in [0,1]; the z axis varies the field per seed/octave (cf. noise2d).
    double noise3d(double x, double y, double z) const;
    /// Next draw from this api's seeded stream (see setSeed): rng() in [0,1), rngInt(lo,hi) an int in
    /// [lo,hi]. Deterministic and cross-platform (mt19937 + integer reduction), so a seed reproduces.
    double rng();
    int rngInt(int lo, int hi);

    // --- Undo batching -----------------------------------------------------------
    void beginBatch(const std::string& description);
    void endBatch();

    // --- Plan sink (host-only; not script-bound) ---------------------------------
    /// Redirect the mutators: while a sink is installed, placeObject/placeProto/paint*/placeStamp/
    /// placeExitGrid* RECORD their fully-built objects and tiles into `sink` and commit NOTHING to
    /// the map (no undo entry, no live mutation). The host then applies the captured plan as one
    /// undo entry via pattern::PlacementBatch::replay — so a preview is byte-identical to the apply
    /// even for seeded/noise runs, and a whole fill collapses to one Ctrl-Z. Pass nullptr to restore
    /// direct (committing) behaviour. Queries (getFloor, objectAt, …) always read the COMMITTED map,
    /// so they do not see edits already recorded in the current plan.
    void setPlanSink(pattern::FillPlan* sink) { _planSink = sink; }

    /// Bind the selection area the area-queries report (borrowed, must outlive the run; nullptr
    /// clears it). Host-only — a script can't fabricate its own area. See EditArea.
    void setArea(const EditArea* area) { _area = area; }
    /// Seed this api's deterministic stream (rng/rngInt) so a run reproduces. LuaScriptRuntime::run
    /// re-seeds it from the run's resolved seed (exactly as it seeds Lua's math.random); a host may
    /// also pre-seed for api use outside a run, as the fill preview does.
    void setSeed(uint32_t seed) { _rng.seed(seed); }

    // --- Mutators (undoable; batch to collapse into one history entry) -----------
    /// Build and place an object at `hex`. Returns false if `hex` is off-grid or the
    /// object's art (`frmPid`) cannot be resolved/loaded (no visual to place).
    bool placeObject(uint32_t proPid, uint32_t frmPid, int hex, uint32_t direction);
    /// Place a proto by PID alone, resolving its art FID from the proto header — the common
    /// case, so scripts need not also know the FRM id. Same return contract as placeObject();
    /// also returns false if the proto cannot be loaded.
    bool placeProto(uint32_t proPid, int hex, uint32_t direction);
    bool paintFloor(int tileIndex, uint16_t tileId);
    bool paintRoof(int tileIndex, uint16_t tileId);

    // (col, row) forms of the placers/painters: place on the hex grid, paint on the tile grid.
    // An off-grid (col, row) is a no-op returning false (same contract as an out-of-range index).
    bool placeObjectXY(uint32_t proPid, uint32_t frmPid, int col, int row, uint32_t direction);
    bool placeProtoXY(uint32_t proPid, int col, int row, uint32_t direction);
    bool paintFloorXY(int col, int row, uint16_t tileId);
    bool paintRoofXY(int col, int row, uint16_t tileId);

    // Rectangle / region fills: composites over paintFloor/paintRoof, so they respect the plan
    // sink and undo batching like every other mutator. Each returns the number of tiles painted.
    /// Paint every floor/roof tile in the inclusive (col, row) rectangle (clamped to the grid).
    int fillFloorRect(int col0, int row0, int col1, int row1, uint16_t tileId);
    int fillRoofRect(int col0, int row0, int col1, int row1, uint16_t tileId);
    /// Flood-fill (paint-bucket) the 4-connected floor region of uniform tile id containing
    /// (col, row) with `tileId`. A no-op returning 0 when (col, row) is off-grid or the region
    /// already has that id.
    int fillRegion(int col, int row, uint16_t tileId);

    // --- Stamps (prefabs captured by extract_pattern) ----------------------------
    /// Register a pre-loaded stamp under `name` so the script can place it. The host (gecko-cli /
    /// the MCP) loads the stamp JSON and calls this before running the script.
    void addStamp(const std::string& name, pattern::Pattern pattern);
    /// Place a registered stamp's variant so its anchor lands near `anchorHex` (tile-granular, like
    /// the editor). Returns the number of objects placed. Raises if `name` is unregistered or
    /// `variant` is out of range — placement of an off-grid entry is simply dropped, not an error.
    int placeStamp(const std::string& name, int anchorHex, int variant = 0);

    // --- Map setup (spawn / exits) -----------------------------------------------
    /// Replace the bound map with a fresh, empty Fallout 2 map — no objects, empty floor/roof on all
    /// three elevations, default header. Lets a generation script start from a blank slate (in the
    /// editor's Script Console this clears whatever map is open). It is a destructive reset, NOT part
    /// of the undo batch, so call it first, before any placement.
    void newMap();
    /// Set the player's spawn in the map header — where they appear when the map loads: hex 0..39999,
    /// orientation 0..5 (engine hex facings), elevation 0..2. Raises on an out-of-range value. This is
    /// header state (like the editor's Map Info panel), so it is not part of the undo batch.
    void setPlayerStart(int hex, int orientation, int elevation);
    /// Switch which elevation (0..2) subsequent queries and edits target, so one run can author a
    /// multi-level map (e.g. a cave entrance plus its interior). Raises if the value is out of
    /// range or the bound map does not carry that elevation. Tile and object data are routed by
    /// the recorded elevation, so this is safe mid-run; in the editor's console the view keeps
    /// showing the elevation you are looking at.
    void setElevation(int elevation);
    /// Place a map-exit grid at `hex`: stepping onto it sends the player to `destMapId` at `destHex`
    /// (`destElevation`, facing `orientation`). `destMapId` -2 = the worldmap, -1 = the town map,
    /// otherwise a map id; a worldmap/townmap exit ignores destHex. Returns false only when the
    /// exit-grid art can't be loaded (GUI); raises on an off-grid `hex` or an out-of-range destination
    /// field. Counts toward placedObjects().
    bool placeExitGrid(int hex, int destMapId, int destHex, int destElevation, int orientation);
    /// Place a border of exit grids forming a rectangle on screen, centred on `centerHex` and
    /// `screenHalfWidth`/`screenHalfHeight` pixels to each side (the engine's iso projection, so the
    /// hex border staircases). Each of the four edges uses its matching directional exit-grid art, as
    /// shipped maps like bhrnddst.map do; every marker shares the destination (`destMapId`/`destHex`/
    /// `destElevation`/`orientation`, same meaning as placeExitGrid). Returns the number of markers
    /// placed. Raises on an off-grid `centerHex`, a non-positive half-extent, or a bad destination.
    int placeExitGridRect(int centerHex, int screenHalfWidth, int screenHalfHeight,
        int destMapId, int destHex, int destElevation, int orientation);

    int placedObjects() const { return _placedObjects; }
    int paintedTiles() const { return _paintedTiles; }
    /// Whether this api changed the map at all (placed/painted anything, or made a non-undoable
    /// header/map mutation like setPlayerStart/newMap). The host uses it to mark the map modified and
    /// resync the editor after a run, since those non-undoable mutations push no undo command.
    bool mutated() const { return _placedObjects > 0 || _paintedTiles > 0 || _mutatedDirectly; }

private:
    // Where an exit grid sends the player (engine fields exit_map/exit_position/exit_elevation/
    // exit_orientation). Bundled so the exit-grid builders don't take a long parameter list.
    struct ExitDest {
        uint32_t map;
        int hex;
        int elevation;
        int orientation;
    };
    // Record a freshly-built `mapObject`: data-only when headless (registerObjectData), else build its
    // sprite from `frmPid` and register the placement so it draws. Returns false when the GUI can't
    // resolve the art; bumps the placed-objects count on success. Shared by placeObject/placeExitGrid.
    bool registerObject(const std::shared_ptr<MapObject>& mapObject, int hex, uint32_t frmPid, uint32_t direction);
    // Build + register one exit-grid MISC marker at `hex` with the given art and destination. Assumes
    // `hex` is on-grid (callers validate). Returns registerObject's result.
    bool placeExitGridMarker(int hex, uint32_t proPid, uint32_t frmPid, const ExitDest& dest);
    // The four hex-line edges (top, bottom, left, right) of a screen-space rectangle centred on
    // `centerHex` — shared by placeExitGridRect (per-edge directional art) and hexesOnScreenRect
    // (flat query). Callers validate the centre and extents.
    std::array<std::vector<int>, 4> screenRectEdges(int centerHex, int screenHalfWidth, int screenHalfHeight) const;
    bool paintTile(int tileIndex, uint16_t tileId, bool isRoof);
    // Parse a reference map headlessly (GL-free) for the palette queries; nullptr if unreadable.
    std::unique_ptr<Map> loadReferenceMap(const std::string& mapPath) const;
    // pid -> placement count for the scatter-eligible scenery in `map` (scenery type, non-flat).
    // Shared by mapScenery (keys) and mapSceneryHistogram (the counts).
    std::map<int, int> sceneryCounts(Map& map) const;
    // Whether a scenery proto belongs in a scatter palette (upright decoration, not a flat blocker).
    bool isScatterableScenery(uint32_t pid) const;

    resource::GameResources& _resources;
    const HexagonGrid& _hexgrid;
    ObjectCommandController& _controller;
    Map& _map;
    int _elevation;
    bool _buildSprites;
    int _placedObjects = 0;
    int _paintedTiles = 0;
    // Set by mutators that change the map without pushing an undo command (setPlayerStart, newMap),
    // so mutated() reports them even though the placed/painted counters stay at 0.
    bool _mutatedDirectly = false;
    std::unordered_map<std::string, pattern::Pattern> _stamps;
    // When non-null, mutators record into this plan instead of committing (see setPlanSink). Borrowed.
    pattern::FillPlan* _planSink = nullptr;
    // The selection area the area-queries report (see setArea). Borrowed; null when none is bound.
    const EditArea* _area = nullptr;
    // Deterministic stream for rng()/rngInt(); reseed per run via setSeed for reproducible scatter.
    std::mt19937 _rng; // NOSONAR: seeded for reproducible fills, not a security-sensitive use
};

} // namespace geck
