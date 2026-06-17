#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QString>

#include "cli/PatternJson.h"
#include "pattern/Pattern.h"
#include "pattern/PatternSerializer.h"

using namespace geck;

// The headless extractor (gecko_cli) writes stamps with cli::serializePattern, hand-rolled to avoid
// Qt. This proves the editor's PatternSerializer — the real reader, behind the pattern library —
// loads that output and every field survives, so a stamp captured by gecko-mcp/gecko-cli can be used
// in the editor unchanged.
TEST_CASE("headless-serialized pattern round-trips through the editor's PatternSerializer", "[pattern]") {
    pattern::Pattern source;
    source.name = "desert_tent";
    pattern::PatternVariant variant;
    variant.label = "default";
    variant.anchorHex = 18098;
    variant.objects.push_back(pattern::PatternObject{ 1, -2, 50332269U, 50332614U, 3U, 0U });
    variant.objects.push_back(pattern::PatternObject{ 0, 0, 50332270U, 50332615U, 0U, 8U });
    variant.floor.push_back(pattern::PatternTile{ 2, -1, 191 });
    variant.roof.push_back(pattern::PatternTile{ 0, 0, 200 });
    source.variants.push_back(variant);

    const std::string json = cli::serializePattern(source);

    QString error;
    const std::optional<pattern::Pattern> loaded = pattern::PatternSerializer::deserialize(QByteArray::fromStdString(json), &error);

    REQUIRE(loaded.has_value()); // the editor accepts the headless JSON
    CHECK(loaded->name == "desert_tent");
    CHECK(loaded->version == pattern::Pattern::CURRENT_VERSION);
    REQUIRE(loaded->variants.size() == 1);

    const pattern::PatternVariant& result = loaded->variants.front();
    CHECK(result.anchorHex == 18098);
    REQUIRE(result.objects.size() == 2);
    CHECK(result.objects[0].dxHex == 1);
    CHECK(result.objects[0].dyHex == -2);
    CHECK(result.objects[0].proPid == 50332269U);
    CHECK(result.objects[0].frmPid == 50332614U);
    CHECK(result.objects[0].direction == 3U);
    CHECK(result.objects[1].flags == 8U);
    REQUIRE(result.floor.size() == 1);
    CHECK(result.floor[0].dxTile == 2);
    CHECK(result.floor[0].tileId == 191);
    REQUIRE(result.roof.size() == 1);
    CHECK(result.roof[0].tileId == 200);
}
