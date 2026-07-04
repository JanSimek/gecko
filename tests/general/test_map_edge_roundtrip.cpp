#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <vector>

#include "editor/HexagonGrid.h"
#include "format/map/MapEdge.h"
#include "reader/ReaderExceptions.h"
#include "reader/map/MapEdgeReader.h"
#include "rendering/MapEdgeOverlayGeometry.h"
#include "writer/map/MapEdgeWriter.h"

#include "support/ByteWriter.h"
#include "support/Fixtures.h"
#include "support/TempFile.h"

using namespace geck;
using geck::test::ByteWriter;
using geck::test::readAllBytes;
using geck::test::TempFile;

namespace {

// Serialize an edge to a scratch .edg and return its raw bytes. The writer is scoped so its
// ofstream closes (flushing) before we read the file back.
std::vector<uint8_t> writeToBytes(const MapEdge& edge) {
    TempFile tmp{ "map_edge_write", ".edg" };
    {
        MapEdgeWriter writer;
        writer.openFile(tmp.path());
        REQUIRE(writer.write(edge));
    }
    return readAllBytes(tmp.path());
}

// Parse bytes through the reader's in-memory (VFS) path.
MapEdge parseBytes(const std::vector<uint8_t>& bytes) {
    MapEdgeReader reader;
    auto edge = reader.openFile("scratch.edg", bytes);
    REQUIRE(edge != nullptr);
    return *edge;
}

// write -> read equality check via the model's defaulted operator==.
MapEdge roundTrip(const MapEdge& edge) {
    return parseBytes(writeToBytes(edge));
}

MapEdge::Rect rect(int32_t l, int32_t t, int32_t r, int32_t b) {
    return MapEdge::Rect{ l, t, r, b };
}

// Parses a shipped .edg fixture and asserts writing it back reproduces the original bytes.
void checkRealFileRoundTrips(const std::string& name, int expectedZones) {
    const auto original = readAllBytes(geck::test::dataPath(name));

    MapEdgeReader reader;
    auto edge = reader.openFile(geck::test::dataPath(name));
    REQUIRE(edge != nullptr);
    CHECK(edge->version == 1);
    CHECK(edge->totalZones() == expectedZones);

    CHECK(writeToBytes(*edge) == original);
}

} // namespace

TEST_CASE("MapEdge round-trips a v1 file with several zones on one elevation", "[map_edge]") {
    MapEdge edge;
    edge.version = 1;
    edge.elevations[0].zones = { rect(10, 20, 30, 40), rect(1, 2, 3, 4), rect(-5, -6, -7, -8) };

    const MapEdge parsed = roundTrip(edge);

    CHECK(parsed == edge);
    CHECK(parsed.version == 1);
    CHECK(parsed.elevations[0].zones.size() == 3);
    CHECK(parsed.elevations[1].zones.empty());
    CHECK(parsed.elevations[2].zones.empty());
}

TEST_CASE("MapEdge round-trips zones split across elevations, skipping an empty one", "[map_edge]") {
    // Elevation 1 has no zones — the writer's level indicator must jump 0 -> 2 and the
    // reader must skip elevation 1 without consuming a phantom zone.
    MapEdge edge;
    edge.version = 1;
    edge.elevations[0].zones = { rect(1, 1, 1, 1) };
    edge.elevations[2].zones = { rect(2, 2, 2, 2), rect(3, 3, 3, 3) };

    const MapEdge parsed = roundTrip(edge);

    CHECK(parsed == edge);
    CHECK(parsed.elevations[0].zones.size() == 1);
    CHECK(parsed.elevations[1].zones.empty());
    CHECK(parsed.elevations[2].zones.size() == 2);
}

TEST_CASE("MapEdge omits the terminal indicator when the last zone is on the last elevation", "[map_edge]") {
    // One zone per elevation, ending on the last elevation — the structure of the shipped
    // 68-byte files. The final zone must NOT be followed by a level indicator.
    MapEdge edge;
    edge.version = 1;
    edge.elevations[0].zones = { rect(10, 20, 30, 40) };
    edge.elevations[1].zones = { rect(11, 21, 31, 41) };
    edge.elevations[2].zones = { rect(12, 22, 32, 42) };

    ByteWriter expected;
    expected.be32(MapEdge::MAGIC)
        .be32(1)
        .be32(0)
        .be32(10)
        .be32(20)
        .be32(30)
        .be32(40)
        .be32(1) // elevation 0 zone -> next zone on elevation 1
        .be32(11)
        .be32(21)
        .be32(31)
        .be32(41)
        .be32(2) // elevation 1 zone -> next zone on elevation 2
        .be32(12)
        .be32(22)
        .be32(32)
        .be32(42); // elevation 2 zone: no trailing indicator (end of stream)

    CHECK(expected.data().size() == 68); // matches the shipped 3-zone layout
    CHECK(writeToBytes(edge) == expected.data());
    CHECK(parseBytes(expected.data()) == edge);
}

TEST_CASE("MapEdge round-trips shipped Restoration Project .edg files byte-for-byte", "[map_edge]") {
    checkRealFileRoundTrips("vault13.edg", 3); // 3 zones, one per elevation (68 bytes)
    checkRealFileRoundTrips("newr2.edg", 4);   // 4 zones, elevation 1 has two (88 bytes)
}

TEST_CASE("Map-edge overlay geometry maps real zones to sane world rects", "[map_edge]") {
    MapEdgeReader reader;
    auto edge = reader.openFile(geck::test::dataPath("vault13.edg"));
    REQUIRE(edge != nullptr);

    const HexagonGrid grid;

    // Every zone in every elevation has in-range hex corners, so each yields a non-degenerate
    // world-space rectangle (the overlay would draw an outline, not collapse to a point/line).
    int checked = 0;
    for (const auto& elevation : edge->elevations) {
        for (const auto& zone : elevation.zones) {
            const auto box = mapEdgeZoneWorldBounds(grid, zone);
            REQUIRE(box.has_value());
            CHECK(box->size.x > 0.f);
            CHECK(box->size.y > 0.f);
            ++checked;
        }
    }
    CHECK(checked == 3);

    // An off-grid corner yields no box rather than an out-of-bounds hex lookup.
    MapEdge::Rect offGrid{ HexagonGrid::POSITION_COUNT, 0, 1, 2 };
    CHECK_FALSE(mapEdgeZoneWorldBounds(grid, offGrid).has_value());
}

TEST_CASE("MapEdge round-trips zones on every elevation", "[map_edge]") {
    MapEdge edge;
    edge.version = 1;
    edge.elevations[0].zones = { rect(0, 0, 10, 10) };
    edge.elevations[1].zones = { rect(11, 11, 22, 22), rect(33, 33, 44, 44) };
    edge.elevations[2].zones = { rect(55, 55, 66, 66) };

    CHECK(roundTrip(edge) == edge);
}

TEST_CASE("MapEdge round-trips a v2 file with square rect and clip sides", "[map_edge]") {
    MapEdge edge;
    edge.version = 2;
    edge.elevations[0].squareRect = rect(50, 1, 2, 60);
    edge.elevations[0].clipSides = { /*bottom*/ true, /*right*/ false, /*top*/ false, /*left*/ true };
    edge.elevations[0].zones = { rect(5, 6, 7, 8) };
    // Elevations 1 and 2 keep their default square rect ({99,0,0,99}) and no clip sides —
    // the v2 block is written for every elevation regardless of zone count.
    edge.elevations[1].clipSides = { false, true, true, false };

    const MapEdge parsed = roundTrip(edge);

    CHECK(parsed == edge);
    CHECK(parsed.version == 2);
    CHECK(parsed.elevations[0].squareRect == rect(50, 1, 2, 60));
    CHECK(parsed.elevations[0].clipSides.bottom);
    CHECK(parsed.elevations[0].clipSides.left);
    CHECK_FALSE(parsed.elevations[0].clipSides.right);
    CHECK(parsed.elevations[1].clipSides.right);
    CHECK(parsed.elevations[1].clipSides.top);
    CHECK(parsed.elevations[1].squareRect == rect(99, 0, 0, 99)); // untouched default
}

TEST_CASE("MapEdge v2 preserves every clip-side bit independently", "[map_edge]") {
    // Each side maps to a distinct byte of the packed int32; verify no cross-talk.
    for (int mask = 0; mask < 16; ++mask) {
        MapEdge edge;
        edge.version = 2;
        edge.elevations[0].clipSides = {
            (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0, (mask & 8) != 0
        };
        edge.elevations[0].zones = { rect(1, 2, 3, 4) };

        const MapEdge parsed = roundTrip(edge);
        CHECK(parsed.elevations[0].clipSides == edge.elevations[0].clipSides);
    }
}

TEST_CASE("MapEdge v1 writer emits the exact engine byte layout", "[map_edge]") {
    MapEdge edge;
    edge.version = 1;
    edge.elevations[0].zones = { rect(10, 20, 30, 40), rect(1, 2, 3, 4) };

    ByteWriter expected;
    expected.be32(MapEdge::MAGIC) // 'EDGE'
        .be32(1)                  // version
        .be32(0)                  // reserved
        // zone 0 + level indicator (elevation 0 again — another zone follows)
        .be32(10)
        .be32(20)
        .be32(30)
        .be32(40)
        .be32(0)
        // zone 1 + level indicator (no more zones anywhere -> ELEVATION_COUNT)
        .be32(1)
        .be32(2)
        .be32(3)
        .be32(4)
        .be32(MapEdge::ELEVATION_COUNT);

    CHECK(writeToBytes(edge) == expected.data());
    CHECK(parseBytes(expected.data()) == edge);
}

TEST_CASE("MapEdge v2 writer emits the exact engine byte layout", "[map_edge]") {
    MapEdge edge;
    edge.version = 2;
    edge.elevations[0].squareRect = rect(50, 1, 2, 60);
    edge.elevations[0].clipSides = { true, false, false, true }; // bottom | left
    edge.elevations[0].zones = { rect(5, 6, 7, 8) };

    const uint32_t clip0 = 1u | (1u << 24); // bottom (bit0) | left (bit24)

    ByteWriter expected;
    expected.be32(MapEdge::MAGIC)
        .be32(2) // version
        .be32(0) // reserved
        // elevation 0: square rect + clip, then one zone + level indicator
        .be32(50)
        .be32(1)
        .be32(2)
        .be32(60)
        .be32(clip0)
        .be32(5)
        .be32(6)
        .be32(7)
        .be32(8)
        .be32(MapEdge::ELEVATION_COUNT)
        // elevation 1: default square rect, no clip, no zones
        .be32(99)
        .be32(0)
        .be32(0)
        .be32(99)
        .be32(0)
        // elevation 2: default square rect, no clip, no zones
        .be32(99)
        .be32(0)
        .be32(0)
        .be32(99)
        .be32(0);

    CHECK(writeToBytes(edge) == expected.data());
    CHECK(parseBytes(expected.data()) == edge);
}

TEST_CASE("MapEdgeReader rejects malformed headers", "[map_edge]") {
    auto parseThrows = [](const std::vector<uint8_t>& bytes) {
        // The filesystem path preserves ParseException (the VFS overload rewraps as IOException).
        TempFile tmp{ "map_edge_bad", ".edg" };
        {
            std::ofstream out{ tmp.path(), std::ios::binary };
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        MapEdgeReader reader;
        reader.openFile(tmp.path());
    };

    SECTION("bad magic") {
        ByteWriter bytes;
        bytes.be32(0x12345678).be32(1).be32(0).be32(1).be32(2).be32(3).be32(4).be32(MapEdge::ELEVATION_COUNT);
        CHECK_THROWS_AS(parseThrows(bytes.data()), ParseException);
    }

    SECTION("unsupported version") {
        ByteWriter bytes;
        bytes.be32(MapEdge::MAGIC).be32(3).be32(0).be32(1).be32(2).be32(3).be32(4).be32(MapEdge::ELEVATION_COUNT);
        CHECK_THROWS_AS(parseThrows(bytes.data()), ParseException);
    }

    SECTION("nonzero reserved") {
        ByteWriter bytes;
        bytes.be32(MapEdge::MAGIC).be32(1).be32(7).be32(1).be32(2).be32(3).be32(4).be32(MapEdge::ELEVATION_COUNT);
        CHECK_THROWS_AS(parseThrows(bytes.data()), ParseException);
    }
}

TEST_CASE("MapEdge empty()/totalZones() report zone presence", "[map_edge]") {
    MapEdge edge;
    CHECK(edge.empty());
    CHECK(edge.totalZones() == 0);

    edge.elevations[1].zones = { rect(1, 2, 3, 4) };
    CHECK_FALSE(edge.empty());
    CHECK(edge.totalZones() == 1);
}

TEST_CASE("MapEdgeReader::siblingPath derives the .EDG name from a map path", "[map_edge]") {
    CHECK(MapEdgeReader::siblingPath("ARROYO.MAP") == std::filesystem::path{ "ARROYO.EDG" });
    CHECK(MapEdgeReader::siblingPath("arroyo.map") == std::filesystem::path{ "arroyo.EDG" });
    CHECK(MapEdgeReader::siblingPath("/data/maps/klamath.map")
        == std::filesystem::path{ "/data/maps/klamath.EDG" });
}

TEST_CASE("MapEdgeReader::tryParse is lenient — nullopt on bad or empty input", "[map_edge]") {
    MapEdge edge;
    edge.version = 1;
    edge.elevations[0].zones = { rect(1, 2, 3, 4) };

    // A well-formed non-empty edge parses.
    const auto parsed = MapEdgeReader::tryParse("ok.edg", writeToBytes(edge));
    REQUIRE(parsed.has_value());
    CHECK(*parsed == edge);

    // Garbage returns nullopt instead of throwing.
    CHECK_FALSE(MapEdgeReader::tryParse("bad.edg", { 0x00, 0x01, 0x02, 0x03 }).has_value());
    CHECK_FALSE(MapEdgeReader::tryParse("empty.edg", {}).has_value());
}
