#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include "editor/helper/ObjectQueries.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "writer/pro/ProWriter.h"

#include "support/DataTree.h"

namespace fs = std::filesystem;
using namespace geck;
using geck::resource::GameResources;

namespace {

// Writes a minimal valid WALL .pro carrying the given header flags. object_query
// only reads header.flags, but the file must still parse as a real PRO.
void writeWallPro(const fs::path& path, uint32_t index, uint32_t flags) {
    fs::create_directories(path.parent_path());

    Pro pro{ path }; // constructor zero-initialises the header and all data blocks
    pro.header.PID = static_cast<int32_t>(ProtoId(ObjectType::Wall, index).pid());
    pro.header.flags = flags;

    ProWriter writer;
    writer.openFile(path);
    REQUIRE(writer.write(pro));
}

MapObject wallObject(uint32_t index) {
    MapObject object;
    object.pro_pid = ProtoId(ObjectType::Wall, index).pid();
    return object;
}

// A temp data tree with a proto/walls list + three crafted wall PROs, mounted
// into a GameResources so object_query resolves them through the resource stack.
struct ProFixture {
    test::DataTree tree{ "object_query" };

    // walls.lst line indices (1-based), each pointing at a wall PRO crafted below.
    static constexpr uint32_t BLOCKING = 1;      // PRO has no NoBlock flag    -> blocks
    static constexpr uint32_t NON_BLOCKING = 2;  // PRO has the NoBlock flag    -> passable
    static constexpr uint32_t SHOOT_THROUGH = 3; // PRO has the ShootThrough flag

    ProFixture() {
        tree.write("proto/walls/walls.lst", "blocking.pro\nnoblock.pro\nshoot.pro\n");
        writeWallPro(tree.root() / "proto/walls/blocking.pro", BLOCKING, 0x00000000);
        writeWallPro(tree.root() / "proto/walls/noblock.pro", NON_BLOCKING, 0x00000010); // NoBlock
        writeWallPro(tree.root() / "proto/walls/shoot.pro", SHOOT_THROUGH, 0x80000000);  // ShootThrough
    }

    GameResources& resources() { return tree.mount(); }
};

} // namespace

TEST_CASE("object_query::blocksMovement reads the PRO NoBlock flag", "[object_query]") {
    ProFixture fx;

    // No NoBlock flag -> the object blocks movement.
    const auto blocking = wallObject(ProFixture::BLOCKING);
    CHECK(object_query::blocksMovement(blocking, fx.resources()));

    // NoBlock flag set -> passable.
    const auto passable = wallObject(ProFixture::NON_BLOCKING);
    CHECK_FALSE(object_query::blocksMovement(passable, fx.resources()));
}

TEST_CASE("object_query::isShootThroughWallBlocker reads the PRO ShootThrough flag", "[object_query]") {
    ProFixture fx;

    const auto shootThrough = wallObject(ProFixture::SHOOT_THROUGH);
    CHECK(object_query::isShootThroughWallBlocker(shootThrough, fx.resources()));
    // ShootThrough does not imply NoBlock, so it still blocks movement.
    CHECK(object_query::blocksMovement(shootThrough, fx.resources()));

    const auto plain = wallObject(ProFixture::BLOCKING);
    CHECK_FALSE(object_query::isShootThroughWallBlocker(plain, fx.resources()));
}

TEST_CASE("object_query treats an unresolvable PRO as non-blocking", "[object_query]") {
    ProFixture fx;

    // Index 99 is past the end of walls.lst, so the PRO cannot be resolved.
    const auto unknown = wallObject(99);
    CHECK_FALSE(object_query::blocksMovement(unknown, fx.resources()));
    CHECK_FALSE(object_query::isShootThroughWallBlocker(unknown, fx.resources()));
}

// The engine reads an object's flags and light from the map, never again from its proto, and
// obj_new (fallout2-ce object.cc objectCreateWithFrmIdProtoId) is what seeds them. Writing flags = 0
// made a placed exit grid stand over the walls, a NoBlock weed block movement and a lamp unlit.
TEST_CASE("applyProtoDefaults seeds flags and light the way the engine's obj_new does", "[object_query]") {
    using Flag = Pro::ObjectFlags;
    const auto bits = [](std::initializer_list<Flag> flags) {
        uint32_t out = 0;
        for (const Flag flag : flags) {
            out |= static_cast<uint32_t>(flag);
        }
        return out;
    };

    Pro pro{ "seed.pro" };
    MapObject object;
    object.flags = 0xFFFFFFFF; // anything already there is replaced
    object.light_radius = 3;
    object.light_intensity = 100;

    SECTION("the proto's flags are copied, with OBJECT_HIDDEN and the runtime bits left out") {
        pro.header.flags = bits({ Flag::OBJECT_FLAT, Flag::OBJECT_NO_BLOCK, Flag::OBJECT_MULTIHEX,
                               Flag::OBJECT_NO_HIGHLIGHT, Flag::OBJECT_WALL_TRANS_END, Flag::OBJECT_LIGHT_THRU,
                               Flag::OBJECT_SHOOT_THRU })
            | bits({ Flag::OBJECT_HIDDEN, Flag::OBJECT_QUEUED, Flag::OBJECT_SEEN });
        object.applyProtoDefaults(pro);
        CHECK(object.flags
            == bits({ Flag::OBJECT_FLAT, Flag::OBJECT_NO_BLOCK, Flag::OBJECT_MULTIHEX, Flag::OBJECT_NO_HIGHLIGHT,
                Flag::OBJECT_WALL_TRANS_END, Flag::OBJECT_LIGHT_THRU, Flag::OBJECT_SHOOT_THRU }));
        CHECK(object.light_radius == 0); // a proto with no light clears the object's
        CHECK(object.light_intensity == 0);
    }

    SECTION("an object takes one transparency, the first in obj_new's chain") {
        pro.header.flags = bits({ Flag::OBJECT_TRANS_RED, Flag::OBJECT_TRANS_GLASS, Flag::OBJECT_TRANS_ENERGY });
        object.applyProtoDefaults(pro);
        CHECK(object.flags == bits({ Flag::OBJECT_TRANS_GLASS }));

        pro.header.flags = bits({ Flag::OBJECT_TRANS_RED, Flag::OBJECT_TRANS_NONE, Flag::OBJECT_TRANS_WALL });
        object.applyProtoDefaults(pro);
        CHECK(object.flags == bits({ Flag::OBJECT_TRANS_NONE }));
    }

    SECTION("a lit proto lights the object, radius clamped to 8 and OBJECT_LIGHTING set") {
        // Without OBJECT_LIGHTING the engine's _obj_adjust_light never lights the object's hexes.
        pro.header.light_distance = 12;
        pro.header.light_intensity = 40000;
        object.applyProtoDefaults(pro);
        CHECK(object.light_radius == 8);
        CHECK(object.light_intensity == 40000);
        CHECK(object.flags == bits({ Flag::OBJECT_LIGHTING }));
    }
}

// _proto_update_init seeds each type's data too, and the engine reads it back from the map. Zeros
// left a placed critter dead (critterIsDead: HP <= 0), a weapon empty and stairs leading to map 0.
TEST_CASE("applyProtoDefaults seeds a new object's type data from its proto", "[object_query]") {
    SECTION("a critter is unhurt, at full AP, with its proto's AI and team, and nobody hit it") {
        Pro pro{ "critter.pro" };
        pro.header.PID = static_cast<int32_t>(ProtoId(ObjectType::Critter, 7).pid());
        pro.critterData.maxHitPoints = 30;
        pro.critterData.bonusHealthPoints = 5;
        pro.critterData.actionPoints = 7;
        pro.critterData.bonusActionPoints = 2;
        pro.critterData.aiPacket = 41;
        pro.critterData.teamNumber = 3;

        MapObject critter;
        critter.pro_pid = pro.pid().pid();
        critter.applyProtoDefaults(pro);
        CHECK(critter.current_hp == 35);
        CHECK(critter.current_ap == 9);
        CHECK(critter.ai_packet == 41);
        CHECK(critter.group_id == 3);
        CHECK(critter.who_hit_me == 0xFFFFFFFF); // whoHitMeCid -1, as every shipped critter has

        pro.critterData.actionPoints = 0; // critterGetStat clamps AP to 1..99
        pro.critterData.bonusActionPoints = 0;
        critter.applyProtoDefaults(pro);
        CHECK(critter.current_ap == 1);
    }

    SECTION("a weapon starts loaded with its proto's ammo") {
        Pro pro{ "weapon.pro" };
        pro.header.PID = static_cast<int32_t>(ProtoId(ObjectType::Item, 8).pid());
        pro.setObjectSubtypeId(static_cast<unsigned>(ItemType::Weapon));
        pro.weaponData.ammoCapacity = 12;
        pro.weaponData.ammoPID = 0x00000028;

        MapObject weapon;
        weapon.applyProtoDefaults(pro);
        CHECK(weapon.ammo == 12);
        CHECK(weapon.ammo_pid == 0x00000028);
    }

    SECTION("stairs lead where their proto says") {
        Pro pro{ "stairs.pro" };
        pro.header.PID = static_cast<int32_t>(ProtoId(ObjectType::Scenery, 9).pid());
        pro.setObjectSubtypeId(static_cast<unsigned>(SceneryType::Stairs));
        pro.sceneryData.stairsData.destTile = 0x20004E20;
        pro.sceneryData.stairsData.destElevation = 52; // the engine's destinationMap

        MapObject stairs;
        stairs.applyProtoDefaults(pro);
        CHECK(stairs.elevhex == 0x20004E20);
        CHECK(stairs.map == 52);
    }

    SECTION("an exit grid leads nowhere until given a destination") {
        Pro pro{ "exit.pro" };
        pro.header.PID = static_cast<int32_t>(ProtoId(ObjectType::Misc, 16).pid());

        MapObject exitGrid;
        exitGrid.applyProtoDefaults(pro);
        CHECK(exitGrid.exit_map == 0xFFFFFFFF);
        CHECK(exitGrid.exit_position == 0xFFFFFFFF);
    }
}

TEST_CASE("object_query::applyProtoDefaults resolves the PRO through the resources", "[object_query]") {
    ProFixture fx;

    auto passable = wallObject(ProFixture::NON_BLOCKING);
    CHECK(object_query::applyProtoDefaults(passable, fx.resources()));
    CHECK(passable.flags == static_cast<uint32_t>(Pro::ObjectFlags::OBJECT_NO_BLOCK));

    // An unresolvable PRO leaves the object as it was, as obj_new does.
    auto unknown = wallObject(99);
    unknown.flags = 0x10;
    CHECK_FALSE(object_query::applyProtoDefaults(unknown, fx.resources()));
    CHECK(unknown.flags == 0x10);
}
