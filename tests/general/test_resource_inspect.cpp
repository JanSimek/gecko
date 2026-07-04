#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>

#include "cli/ResourceInspect.h"
#include "resource/GameResources.h"
#include "support/Fixtures.h"

using json = nlohmann::json;
using namespace geck;

namespace {
// f2_res.dat is the committed DAT fixture; art/intrface/hr_alltlk.frm is a known entry
// (see test_reading_dat). GameResources is non-copyable, so mount into a caller-owned instance.
void mountFixtureDat(resource::GameResources& resources) {
    resources.files().addDataPath(geck::test::dataPath("f2_res.dat"));
}
} // namespace

TEST_CASE("resourceFind locates a DAT entry and names its source", "[resource]") {
    resource::GameResources resources;
    mountFixtureDat(resources);
    std::ostringstream oss;
    CHECK(cli::resourceFind(resources, "art/intrface/hr_alltlk.frm", oss) == 0);

    const json j = json::parse(oss.str());
    CHECK(j["found"] == true);
    REQUIRE(j["source"].is_object());
    CHECK(j["source"]["kind"] == "dat");
    CHECK(j["source"]["path"].get<std::string>().find("f2_res.dat") != std::string::npos);
}

TEST_CASE("resourceFind reports an absent path as found=false, not an error", "[resource]") {
    resource::GameResources resources;
    mountFixtureDat(resources);
    std::ostringstream oss;
    CHECK(cli::resourceFind(resources, "art/tiles/nope12345.frm", oss) == 0);

    const json j = json::parse(oss.str());
    CHECK(j["found"] == false);
    CHECK(j["source"].is_null());
}

TEST_CASE("resourceList matches case-insensitively and tags each entry's source", "[resource]") {
    resource::GameResources resources;
    mountFixtureDat(resources);
    std::ostringstream oss;
    // Upper-case glob against a lower-case entry exercises the case-insensitive match.
    CHECK(cli::resourceList(resources, "*HR_ALLTLK*", oss) == 0);

    const json j = json::parse(oss.str());
    CHECK(j["count"].get<int>() >= 1);
    CHECK(j["truncated"] == false);

    bool found = false;
    for (const auto& entry : j["entries"]) {
        if (entry["path"].get<std::string>().find("hr_alltlk.frm") != std::string::npos) {
            found = true;
            CHECK(entry["source"]["kind"] == "dat");
        }
    }
    CHECK(found);
}

TEST_CASE("resourceMissing reports an unreadable map as an error", "[resource]") {
    resource::GameResources resources;
    mountFixtureDat(resources);
    std::ostringstream oss;
    CHECK(cli::resourceMissing(resources, "maps/does_not_exist.map", oss) == 1);

    const json j = json::parse(oss.str());
    CHECK(j.contains("error"));
    CHECK(j["map"] == "maps/does_not_exist.map");
}
