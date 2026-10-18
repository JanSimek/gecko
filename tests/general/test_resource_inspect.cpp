#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>

#include "cli/ResourceInspect.h"
#include "resource/GameResources.h"
#include "support/CliJson.h"
#include "support/Fixtures.h"

using json = nlohmann::json;
using namespace geck;

// Each case mounts f2_res.dat, the committed DAT fixture, of which test::HR_ALLTLK is a known entry.

TEST_CASE("resourceFind locates a DAT entry and names its source", "[resource]") {
    resource::GameResources resources;
    resources.files().addDataPath(test::f2ResDat());
    const json j = test::runJson([&](std::ostream& out) { return cli::resourceFind(resources, std::string(test::HR_ALLTLK), out); });
    CHECK(j["found"] == true);
    REQUIRE(j["source"].is_object());
    CHECK(j["source"]["kind"] == "dat");
    CHECK(j["source"]["path"].get<std::string>().find("f2_res.dat") != std::string::npos);
}

TEST_CASE("resourceFind reports an absent path as found=false, not an error", "[resource]") {
    resource::GameResources resources;
    resources.files().addDataPath(test::f2ResDat());
    const json j = test::runJson([&](std::ostream& out) { return cli::resourceFind(resources, "art/tiles/nope12345.frm", out); });
    CHECK(j["found"] == false);
    CHECK(j["source"].is_null());
}

TEST_CASE("resourceList matches case-insensitively and tags each entry's source", "[resource]") {
    resource::GameResources resources;
    resources.files().addDataPath(test::f2ResDat());
    // Upper-case glob against a lower-case entry exercises the case-insensitive match.
    const json j = test::runJson([&](std::ostream& out) { return cli::resourceList(resources, "*HR_ALLTLK*", out); });
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
    resources.files().addDataPath(test::f2ResDat());
    std::ostringstream oss;
    CHECK(cli::resourceMissing(resources, "maps/does_not_exist.map", oss) == 1);

    const json j = json::parse(oss.str());
    CHECK(j.contains("error"));
    CHECK(j["map"] == "maps/does_not_exist.map");
}
