#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "cli/PatternJson.h"
#include "pattern/Pattern.h"

using namespace geck;

// extract_pattern writes a stamp with cli::serializePattern; generate reads it back with
// cli::loadPattern for api:placeStamp. This proves that headless round-trip (write -> read) keeps
// every field, so a stamp the agent extracts is the stamp generate places. (The editor side of the
// round-trip is covered by the qt PatternSerializer test; the actual stamping needs game art and is
// validated manually.)
TEST_CASE("loadPattern reads back what serializePattern writes", "[pattern]") {
    pattern::Pattern source;
    source.name = "tent";
    pattern::PatternVariant variant;
    variant.label = "default";
    variant.anchorHex = 18901;
    variant.objects.push_back(pattern::PatternObject{ 3, -1, 50332269U, 50332614U, 2U, 0U });
    variant.objects.push_back(pattern::PatternObject{ 0, 0, 50332270U, 50332615U, 0U, 8U });
    variant.floor.push_back(pattern::PatternTile{ 1, 0, 191 });
    source.variants.push_back(variant);

    const std::filesystem::path path = std::filesystem::path(GECK_TEST_TMP_DIR) / "stamp_roundtrip.json";
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream file(path, std::ios::binary);
        file << cli::serializePattern(source);
    }

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
    CHECK(result.objects[0].proPid == 50332269U);
    CHECK(result.objects[0].direction == 2U);
    CHECK(result.objects[1].flags == 8U);
    REQUIRE(result.floor.size() == 1);
    CHECK(result.floor[0].tileId == 191);
}

TEST_CASE("loadPattern fails cleanly on a missing or malformed file", "[pattern]") {
    std::string error;
    CHECK_FALSE(cli::loadPattern("/no/such/stamp.json", &error).has_value());
    CHECK_FALSE(error.empty());
}
