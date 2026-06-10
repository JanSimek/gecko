#include <catch2/catch_test_macros.hpp>

#include <SFML/Graphics.hpp>

#include <cstdint>
#include <memory>
#include <vector>

#include "editor/HexagonGrid.h"
#include "editor/Object.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "resource/GameResources.h"
#include "ui/editing/ObjectCommandController.h"
#include "ui/rendering/MapSpriteLoader.h"
#include "util/UndoStack.h"

using namespace geck;

namespace {

constexpr int ITEM_SECTION = static_cast<int>(MapScript::ScriptType::ITEM);

// Wires an ObjectCommandController to a fresh empty map. The resource and sprite
// dependencies are unused by the elevation commands under test (they only touch
// the map's objects and scripts), so default-constructed instances suffice.
struct ControllerFixture {
    resource::GameResources resources;
    HexagonGrid hexgrid;
    MapSpriteLoader spriteLoader{ resources, hexgrid };
    std::vector<std::shared_ptr<Object>> objects;
    std::vector<sf::Sprite> overlays;
    UndoStack undoStack;
    std::unique_ptr<Map> map;
    ObjectCommandController controller;

    ControllerFixture()
        : map(std::make_unique<Map>("test.map"))
        , controller(
              resources, map, hexgrid, spriteLoader, objects, overlays, undoStack,
              [] {}, [] {},
              [this](int elevation) -> std::vector<Tile>& { return map->getMapFile().tiles[elevation]; },
              [] { return 0; },
              [](int, bool, int) {}, [] {}) {
        map->setMapFile(std::make_unique<Map::MapFile>(Map::createEmptyMapFile()));
    }

    Map::MapFile& mapFile() { return map->getMapFile(); }

    // Adds an object on `elevation` carrying an attached ITEM script and returns it.
    std::shared_ptr<MapObject> addScriptedObject(int elevation, uint32_t oid, uint32_t scriptIndex) {
        auto& mf = mapFile();
        MapScript script = MapScript::makeObjectScript(
            MapScript::ScriptType::ITEM, scriptIndex, /*programIndex*/ 100, oid);

        auto obj = std::make_shared<MapObject>();
        obj->elevation = static_cast<uint32_t>(elevation);
        obj->unknown0 = oid;
        obj->map_scripts_pid = static_cast<int32_t>(script.pid);

        mf.map_scripts[ITEM_SECTION].push_back(script);
        mf.scripts_in_section[ITEM_SECTION] = static_cast<int>(mf.map_scripts[ITEM_SECTION].size());
        mf.map_objects[elevation].push_back(obj);
        return obj;
    }
};

} // namespace

TEST_CASE("clearElevationObjects removes attached scripts and undo restores both", "[undo][controller]") {
    ControllerFixture fx;
    fx.addScriptedObject(0, 1, 0);
    fx.addScriptedObject(0, 2, 1);

    REQUIRE(fx.mapFile().map_objects[0].size() == 2);
    REQUIRE(fx.mapFile().map_scripts[ITEM_SECTION].size() == 2);

    REQUIRE(fx.controller.clearElevationObjects(0));

    // Objects and their scripts are gone, with the section count kept in sync.
    CHECK(fx.mapFile().map_objects[0].empty());
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].empty());
    CHECK(fx.mapFile().scripts_in_section[ITEM_SECTION] == 0);

    REQUIRE(fx.undoStack.undo());
    CHECK(fx.mapFile().map_objects[0].size() == 2);
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].size() == 2);
    CHECK(fx.mapFile().scripts_in_section[ITEM_SECTION] == 2);

    REQUIRE(fx.undoStack.redo());
    CHECK(fx.mapFile().map_objects[0].empty());
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].empty());
}

TEST_CASE("clearElevationObjects returns false for an empty elevation", "[undo][controller]") {
    ControllerFixture fx;
    CHECK_FALSE(fx.controller.clearElevationObjects(0));
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("copyElevation detaches cloned objects from source scripts", "[undo][controller]") {
    ControllerFixture fx;
    auto src = fx.addScriptedObject(0, 5, 0);
    const size_t scriptsBefore = fx.mapFile().map_scripts[ITEM_SECTION].size();

    REQUIRE(fx.controller.copyElevation(0, 1));

    REQUIRE(fx.mapFile().map_objects[1].size() == 1);
    auto clone = fx.mapFile().map_objects[1].front();

    // The clone is a distinct object detached from the source's script: no shared
    // SID and no duplicated OID, so it cannot collide with the original.
    CHECK(clone.get() != src.get());
    CHECK(clone->map_scripts_pid == -1);
    CHECK(clone->script_id == -1);
    CHECK(clone->unknown0 == 0u);
    CHECK(clone->elevation == 1u);

    // Scripts are not copied; the section and the source object are untouched.
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].size() == scriptsBefore);
    CHECK(src->map_scripts_pid != -1);
    CHECK(fx.mapFile().map_objects[0].size() == 1);

    // Undo restores the destination to its prior (empty) state.
    REQUIRE(fx.undoStack.undo());
    CHECK(fx.mapFile().map_objects[1].empty());

    REQUIRE(fx.undoStack.redo());
    CHECK(fx.mapFile().map_objects[1].size() == 1);
}

TEST_CASE("copyElevation rejects copying an elevation onto itself", "[undo][controller]") {
    ControllerFixture fx;
    fx.addScriptedObject(0, 1, 0);
    CHECK_FALSE(fx.controller.copyElevation(0, 0));
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("registerInstanceEdit applies the after-state and reverts on undo", "[undo][controller]") {
    ControllerFixture fx;
    auto obj = std::make_shared<MapObject>(); // starts in the before-state (all zero)

    MapObjectInstanceState before; // default-constructed zeros
    MapObjectInstanceState after;
    after.flags = 0x10;
    after.lightRadius = 5;

    REQUIRE(fx.controller.registerInstanceEdit(obj, before, after, "Edit Flags"));
    CHECK(obj->flags == 0x10u);
    CHECK(obj->light_radius == 5u);

    REQUIRE(fx.undoStack.undo());
    CHECK(obj->flags == 0u);
    CHECK(obj->light_radius == 0u);

    REQUIRE(fx.undoStack.redo());
    CHECK(obj->flags == 0x10u);
    CHECK(obj->light_radius == 5u);
}

TEST_CASE("registerInstanceEdit rejects a null object", "[undo][controller]") {
    ControllerFixture fx;
    CHECK_FALSE(fx.controller.registerInstanceEdit(nullptr, {}, {}, "Edit"));
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("attachScript and detachScript are undoable", "[undo][controller]") {
    ControllerFixture fx;
    auto obj = std::make_shared<MapObject>();
    REQUIRE(fx.mapFile().map_scripts[ITEM_SECTION].empty());

    REQUIRE(fx.controller.attachScript(obj, ITEM_SECTION, /*programIndex*/ 42));
    REQUIRE(fx.mapFile().map_scripts[ITEM_SECTION].size() == 1);
    REQUIRE(obj->map_scripts_pid != -1);
    const int32_t attachedSid = obj->map_scripts_pid;

    // Undo removes the script and unlinks the object.
    REQUIRE(fx.undoStack.undo());
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].empty());
    CHECK(obj->map_scripts_pid == -1);

    // Redo re-attaches the same script.
    REQUIRE(fx.undoStack.redo());
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].size() == 1);
    CHECK(obj->map_scripts_pid == attachedSid);

    // Detaching removes it again, and undo restores the linkage.
    REQUIRE(fx.controller.detachScript(obj));
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].empty());
    CHECK(obj->map_scripts_pid == -1);

    REQUIRE(fx.undoStack.undo());
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].size() == 1);
    CHECK(obj->map_scripts_pid == attachedSid);
}

TEST_CASE("registerInventoryEdit restores inventory snapshots", "[undo][controller]") {
    ControllerFixture fx;
    auto container = std::make_shared<MapObject>();

    auto before = ObjectCommandController::cloneInventory(container->inventory); // empty

    auto item = std::make_unique<MapObject>();
    item->pro_pid = 1234;
    container->inventory.push_back(std::move(item)); // caller applies the edit
    auto after = ObjectCommandController::cloneInventory(container->inventory);

    REQUIRE(fx.controller.registerInventoryEdit(container, before, after));
    CHECK(container->inventory.size() == 1);

    REQUIRE(fx.undoStack.undo());
    CHECK(container->inventory.empty());

    REQUIRE(fx.undoStack.redo());
    REQUIRE(container->inventory.size() == 1);
    CHECK(container->inventory.front()->pro_pid == 1234u);
}
