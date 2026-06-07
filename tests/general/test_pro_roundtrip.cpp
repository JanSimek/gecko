#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "format/pro/Pro.h"
#include "reader/pro/ProReader.h"
#include "writer/pro/ProWriter.h"

namespace {

// Read an entire file into a byte vector for byte-for-byte comparisons.
std::vector<uint8_t> readAllBytes(const std::filesystem::path& path) {
    std::ifstream stream{ path.string(), std::ios::binary };
    REQUIRE(stream.is_open());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

// Build a unique temp path inside the test working directory so the existing
// tests/data copy rules are untouched and parallel runs do not collide.
std::filesystem::path makeTempProPath(const std::string& stem) {
    auto dir = std::filesystem::temp_directory_path();
    auto path = dir / (stem + ".pro");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// Level 1: generic read -> write -> read integrity using the shipped fixture.
// Proves that a real .pro parsed by ProReader, serialized by ProWriter, and
// re-parsed produces identical state AND identical on-disk bytes.
// ---------------------------------------------------------------------------
TEST_CASE("PRO generic round-trip preserves bytes and state", "[pro][roundtrip]") {
    const std::filesystem::path fixture = "data/test_item_drug_radx.pro";

    geck::ProReader reader{};
    auto original = reader.openFile(fixture);
    REQUIRE(original != nullptr);
    REQUIRE(original->type() == geck::Pro::OBJECT_TYPE::ITEM);
    REQUIRE(original->itemType() == geck::Pro::ITEM_TYPE::DRUG);

    const auto tempPath = makeTempProPath("test_pro_roundtrip_drug");

    {
        geck::ProWriter writer{};
        writer.openFile(tempPath);
        REQUIRE(writer.write(*original));
    } // writer destructor closes/flushes the stream before we read it back

    // On-disk bytes must be identical to the source fixture.
    const auto originalBytes = readAllBytes(fixture);
    const auto rewrittenBytes = readAllBytes(tempPath);
    REQUIRE(rewrittenBytes == originalBytes);

    // Re-parse the rewritten file and compare the fields that matter for this
    // object type. This guards against a writer that happens to round-trip
    // bytes by luck but a reader that reinterprets them differently.
    geck::ProReader reader2{};
    auto reparsed = reader2.openFile(tempPath);
    REQUIRE(reparsed != nullptr);

    REQUIRE(reparsed->type() == original->type());
    REQUIRE(reparsed->itemType() == original->itemType());

    REQUIRE(reparsed->header.PID == original->header.PID);
    REQUIRE(reparsed->header.message_id == original->header.message_id);
    REQUIRE(reparsed->header.FID == original->header.FID);
    REQUIRE(reparsed->header.light_distance == original->header.light_distance);
    REQUIRE(reparsed->header.light_intensity == original->header.light_intensity);
    REQUIRE(reparsed->header.flags == original->header.flags);

    REQUIRE(reparsed->commonItemData.flagsExt == original->commonItemData.flagsExt);
    REQUIRE(reparsed->commonItemData.SID == original->commonItemData.SID);
    REQUIRE(reparsed->commonItemData.materialId == original->commonItemData.materialId);
    REQUIRE(reparsed->commonItemData.containerSize == original->commonItemData.containerSize);
    REQUIRE(reparsed->commonItemData.weight == original->commonItemData.weight);
    REQUIRE(reparsed->commonItemData.basePrice == original->commonItemData.basePrice);
    REQUIRE(reparsed->commonItemData.inventoryFID == original->commonItemData.inventoryFID);
    REQUIRE(reparsed->commonItemData.soundId == original->commonItemData.soundId);

    REQUIRE(reparsed->drugData.stat0 == original->drugData.stat0);
    REQUIRE(reparsed->drugData.stat1 == original->drugData.stat1);
    REQUIRE(reparsed->drugData.stat2 == original->drugData.stat2);
    REQUIRE(reparsed->drugData.amount0 == original->drugData.amount0);
    REQUIRE(reparsed->drugData.amount1 == original->drugData.amount1);
    REQUIRE(reparsed->drugData.amount2 == original->drugData.amount2);
    REQUIRE(reparsed->drugData.duration1 == original->drugData.duration1);
    REQUIRE(reparsed->drugData.amount0_1 == original->drugData.amount0_1);
    REQUIRE(reparsed->drugData.amount1_1 == original->drugData.amount1_1);
    REQUIRE(reparsed->drugData.amount2_1 == original->drugData.amount2_1);
    REQUIRE(reparsed->drugData.duration2 == original->drugData.duration2);
    REQUIRE(reparsed->drugData.amount0_2 == original->drugData.amount0_2);
    REQUIRE(reparsed->drugData.amount1_2 == original->drugData.amount1_2);
    REQUIRE(reparsed->drugData.amount2_2 == original->drugData.amount2_2);
    REQUIRE(reparsed->drugData.addictionRate == original->drugData.addictionRate);
    REQUIRE(reparsed->drugData.addictionEffect == original->drugData.addictionEffect);
    REQUIRE(reparsed->drugData.addictionOnset == original->drugData.addictionOnset);

    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
}

// ---------------------------------------------------------------------------
// Level 2: WP-1.1 guard. ProWriter::writeWallData previously serialized hard
// zeros for the extended flags and SID fields (writeBE32(0)), silently losing
// any non-zero engine values. Wall .pro files are derived purely from the PID
// type nibble (Pro::type() == (PID & 0x0F000000) >> 24), so a faithful wall
// prototype can be constructed entirely in memory without a fixture.
//
// We set flagsExt and SID to distinctive non-zero sentinels, write, read back,
// and assert they survive. Against the old writeBE32(0) code these reads return
// 0 and the test FAILS; against the fix they round-trip and the test PASSES.
// ---------------------------------------------------------------------------
TEST_CASE("PRO wall round-trip preserves flagsExt and SID (WP-1.1)", "[pro][roundtrip][wall]") {
    const std::filesystem::path tempPath = makeTempProPath("test_pro_roundtrip_wall");

    // PID type nibble == 3 selects OBJECT_TYPE::WALL. The remaining bits are
    // an arbitrary but distinctive prototype index, preserved verbatim.
    constexpr int32_t WALL_PID = static_cast<int32_t>(
        (static_cast<uint32_t>(geck::Pro::OBJECT_TYPE::WALL) << 24) | 0x00000005u);

    constexpr uint32_t SENTINEL_FLAGS_EXT = 0xABCD1234u;
    constexpr uint32_t SENTINEL_SID = 0x42u;
    constexpr uint32_t SENTINEL_MATERIAL = 0x07u;

    geck::Pro wall{ tempPath };
    wall.header.PID = WALL_PID;
    wall.header.message_id = 1234;
    wall.header.FID = 0x04000010;
    wall.header.light_distance = 8;
    wall.header.light_intensity = 65536;
    wall.header.flags = 0x00000020; // arbitrary distinctive header flags
    wall.commonItemData.flagsExt = SENTINEL_FLAGS_EXT;
    wall.commonItemData.SID = SENTINEL_SID;
    wall.wallData.materialId = SENTINEL_MATERIAL;

    REQUIRE(wall.type() == geck::Pro::OBJECT_TYPE::WALL);

    {
        geck::ProWriter writer{};
        writer.openFile(tempPath);
        REQUIRE(writer.write(wall));
    } // flush + close before reading back

    geck::ProReader reader{};
    auto reparsed = reader.openFile(tempPath);
    REQUIRE(reparsed != nullptr);
    REQUIRE(reparsed->type() == geck::Pro::OBJECT_TYPE::WALL);

    // Header integrity.
    REQUIRE(reparsed->header.PID == WALL_PID);
    REQUIRE(reparsed->header.message_id == 1234);
    REQUIRE(reparsed->header.FID == 0x04000010);
    REQUIRE(reparsed->header.light_distance == 8);
    REQUIRE(reparsed->header.light_intensity == 65536);
    REQUIRE(reparsed->header.flags == 0x00000020);

    // The actual WP-1.1 regression assertions: these are the fields the old
    // writer zeroed out.
    REQUIRE(reparsed->commonItemData.flagsExt == SENTINEL_FLAGS_EXT);
    REQUIRE(reparsed->commonItemData.SID == SENTINEL_SID);
    REQUIRE(reparsed->wallData.materialId == SENTINEL_MATERIAL);

    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
}
