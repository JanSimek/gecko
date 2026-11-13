#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <ostream>
#include <string>

#include "cli/MapExamples.h"
#include "resource/GameResources.h"

#include "support/CliJson.h"
#include "support/DataTree.h"
#include "support/MapFileIo.h"
#include "support/ShopMap.h"

using nlohmann::json;
using namespace geck;
namespace fs = std::filesystem;

// find_examples on a data directory holding test::ShopMap: a scripted shopkeeper with a wall beside
// it and a second critter four hexes east. Critters and walls read back without protos, so no game
// data is needed; proto names stay empty, so the filters exercised here are kind, script, scriptCalls,
// near and maps.
namespace {

constexpr int SHOPKEEPER_HEX = test::ShopMap::SHOPKEEPER_HEX;

json run(resource::GameResources& resources, const cli::ExamplesOptions& options, int* rc = nullptr) {
    return test::runJson([&](std::ostream& out) { return cli::findExamples(resources, options, out); }, rc);
}

} // namespace

TEST_CASE("find_examples reports matching objects with their script and surroundings", "[find_examples][cli]") {
    test::DataTree tree{ "map_examples_data" };
    const fs::path& root = tree.root();
    test::writeMapFile(test::ShopMap::build(), root / "maps" / "shop.map");
    // ShopMap::SHOPKEEPER_PROGRAM is 1: line 2, shopkpr.int.
    tree.write("scripts/scripts.lst", "first.int ; First\nshopkpr.int ; Shopkeeper\n");
    tree.write("scripts_src/shopkpr.ssl", "procedure talk_p_proc begin\n   gdialog_mod_barter(0);\nend\n");

    auto& resources = tree.mount();

    SECTION("every critter, nearest neighbours first") {
        cli::ExamplesOptions options;
        options.kind = "critter";
        const json result = run(resources, options);
        CHECK(result["total"] == 2);
        CHECK(result["byMap"][0]["map"] == "shop.map");
        CHECK(result["byMap"][0]["count"] == 2);
        const json& shopkeeper = result["matches"][0];
        CHECK(shopkeeper["hex"] == SHOPKEEPER_HEX);
        CHECK(shopkeeper["script"]["name"] == "shopkpr");
        REQUIRE(shopkeeper["neighbours"].size() == 1); // the wall; the other critter is 4 hexes away
        CHECK(shopkeeper["neighbours"][0]["kind"] == "wall");
        CHECK(shopkeeper["neighbours"][0]["nearest"] == 1);
    }

    SECTION("a script filter matches the script's description; a wider radius reaches the other critter") {
        cli::ExamplesOptions options;
        options.script = "SHOPKEEPER";
        options.contextRadius = 4;
        const json result = run(resources, options);
        REQUIRE(result["total"] == 1);
        const json& neighbours = result["matches"][0]["neighbours"];
        REQUIRE(neighbours.size() == 2);
        CHECK(neighbours[1]["kind"] == "critter");
        CHECK(neighbours[1]["nearest"] == 4);
    }

    SECTION("scriptCalls keeps the objects whose script calls the opcode") {
        cli::ExamplesOptions options;
        options.scriptCalls = "gdialog_mod_barter";
        const json result = run(resources, options);
        REQUIRE(result["total"] == 1);
        CHECK(result["matches"][0]["hex"] == SHOPKEEPER_HEX);

        options.scriptCalls = "gdialog_mod_free";
        CHECK(run(resources, options)["total"] == 0);
    }

    SECTION("near, maps and limit narrow the matches") {
        cli::ExamplesOptions options;
        options.kind = "critter";
        options.nearName = "no such proto";
        CHECK(run(resources, options)["total"] == 0);

        options.nearName.clear();
        options.maps = { "elsewhere" };
        CHECK(run(resources, options)["total"] == 0);
        options.maps = { "SHOP" };
        CHECK(run(resources, options)["total"] == 2);

        options.limit = 1;
        const json limited = run(resources, options);
        CHECK(limited["returned"] == 1);
        CHECK(limited["truncated"] == true);
    }

    SECTION("a query needs at least one filter") {
        int rc = 0;
        CHECK(run(resources, cli::ExamplesOptions{}, &rc).is_null());
        CHECK(rc != 0);
    }
}
