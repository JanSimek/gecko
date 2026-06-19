#include <catch2/catch_test_macros.hpp>

#include <SFML/Graphics.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "editing/commands/ObjectCommandController.h"

#include "support/ControllerFixture.h"

using namespace geck;
using geck::test::ControllerFixture;

namespace {

constexpr int ITEM_SECTION = static_cast<int>(MapScript::ScriptType::ITEM);

// Adds an object on `elevation` carrying an attached ITEM script and returns it.
std::shared_ptr<MapObject> addScriptedObject(ControllerFixture& fx, int elevation, uint32_t oid, uint32_t scriptIndex) {
    auto& mf = fx.mapFile();
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

} // namespace

TEST_CASE("clearElevationObjects removes attached scripts and undo restores both", "[undo][controller]") {
    ControllerFixture fx;
    addScriptedObject(fx, 0, 1, 0);
    addScriptedObject(fx, 0, 2, 1);

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

TEST_CASE("registerObjectData places a MapObject as data only, undoably", "[undo][controller]") {
    ControllerFixture fx;

    auto obj = std::make_shared<MapObject>();
    obj->elevation = 0;
    obj->position = 12345;
    obj->pro_pid = 0x02000066; // Scrub

    // Records the map data without ever building a sprite (no MapSpriteLoader, no GL).
    REQUIRE(fx.controller.registerObjectData(obj));
    REQUIRE(fx.mapFile().map_objects[0].size() == 1);
    CHECK(fx.mapFile().map_objects[0][0] == obj);
    CHECK(fx.objects.empty()); // no render-side sprite registered

    REQUIRE(fx.undoStack.undo());
    CHECK(fx.mapFile().map_objects[0].empty());
    REQUIRE(fx.undoStack.redo());
    CHECK(fx.mapFile().map_objects[0].size() == 1);

    CHECK_FALSE(fx.controller.registerObjectData(nullptr));
}

TEST_CASE("clearElevationObjects returns false for an empty elevation", "[undo][controller]") {
    ControllerFixture fx;
    CHECK_FALSE(fx.controller.clearElevationObjects(0));
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("copyElevation detaches cloned objects from source scripts", "[undo][controller]") {
    ControllerFixture fx;
    auto src = addScriptedObject(fx, 0, 5, 0);
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
    addScriptedObject(fx, 0, 1, 0);
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

    // First ITEM script in an empty section gets index 0, so the SID is deterministic.
    const int32_t expectedSid = static_cast<int32_t>(MapScript::makeSid(MapScript::ScriptType::ITEM, 0));

    REQUIRE(fx.controller.attachScript(obj, ITEM_SECTION, /*programIndex*/ 42));
    REQUIRE(fx.mapFile().map_scripts[ITEM_SECTION].size() == 1);
    CHECK(obj->map_scripts_pid == expectedSid);

    // Undo removes the script and unlinks the object.
    REQUIRE(fx.undoStack.undo());
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].empty());
    CHECK(obj->map_scripts_pid == -1);

    // Redo re-attaches the same script.
    REQUIRE(fx.undoStack.redo());
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].size() == 1);
    CHECK(obj->map_scripts_pid == expectedSid);

    // Detaching removes it again, and undo restores the linkage.
    REQUIRE(fx.controller.detachScript(obj));
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].empty());
    CHECK(obj->map_scripts_pid == -1);

    REQUIRE(fx.undoStack.undo());
    CHECK(fx.mapFile().map_scripts[ITEM_SECTION].size() == 1);
    CHECK(obj->map_scripts_pid == expectedSid);
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

namespace {
// Records an undoable flags edit on a fresh object (applied immediately) so batch
// tests have observable, independent state to assert on.
std::shared_ptr<MapObject> editFlags(ObjectCommandController& controller, uint32_t flags) {
    auto obj = std::make_shared<MapObject>();
    MapObjectInstanceState before;
    MapObjectInstanceState after;
    after.flags = flags;
    controller.registerInstanceEdit(obj, before, after, "Edit Flags");
    return obj;
}
} // namespace

TEST_CASE("beginBatch/endBatch collapse many edits into one undo entry", "[undo][controller][batch]") {
    ControllerFixture fx;

    fx.controller.beginBatch("Batch edit");
    CHECK(fx.controller.isBatching());

    auto o1 = editFlags(fx.controller, 0x01);
    auto o2 = editFlags(fx.controller, 0x02);
    auto o3 = editFlags(fx.controller, 0x04);

    // Each edit is applied immediately, but nothing is on the stack mid-batch.
    CHECK(o1->flags == 0x01u);
    CHECK(o2->flags == 0x02u);
    CHECK(o3->flags == 0x04u);
    CHECK_FALSE(fx.undoStack.canUndo());

    CHECK(fx.controller.endBatch());
    CHECK_FALSE(fx.controller.isBatching());
    CHECK(fx.undoStack.lastUndoLabel() == "Batch edit");

    // A single undo reverts ALL three edits, and the stack then holds nothing else
    // (proving the batch is one entry, not three).
    REQUIRE(fx.undoStack.undo());
    CHECK(o1->flags == 0u);
    CHECK(o2->flags == 0u);
    CHECK(o3->flags == 0u);
    CHECK_FALSE(fx.undoStack.canUndo());

    // A single redo re-applies the whole batch.
    REQUIRE(fx.undoStack.redo());
    CHECK(o1->flags == 0x01u);
    CHECK(o2->flags == 0x02u);
    CHECK(o3->flags == 0x04u);
}

TEST_CASE("an empty batch records nothing", "[undo][controller][batch]") {
    ControllerFixture fx;

    fx.controller.beginBatch("Nothing happens");
    CHECK_FALSE(fx.controller.endBatch()); // false: nothing was recorded
    CHECK_FALSE(fx.controller.isBatching());
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("an unbalanced endBatch is a no-op", "[undo][controller][batch]") {
    ControllerFixture fx;
    CHECK_FALSE(fx.controller.endBatch());
    CHECK_FALSE(fx.controller.isBatching());
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("nested batches flush once at the outermost endBatch", "[undo][controller][batch]") {
    ControllerFixture fx;

    fx.controller.beginBatch("Outer");
    auto o1 = editFlags(fx.controller, 0x01);

    fx.controller.beginBatch("Inner");
    auto o2 = editFlags(fx.controller, 0x02);
    CHECK_FALSE(fx.controller.endBatch()); // inner close defers the flush
    CHECK(fx.controller.isBatching());     // still inside the outer batch
    CHECK_FALSE(fx.undoStack.canUndo());

    auto o3 = editFlags(fx.controller, 0x04);
    CHECK(fx.controller.endBatch()); // outer close flushes one combined command
    CHECK_FALSE(fx.controller.isBatching());

    // One undo reverts all three edits across both nesting levels.
    REQUIRE(fx.undoStack.undo());
    CHECK(o1->flags == 0u);
    CHECK(o2->flags == 0u);
    CHECK(o3->flags == 0u);
    CHECK_FALSE(fx.undoStack.canUndo());
}

TEST_CASE("ScopedUndoBatch flushes on scope exit, even via an exception", "[undo][controller][batch]") {
    ControllerFixture fx;
    std::shared_ptr<MapObject> o1;
    std::shared_ptr<MapObject> o2;

    SECTION("normal scope exit collapses to one entry") {
        {
            ScopedUndoBatch batch(fx.controller, "Scoped");
            o1 = editFlags(fx.controller, 0x01);
            o2 = editFlags(fx.controller, 0x02);
            CHECK(fx.controller.isBatching());
        }
        CHECK_FALSE(fx.controller.isBatching());

        REQUIRE(fx.undoStack.undo());
        CHECK(o1->flags == 0u);
        CHECK(o2->flags == 0u);
        CHECK_FALSE(fx.undoStack.canUndo());
    }

    SECTION("an exception mid-batch still flushes the buffered edits as one entry") {
        try {
            ScopedUndoBatch batch(fx.controller, "Aborted");
            o1 = editFlags(fx.controller, 0x01);
            o2 = editFlags(fx.controller, 0x02);
            throw std::runtime_error("simulated script abort");
        } catch (const std::runtime_error&) {
            // The guard's destructor ran during unwinding and flushed the batch.
        }

        CHECK_FALSE(fx.controller.isBatching());
        CHECK(fx.undoStack.canUndo());
        REQUIRE(fx.undoStack.undo());
        CHECK(o1->flags == 0u);
        CHECK(o2->flags == 0u);
        CHECK_FALSE(fx.undoStack.canUndo());
    }
}
