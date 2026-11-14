#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "editor/HexGeometry.h"
#include "editor/HexagonGrid.h"
#include "editor/helper/MapClipboard.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "format/map/Tile.h"
#include "pattern/FillPlan.h"
#include "pattern/PlacementBatch.h"
#include "selection/SelectionState.h"

#include "support/ControllerFixture.h"

// MapClipboard is the pure core of copy/cut/paste: capture turns a selection into offsets from an
// even-aligned anchor, place() builds a fresh, translated, re-identified copy for one drop. The
// editor only adds sprites and commits the placement as a single undo entry.

using namespace geck;
using geck::test::ControllerFixture;

namespace {

constexpr int ELEV = 0;
constexpr uint16_t EMPTY = static_cast<uint16_t>(Map::EMPTY_TILE);
constexpr uint16_t FLOOR_ID = 271;
constexpr uint16_t ROOF_ID = 300;

int hexAt(int col, int row) {
    return row * hexgrid::WIDTH + col;
}

int tileAt(int col, int row) {
    return row * HexagonGrid::TILE_GRID_WIDTH + col;
}

std::shared_ptr<MapObject> makeObject(int hex, uint32_t proPid, uint32_t id) {
    auto object = std::make_shared<MapObject>();
    object->position = hex;
    object->elevation = ELEV;
    object->pro_pid = proPid;
    object->frm_pid = 0x02000010;
    object->unknown0 = id;
    return object;
}

std::unique_ptr<MapObject> makeItem(uint32_t proPid, uint32_t id) {
    auto item = std::make_unique<MapObject>();
    item->pro_pid = proPid;
    item->unknown0 = id;
    item->amount = 3;
    return item;
}

// Stand in for the editor's commit: put a placement into the map so the next place() sees it.
void commit(Map::MapFile& map, const MapClipboard::Placement& placement) {
    for (const auto& entry : placement.objects) {
        map.map_objects[static_cast<int>(entry.object->elevation)].push_back(entry.object);
        for (const MapScript& script : entry.scripts) {
            map.map_scripts[MapScript::sidSection(script.pid)].push_back(script);
        }
    }
}

void collectIds(const MapObject& object, std::vector<uint32_t>& out) {
    out.push_back(object.unknown0);
    for (const auto& item : object.inventory) {
        collectIds(*item, out);
    }
}

Map::MapFile emptyMap() {
    Map::MapFile map = Map::createEmptyMapFile();
    map.tiles[ELEV] = std::vector<Tile>(Map::TILES_PER_ELEVATION, Tile(EMPTY, EMPTY));
    return map;
}

} // namespace

TEST_CASE("capture stores offsets from an anchor centred on the copy and snapped to even", "[clipboard]") {
    const Map::MapFile map = emptyMap();
    // Objects at (41,31) and (45,37); a floor tile at tile (20,15) covers hexes (40..41, 30..31).
    const auto a = makeObject(hexAt(41, 31), 0x02000001, 1);
    const auto b = makeObject(hexAt(45, 37), 0x02000002, 2);

    MapClipboard clipboard;
    REQUIRE(clipboard.capture({ a.get(), b.get() }, { { tileAt(20, 15), FLOOR_ID } }, {}, map));

    // Bounds cols 40..45, rows 30..37: centre (42, 33), snapped to the even row 32.
    CHECK(hexgrid::columnOf(clipboard.anchorHex()) == 42);
    CHECK(hexgrid::rowOf(clipboard.anchorHex()) == 32);

    REQUIRE(clipboard.objects().size() == 2);
    CHECK(clipboard.objects()[0].dCol == -1);
    CHECK(clipboard.objects()[0].dRow == -1);
    CHECK(clipboard.objects()[1].dCol == 3);
    CHECK(clipboard.objects()[1].dRow == 5);

    // Tile offsets are relative to the anchor's tile (21, 16).
    REQUIRE(clipboard.floor().size() == 1);
    CHECK(clipboard.floor()[0].dCol == -1);
    CHECK(clipboard.floor()[0].dRow == -1);
    CHECK(clipboard.floor()[0].tileId == FLOOR_ID);
    CHECK(clipboard.roof().empty());
}

TEST_CASE("capture snaps the anchor to an even column and row for any bounds", "[clipboard]") {
    const Map::MapFile map = emptyMap();
    for (const int hex : { hexAt(11, 11), hexAt(10, 11), hexAt(11, 10), hexAt(10, 10), hexAt(199, 199) }) {
        const auto object = makeObject(hex, 0x02000001, 1);
        MapClipboard clipboard;
        REQUIRE(clipboard.capture({ object.get() }, {}, {}, map));
        INFO("hex " << hex);
        CHECK(hexgrid::columnOf(clipboard.anchorHex()) % 2 == 0);
        CHECK(hexgrid::rowOf(clipboard.anchorHex()) % 2 == 0);
    }
}

TEST_CASE("capture skips empty floor and roof squares", "[clipboard]") {
    const Map::MapFile map = emptyMap();
    MapClipboard clipboard;
    REQUIRE(clipboard.capture({},
        { { tileAt(10, 10), FLOOR_ID }, { tileAt(11, 10), EMPTY } },
        { { tileAt(10, 10), EMPTY }, { tileAt(11, 10), ROOF_ID } }, map));
    REQUIRE(clipboard.floor().size() == 1);
    CHECK(clipboard.floor()[0].tileId == FLOOR_ID);
    REQUIRE(clipboard.roof().size() == 1);
    CHECK(clipboard.roof()[0].tileId == ROOF_ID);

    // Nothing but empty squares: nothing to copy, and a previous copy is gone.
    CHECK_FALSE(clipboard.capture({}, { { tileAt(10, 10), EMPTY } }, { { tileAt(10, 10), EMPTY } }, map));
    CHECK(clipboard.isEmpty());
}

TEST_CASE("captureSelection reads the selected tiles from the map and ignores hexes", "[clipboard]") {
    using selection::SelectionType;
    Map::MapFile map = emptyMap();
    map.tiles[ELEV][tileAt(10, 10)].setFloor(FLOOR_ID);
    map.tiles[ELEV][tileAt(11, 10)].setRoof(ROOF_ID);

    selection::SelectionState hexesOnly;
    hexesOnly.addItem({ SelectionType::HEX, hexAt(20, 20) });
    MapClipboard clipboard;
    CHECK_FALSE(clipboard.captureSelection(hexesOnly, map, ELEV));
    CHECK(clipboard.isEmpty());

    selection::SelectionState state;
    state.addItem({ SelectionType::FLOOR_TILE, tileAt(10, 10) });
    state.addItem({ SelectionType::FLOOR_TILE, tileAt(12, 10) }); // empty on the map: skipped
    state.addItem({ SelectionType::ROOF_TILE, tileAt(11, 10) });
    state.addItem({ SelectionType::HEX, hexAt(60, 60) });
    REQUIRE(clipboard.captureSelection(state, map, ELEV));
    REQUIRE(clipboard.floor().size() == 1);
    CHECK(clipboard.floor()[0].tileId == FLOOR_ID);
    REQUIRE(clipboard.roof().size() == 1);
    CHECK(clipboard.roof()[0].tileId == ROOF_ID);
    CHECK(clipboard.objects().empty());
    // The hex far away does not stretch the bounds (hexes 20..23 x 20..21): the anchor sits on the
    // copied tiles.
    CHECK(hexgrid::columnOf(clipboard.anchorHex()) == 20);
    CHECK(hexgrid::rowOf(clipboard.anchorHex()) == 20);
}

TEST_CASE("capture deep-copies objects with their inventory", "[clipboard]") {
    const Map::MapFile map = emptyMap();
    auto container = makeObject(hexAt(60, 60), 0x00000010, 5);
    container->inventory.push_back(makeItem(0x00000029, 6));
    container->inventory.push_back(makeItem(0x0000002A, 7));
    container->inventory.back()->inventory.push_back(makeItem(0x0000002B, 8)); // a bag in the box
    container->objects_in_inventory = 2;
    container->light_radius = 4;
    container->flags = 0x2000;

    MapClipboard clipboard;
    REQUIRE(clipboard.capture({ container.get() }, {}, {}, map));
    REQUIRE(clipboard.objects().size() == 1);
    const MapObject& copy = *clipboard.objects()[0].object;
    CHECK(&copy != container.get());
    CHECK(copy.pro_pid == container->pro_pid);
    CHECK(copy.light_radius == 4);
    CHECK(copy.flags == 0x2000);
    CHECK(copy.objects_in_inventory == 2);
    REQUIRE(copy.inventory.size() == 2);
    CHECK(copy.inventory[0].get() != container->inventory[0].get());
    CHECK(copy.inventory[0]->pro_pid == 0x00000029);
    CHECK(copy.inventory[0]->amount == 3);
    REQUIRE(copy.inventory[1]->inventory.size() == 1);
    CHECK(copy.inventory[1]->inventory[0]->pro_pid == 0x0000002B);

    // Later edits to the map's object don't reach the clipboard.
    container->inventory.clear();
    container->light_radius = 0;
    CHECK(copy.inventory.size() == 2);
    CHECK(copy.light_radius == 4);
}

TEST_CASE("place translates with the stamp's parity snapping and keeps tiles under objects", "[clipboard]") {
    using namespace geck::hexgrid;
    const Map::MapFile source = emptyMap();
    // An object an odd number of hexes from where the anchor will be, plus the tile under it.
    const auto object = makeObject(hexAt(83, 83), 0x02000001, 1);
    MapClipboard clipboard;
    REQUIRE(clipboard.capture({ object.get() }, { { tileAt(83 / 2, 83 / 2), FLOOR_ID } }, {}, source));

    const Map::MapFile target = emptyMap();
    for (const int hex : { hexAt(50, 50), hexAt(51, 50), hexAt(50, 51), hexAt(51, 51) }) {
        INFO("target " << hex);
        const int snapped = MapClipboard::snapTarget(hex);
        CHECK(snapped == hexAt(50, 50)); // moves in 2-hex steps

        const auto placement = clipboard.place(hex, ELEV, target);
        REQUIRE(placement.objects.size() == 1);
        REQUIRE(placement.tiles.size() == 1);
        const int placed = placement.objects[0].object->position;
        CHECK(placed == hexAt(50 + clipboard.objects()[0].dCol, 50 + clipboard.objects()[0].dRow));
        // The copied floor is still the tile under the object.
        CHECK(placement.tiles[0].tileIndex == tileAt(columnOf(placed) / 2, rowOf(placed) / 2));
        CHECK(placement.tiles[0].after == FLOOR_ID);
        CHECK(placement.tiles[0].before == EMPTY);
        CHECK_FALSE(placement.tiles[0].isRoof);
    }
}

TEST_CASE("place keeps every copied field, including exit grid and stair destinations", "[clipboard]") {
    const Map::MapFile source = emptyMap();
    auto exitGrid = makeObject(hexAt(40, 40), 0x05000010, 10);
    exitGrid->exit_map = 12;
    exitGrid->exit_position = 20100;
    exitGrid->exit_elevation = 1;
    exitGrid->exit_orientation = 3;
    auto stairs = makeObject(hexAt(42, 40), 0x02000123, 11);
    stairs->elevhex = (1u << 26) | 15000u;
    stairs->map = 7;
    stairs->direction = 2;
    stairs->walkthrough = 1;
    stairs->light_radius = 5;
    stairs->light_intensity = 0x8000;
    stairs->x = 4;
    stairs->y = 2;
    auto critter = makeObject(hexAt(44, 40), 0x01000040, 12);
    critter->critter_index = 3;
    critter->ai_packet = 41;
    critter->group_id = 2;
    critter->current_hp = 30;

    MapClipboard clipboard;
    REQUIRE(clipboard.capture({ exitGrid.get(), stairs.get(), critter.get() }, {}, {}, source));

    constexpr int TARGET_ELEVATION = 2;
    const Map::MapFile target = emptyMap();
    const auto placement = clipboard.place(hexAt(100, 100), TARGET_ELEVATION, target);
    REQUIRE(placement.objects.size() == 3);

    const MapObject& e = *placement.objects[0].object;
    CHECK(e.exit_map == 12);
    CHECK(e.exit_position == 20100); // a destination elsewhere, never translated
    CHECK(e.exit_elevation == 1);
    CHECK(e.exit_orientation == 3);

    const MapObject& s = *placement.objects[1].object;
    CHECK(s.elevhex == ((1u << 26) | 15000u));
    CHECK(s.map == 7);
    CHECK(s.direction == 2);
    CHECK(s.walkthrough == 1);
    CHECK(s.light_radius == 5);
    CHECK(s.light_intensity == 0x8000);
    CHECK(s.x == 4);
    CHECK(s.y == 2);
    CHECK(s.frm_pid == stairs->frm_pid);

    const MapObject& c = *placement.objects[2].object;
    CHECK(c.ai_packet == 41);
    CHECK(c.group_id == 2);
    CHECK(c.current_hp == 30);
    CHECK(c.critter_index == -1); // like any newly placed object

    for (const auto& entry : placement.objects) {
        CHECK(entry.object->elevation == static_cast<uint32_t>(TARGET_ELEVATION));
    }
    // The shape is kept: same spacing as the source.
    CHECK(s.position - e.position == stairs->position - exitGrid->position);
    CHECK(c.position - e.position == critter->position - exitGrid->position);
}

TEST_CASE("place gives every copy a new object id, unique across the map and repeated drops", "[clipboard]") {
    Map::MapFile map = emptyMap();
    auto a = makeObject(hexAt(30, 30), 0x02000001, 41);
    auto box = makeObject(hexAt(32, 30), 0x00000010, 57);
    box->inventory.push_back(makeItem(0x00000029, 90)); // the highest id is inside a container
    map.map_objects[ELEV] = { a, box };
    map.map_scripts[static_cast<int>(MapScript::ScriptType::SPATIAL)].push_back(
        MapScript::makeSpatialScript(0, 5, 1000, ELEV, 2, 80));

    MapClipboard clipboard;
    REQUIRE(clipboard.capture({ a.get(), box.get() }, {}, {}, map));

    std::set<uint32_t> seen;
    std::vector<uint32_t> existing;
    for (const auto& object : map.map_objects[ELEV]) {
        collectIds(*object, existing);
    }
    seen.insert(existing.begin(), existing.end());

    for (int drop = 0; drop < 3; ++drop) {
        const auto placement = clipboard.place(hexAt(100 + drop * 10, 100), ELEV, map);
        REQUIRE(placement.objects.size() == 2);
        std::vector<uint32_t> ids;
        for (const auto& entry : placement.objects) {
            collectIds(*entry.object, ids);
        }
        CHECK(ids.size() == 3); // two objects and the box's item
        for (const uint32_t id : ids) {
            INFO("drop " << drop << " id " << id);
            CHECK(id > 90);
            CHECK(seen.insert(id).second);
        }
        // Every drop clones afresh: nothing is shared with the clipboard or an earlier drop.
        CHECK(placement.objects[0].object.get() != clipboard.objects()[0].object.get());
        commit(map, placement);
    }
    // The source objects keep their ids.
    CHECK(a->unknown0 == 41);
    CHECK(box->unknown0 == 57);
}

TEST_CASE("place gives each scripted copy its own script record running the same program", "[clipboard]") {
    using enum MapScript::ScriptType;
    Map::MapFile map = emptyMap();
    constexpr int ITEM_SECTION = static_cast<int>(ITEM);
    constexpr int CRITTER_SECTION = static_cast<int>(CRITTER);

    auto door = makeObject(hexAt(30, 30), 0x02000200, 5);
    const MapScript doorScript = MapScript::makeObjectScript(ITEM, 4, 123, 5);
    door->map_scripts_pid = static_cast<int32_t>(doorScript.pid);
    auto guard = makeObject(hexAt(34, 30), 0x01000040, 6);
    const MapScript guardScript = MapScript::makeObjectScript(CRITTER, 0, 77, 6);
    guard->map_scripts_pid = static_cast<int32_t>(guardScript.pid);
    auto note = makeItem(0x00000030, 7); // a scripted item the guard carries
    const MapScript noteScript = MapScript::makeObjectScript(ITEM, 9, 300, 7);
    note->map_scripts_pid = static_cast<int32_t>(noteScript.pid);
    guard->inventory.push_back(std::move(note));
    auto rock = makeObject(hexAt(36, 30), 0x02000001, 8); // no script
    auto dangling = makeObject(hexAt(38, 30), 0x02000001, 9);
    dangling->map_scripts_pid = static_cast<int32_t>(MapScript::makeSid(ITEM, 55)); // no such record

    map.map_objects[ELEV] = { door, guard, rock, dangling };
    map.map_scripts[ITEM_SECTION] = { doorScript, noteScript };
    map.map_scripts[CRITTER_SECTION] = { guardScript };
    const auto spatial = MapScript::makeSpatialScript(0, 9, 1000, ELEV, 3, 50);
    map.map_scripts[static_cast<int>(SPATIAL)] = { spatial };

    MapClipboard clipboard;
    REQUIRE(clipboard.capture({ door.get(), guard.get(), rock.get(), dangling.get() }, {}, {}, map));

    std::set<uint32_t> sids;
    for (const auto& section : map.map_scripts) {
        for (const MapScript& script : section) {
            sids.insert(script.pid);
        }
    }

    for (int drop = 0; drop < 2; ++drop) {
        INFO("drop " << drop);
        const auto placement = clipboard.place(hexAt(120, 120 + drop * 10), ELEV, map);
        REQUIRE(placement.objects.size() == 4);
        CHECK(placement.scriptCount() == 3);

        const auto checkScript = [&](const MapObject& owner, const MapScript& script, int section, uint32_t program) {
            CHECK(MapScript::sidSection(script.pid) == section);
            CHECK(script.script_id == program);
            CHECK(script.script_oid == owner.unknown0);
            CHECK(owner.map_scripts_pid == static_cast<int32_t>(script.pid));
            CHECK(sids.insert(script.pid).second); // a new SID, never one already in the map
        };

        const auto& doorEntry = placement.objects[0];
        REQUIRE(doorEntry.scripts.size() == 1);
        checkScript(*doorEntry.object, doorEntry.scripts[0], ITEM_SECTION, 123);

        const auto& guardEntry = placement.objects[1];
        REQUIRE(guardEntry.scripts.size() == 2);
        checkScript(*guardEntry.object, guardEntry.scripts[0], CRITTER_SECTION, 77);
        checkScript(*guardEntry.object->inventory[0], guardEntry.scripts[1], ITEM_SECTION, 300);

        CHECK(placement.objects[2].scripts.empty());
        CHECK(placement.objects[2].object->map_scripts_pid == -1);
        // A SID with no record in the source isn't carried into the copy.
        CHECK(placement.objects[3].scripts.empty());
        CHECK(placement.objects[3].object->map_scripts_pid == -1);

        commit(map, placement);
    }

    // The originals keep their scripts, and no spatial script was copied.
    CHECK(door->map_scripts_pid == static_cast<int32_t>(doorScript.pid));
    CHECK(map.map_scripts[static_cast<int>(SPATIAL)].size() == 1);
}

TEST_CASE("place drops and counts entries that would land off the map", "[clipboard]") {
    const Map::MapFile map = emptyMap();
    const auto left = makeObject(hexAt(20, 50), 0x02000001, 1);
    const auto right = makeObject(hexAt(30, 50), 0x02000002, 2);
    MapClipboard clipboard;
    REQUIRE(clipboard.capture({ left.get(), right.get() },
        { { tileAt(10, 25), FLOOR_ID }, { tileAt(15, 25), FLOOR_ID } }, { { tileAt(15, 25), ROOF_ID } }, map));
    REQUIRE(hexgrid::columnOf(clipboard.anchorHex()) == 24);

    // Dropped on the map's right edge, everything right of the anchor falls off.
    const auto placement = clipboard.place(hexAt(199, 50), ELEV, map);
    CHECK(placement.objectsDropped == 1);
    CHECK(placement.tilesDropped == 2);
    REQUIRE(placement.objects.size() == 1);
    CHECK(placement.objects[0].object->pro_pid == 0x02000001);
    REQUIRE(placement.tiles.size() == 1);
    CHECK_FALSE(placement.tiles[0].isRoof);

    // resolve() marks the same entries off-grid.
    const auto resolved = clipboard.resolve(hexAt(199, 50));
    CHECK(resolved.objectHexes[0] >= 0);
    CHECK(resolved.objectHexes[1] == -1);
    CHECK(resolved.floorTiles[1] == -1);
    CHECK(resolved.roofTiles[0] == -1);
}

TEST_CASE("a paste commits objects, tiles and scripts as one undo entry", "[clipboard]") {
    ControllerFixture fx;
    fx.mapFile().tiles[ELEV] = std::vector<Tile>(Map::TILES_PER_ELEVATION, Tile(EMPTY, EMPTY));

    auto door = makeObject(hexAt(30, 30), 0x02000200, 5);
    const MapScript doorScript = MapScript::makeObjectScript(MapScript::ScriptType::ITEM, 0, 123, 5);
    door->map_scripts_pid = static_cast<int32_t>(doorScript.pid);
    fx.mapFile().map_objects[ELEV] = { door };
    fx.mapFile().map_scripts[static_cast<int>(MapScript::ScriptType::ITEM)] = { doorScript };
    fx.mapFile().scripts_in_section[static_cast<int>(MapScript::ScriptType::ITEM)] = 1;

    MapClipboard clipboard;
    REQUIRE(clipboard.capture({ door.get() }, { { tileAt(15, 15), FLOOR_ID } }, {}, fx.mapFile()));
    const auto placement = clipboard.place(hexAt(100, 100), ELEV, fx.mapFile());
    REQUIRE(placement.objects.size() == 1);
    REQUIRE(placement.tiles.size() == 1);

    // The editor's commit, data-only: placement and script records inside one outer batch.
    pattern::FillPlan plan;
    plan.objects.push_back({ placement.objects[0].object, nullptr });
    plan.tiles = placement.tiles;
    {
        ScopedUndoBatch batch(fx.controller, "Paste");
        pattern::PlacementBatch::replay(fx.controller, plan, /*buildSprites*/ false, "Paste");
        REQUIRE(fx.controller.addObjectScripts(placement.objects[0].scripts));
    }

    auto& itemScripts = fx.mapFile().map_scripts[static_cast<int>(MapScript::ScriptType::ITEM)];
    CHECK(fx.mapFile().map_objects[ELEV].size() == 2);
    CHECK(itemScripts.size() == 2);
    CHECK(fx.mapFile().scripts_in_section[static_cast<int>(MapScript::ScriptType::ITEM)] == 2);
    CHECK(fx.mapFile().tiles[ELEV][placement.tiles[0].tileIndex].getFloor() == FLOOR_ID);

    // One Ctrl-Z takes the object, its script record and the tiles back out, and nothing else.
    REQUIRE(fx.undoStack.canUndo());
    fx.undoStack.undo();
    CHECK_FALSE(fx.undoStack.canUndo());
    REQUIRE(fx.mapFile().map_objects[ELEV].size() == 1);
    CHECK(fx.mapFile().map_objects[ELEV][0] == door);
    REQUIRE(itemScripts.size() == 1);
    CHECK(itemScripts[0].pid == doorScript.pid);
    CHECK(fx.mapFile().scripts_in_section[static_cast<int>(MapScript::ScriptType::ITEM)] == 1);
    CHECK(fx.mapFile().tiles[ELEV][placement.tiles[0].tileIndex].getFloor() == EMPTY);

    fx.undoStack.redo();
    CHECK(fx.mapFile().map_objects[ELEV].size() == 2);
    CHECK(itemScripts.size() == 2);
}
