#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "reader/map/MapReader.h"
#include "support/Fixtures.h"
#include "support/ProStubProvider.h"
#include "support/TempFile.h"

#include <fstream>
#include <vector>

using namespace geck;

// Parses a real shipped map (sfshutl2.map) to guard the reader against
// assumptions the fully-synthetic round-trip can't exercise: real header
// values, real per-elevation framing, a real tile block, and real objects.
//
// The map references 10 distinct SCENERY protos (pids 33555970..33555979), all
// of the GENERIC subtype, and no ITEM objects. Only ITEM/SCENERY objects
// dereference a PRO during parsing, so a stub provider supplying just those
// subtypes lets the whole map parse without the (uncommitted) game proto tree.
TEST_CASE("MapReader parses the real sfshutl2 map", "[map]") {
    geck::test::StubProvider provider;
    for (uint32_t pid = 33555970u; pid <= 33555979u; ++pid) {
        provider.addScenery(pid, Pro::SCENERY_TYPE::GENERIC);
    }

    MapReader reader{ [&provider](uint32_t pid) { return provider.load(pid); } };
    auto map = reader.openFile(geck::test::dataPath("sfshutl2.map"));
    REQUIRE(map != nullptr);
    const auto& mf = map->getMapFile();

    // Header.
    CHECK(mf.header.version == 20);
    CHECK(mf.header.filename.substr(0, 12) == "SFSHUTL2.MAP");
    CHECK(mf.header.player_default_position == 20100);
    CHECK(mf.header.player_default_elevation == 0);
    // flags 0x4|0x8 mark elevations 1 and 2 as absent; only elevation 0 ships.
    CHECK(mf.header.flags == 0x0Cu);

    // Objects are framed as three per-elevation blocks; only elevation 0 holds any.
    REQUIRE(mf.map_objects.at(0).size() == 227);
    CHECK(mf.map_objects.at(1).empty());
    CHECK(mf.map_objects.at(2).empty());

    // Only elevation 0's tile block is present, with a full floor+roof grid.
    CHECK(mf.tiles.count(0) == 1);
    CHECK(mf.tiles.count(1) == 0);
    CHECK(mf.tiles.count(2) == 0);
    CHECK(mf.tiles.at(0).size() == Map::TILES_PER_ELEVATION);

    // This map carries no scripts in any section.
    for (const auto& section : mf.map_scripts) {
        CHECK(section.empty());
    }

    // Every parsed object belongs to elevation 0.
    for (const auto& object : mf.map_objects.at(0)) {
        REQUIRE(object != nullptr);
        CHECK(object->elevation == 0u);
    }
}

// Each script section stores its scripts in a block of 16 (the count rounded up). The real scripts in a
// section are all one type, but the PADDING slots beyond `count` hold leftover/garbage pids of arbitrary
// types — and a slot's on-disk size depends on its OWN pid's top byte (SPATIAL +2 ints, TIMER +1, else
// +0), matching the engine's scriptRead (scripts.cc). A parser that strides over the block by a fixed
// per-script size (e.g. `remaining * sizeof(section_type)`) desyncs on mixed-type padding and lands at
// the wrong offset for the following objects section. This crafts exactly that case — one real spatial
// script followed by 15 mixed-type padding slots — and checks the objects section is still reached.
TEST_CASE("MapReader walks a partial script block with mixed-type padding", "[map][scripts]") {
    std::vector<uint8_t> bytes;
    const auto u32 = [&](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v >> 24));
        bytes.push_back(static_cast<uint8_t>(v >> 16));
        bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v));
    };
    const auto str = [&](const std::string& s, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            bytes.push_back(i < s.size() ? static_cast<uint8_t>(s[i]) : uint8_t{ 0 });
        }
    };
    const auto zeros = [&](size_t n) { bytes.insert(bytes.end(), n, uint8_t{ 0 }); };

    // One on-disk script slot of the given type (pid top byte), mirroring MapReader/scriptRead exactly:
    // pid + next, type-specific fields (SPATIAL +2, TIMER +1, else +0), then the fixed 14-int trailer.
    const auto slot = [&](uint8_t typeByte, uint32_t index) {
        u32((static_cast<uint32_t>(typeByte) << 24) | index); // pid
        u32(0);                                               // next_script
        if (typeByte == 1) {                                  // SPATIAL
            u32(0x1234);                                      // built_tile
            u32(7);                                           // radius
        } else if (typeByte == 2) {                           // TIMER
            u32(0x5678);                                      // time
        }
        for (int i = 0; i < 14; ++i) {
            u32(0); // trailer
        }
    };

    // Header.
    u32(20);              // version
    str("MIXED.MAP", 16); // filename
    u32(0);               // player_default_position
    u32(0);               // player_default_elevation
    u32(0);               // player_default_orientation
    u32(0);               // num_local_vars
    u32(0xFFFFFFFFu);     // script_id (-1)
    u32(0x0Eu);           // flags: elevations 0/1/2 all marked absent -> no tile blocks
    u32(0);               // darkness
    u32(0);               // num_global_vars
    u32(0);               // map_id
    u32(0);               // timestamp
    zeros(4 * 44);        // header padding (MapReader skips 44 words)
    // No global/local vars and no tiles (every elevation is absent).

    // Scripts: 5 sections. Section 1 (spatial) carries one real script + 15 mixed-type padding slots.
    u32(0);                                                                       // section 0 (system): empty
    u32(1);                                                                       // section 1 (spatial): count
    slot(1, 100);                                                                 // the one real spatial script
    const uint8_t padding[15] = { 0, 2, 5, 1, 0, 3, 2, 4, 0, 99, 1, 2, 0, 0, 7 }; // mixed -> varying sizes
    for (uint8_t type : padding) {
        slot(type, 0xDEADu);
    }
    u32(1); // per-block check: must equal the section count (the reader validates the sum)
    u32(0); // per-block unused word
    u32(0); // section 2 (timer): empty
    u32(0); // section 3 (item): empty
    u32(0); // section 4 (critter): empty

    // Objects: total + three per-elevation counts, all zero.
    u32(0);
    u32(0);
    u32(0);
    u32(0);

    geck::test::TempFile mapFile{ "geck_map_mixed_script_padding", ".map" };
    {
        std::ofstream out(mapFile.path(), std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    MapReader reader{ [](uint32_t) -> Pro* { return nullptr; } }; // no objects -> the callback is never used
    auto map = reader.openFile(mapFile.path());
    REQUIRE(map != nullptr);
    const auto& mf = map->getMapFile();

    // The single real spatial script parsed, type-specific fields intact; padding slots aren't kept.
    REQUIRE(mf.map_scripts[1].size() == 1);
    CHECK(mf.map_scripts[1][0].pid == ((1u << 24) | 100u));
    CHECK(mf.map_scripts[1][0].timer == 0x1234u);
    CHECK(mf.map_scripts[1][0].spatial_radius == 7u);
    CHECK(mf.map_scripts[0].empty());
    CHECK(mf.map_scripts[2].empty());

    // The objects section was reached at exactly the right offset despite the mixed-size padding — a
    // fixed-stride parser would land in garbage here and throw or mis-count.
    CHECK(mf.map_objects.at(0).empty());
    CHECK(mf.map_objects.at(1).empty());
    CHECK(mf.map_objects.at(2).empty());
}
