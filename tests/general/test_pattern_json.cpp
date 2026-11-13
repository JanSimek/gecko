#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include "cli/PatternJson.h"
#include "format/frm/FrmId.h"
#include "format/pro/ProtoTypes.h"
#include "pattern/Pattern.h"

#include "support/TempFile.h"

using namespace geck;

namespace {
constexpr uint32_t wallPid(uint32_t index) { return ProtoId(ObjectType::Wall, index).pid(); }
constexpr uint32_t wallFid(uint32_t frameId) { return FrmId(ObjectType::Wall, frameId).fid(); }
} // namespace

// extract_pattern writes a stamp with cli::serializePattern; generate reads it back with
// cli::loadPattern for api:placeStamp. This proves that headless round-trip (write -> read) keeps
// every field, so a stamp the agent extracts is the stamp generate places. (The editor side of the
// round-trip is covered by the qt PatternSerializer test; the actual stamping needs game art and is
// validated manually.)
TEST_CASE("loadPattern reads back what serializePattern writes", "[pattern_json][cli][roundtrip]") {
    pattern::Pattern source;
    source.name = "tent";
    pattern::PatternVariant variant;
    variant.label = "default";
    variant.anchorHex = 18901;
    variant.objects.push_back(pattern::PatternObject{ 3, -1, wallPid(621), wallFid(966), 2U, 0U });
    variant.objects.push_back(pattern::PatternObject{ 0, 0, wallPid(622), wallFid(967), 0U, 8U });
    variant.floor.push_back(pattern::PatternTile{ 1, 0, 191 });
    source.variants.push_back(variant);

    const test::TempFile file{ "stamp_roundtrip", ".json" };
    const std::filesystem::path& path = file.path();
    test::writeFile(path, cli::serializePattern(source));

    std::string error;
    const std::optional<pattern::Pattern> loaded = cli::loadPattern(path.string(), &error);
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == "tent");
    REQUIRE(loaded->variants.size() == 1);
    const pattern::PatternVariant& result = loaded->variants.front();
    CHECK(result.anchorHex == 18901);
    REQUIRE(result.objects.size() == 2);
    CHECK(result.objects[0].dxHex == 3);
    CHECK(result.objects[0].dyHex == -1);
    CHECK(result.objects[0].proPid == wallPid(621));
    CHECK(result.objects[0].direction == 2U);
    CHECK(result.objects[1].flags == 8U);
    REQUIRE(result.floor.size() == 1);
    CHECK(result.floor[0].tileId == 191);
}

TEST_CASE("loadPattern fails cleanly on a missing or malformed file", "[pattern_json][cli]") {
    std::string error;
    CHECK_FALSE(cli::loadPattern("/no/such/stamp.json", &error).has_value());
    CHECK_FALSE(error.empty());
}

// A stamp the MCP/scripts feed to generate must have its required fields — a missing proPid is
// rejected as an error, not silently defaulted to PID 0 (which would place a wrong/empty object).
TEST_CASE("loadPattern rejects a stamp object missing a required field", "[pattern_json][cli]") {
    const test::TempFile file{ "stamp_malformed", ".json" };
    const std::filesystem::path& path = file.path();
    // object has dxHex/dyHex/frmPid but no proPid.
    test::writeFile(path, R"({"name":"bad","version":1,"variants":[{"anchorHex":0,)"
                          R"("objects":[{"dxHex":0,"dyHex":0,"frmPid":123}],"floor":[],"roof":[]}]})");

    std::string error;
    const auto loaded = cli::loadPattern(path.string(), &error);
    CHECK_FALSE(loaded.has_value());
    INFO("error: " << error);
    CHECK(error.find("proPid") != std::string::npos);
}
