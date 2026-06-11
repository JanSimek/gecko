#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QString>

#include "pattern/Pattern.h"
#include "pattern/PatternSerializer.h"

using namespace geck::pattern;

namespace {

Pattern makeSamplePattern() {
    Pattern p;
    p.name = "small_tent";
    p.version = 1;

    PatternVariant west;
    west.label = "entrance west";
    west.anchorHex = 19998;
    // PIDs chosen to exceed 2^24 so the round-trip proves no precision loss.
    west.objects.push_back(PatternObject{ 0, 0, 33555201u, 16777345u, 0u, 0u });
    west.objects.push_back(PatternObject{ 2, 0, 33555201u, 16777345u, 2u, 0x10u });
    // Negative offsets must survive (objects can sit "before" the anchor).
    west.objects.push_back(PatternObject{ -3, -1, 33554435u, 16777220u, 5u, 0u });
    west.floor.push_back(PatternTile{ 0, 0, 271 });
    west.floor.push_back(PatternTile{ 1, 0, 271 });
    west.roof.push_back(PatternTile{ 0, 0, 2048 });
    p.variants.push_back(west);

    PatternVariant south;
    south.label = "entrance south";
    south.anchorHex = 20050;
    south.objects.push_back(PatternObject{ 0, 0, 33555202u, 16777346u, 3u, 0u });
    p.variants.push_back(south);

    return p;
}

// Wraps an extra top-level fragment into an otherwise-minimal valid pattern document,
// so rejection cases differ only by the part under test.
QByteArray patternDoc(const QByteArray& extraFields) {
    QByteArray fields = R"("name": "x", "version": 1)";
    if (!extraFields.isEmpty()) {
        fields += ", " + extraFields;
    }
    return "{ " + fields + " }";
}

// A minimal valid single-variant document, with an extra fragment merged into that
// variant for the per-variant rejection cases.
QByteArray variantDoc(const QByteArray& extraVariantFields) {
    QByteArray variant = R"("anchorHex": 0)";
    if (!extraVariantFields.isEmpty()) {
        variant += ", " + extraVariantFields;
    }
    return patternDoc("\"variants\": [ { " + variant + " } ]");
}

// Asserts a document is rejected. When `needle` is given, also asserts the error names
// the offending field; otherwise just that some error was reported.
void checkRejected(const QByteArray& doc, const char* needle = nullptr) {
    QString error;
    const auto parsed = PatternSerializer::deserialize(doc, &error);
    INFO("error: " << error.toStdString());
    CHECK_FALSE(parsed.has_value());
    if (needle != nullptr) {
        CHECK(error.contains(QLatin1String(needle)));
    } else {
        CHECK_FALSE(error.isEmpty());
    }
}

} // namespace

TEST_CASE("PatternSerializer round-trips a pattern verbatim", "[pattern]") {
    const Pattern original = makeSamplePattern();

    const QByteArray json = PatternSerializer::serialize(original);
    REQUIRE_FALSE(json.isEmpty());

    QString error;
    const auto parsed = PatternSerializer::deserialize(json, &error);
    INFO("error: " << error.toStdString()); // shown if the REQUIRE below fails
    REQUIRE(parsed.has_value());

    CHECK(parsed->name == original.name);
    CHECK(parsed->version == original.version);
    REQUIRE(parsed->variants.size() == original.variants.size());

    const auto& west = parsed->variants[0];
    CHECK(west.label == "entrance west");
    CHECK(west.anchorHex == 19998);
    REQUIRE(west.objects.size() == 3);
    for (size_t i = 0; i < original.variants[0].objects.size(); ++i) {
        const auto& a = original.variants[0].objects[i];
        const auto& b = west.objects[i];
        CHECK(b.dxHex == a.dxHex);
        CHECK(b.dyHex == a.dyHex);
        CHECK(b.proPid == a.proPid); // verbatim engine PID, no precision loss
        CHECK(b.frmPid == a.frmPid);
        CHECK(b.direction == a.direction);
        CHECK(b.flags == a.flags);
    }
    REQUIRE(west.floor.size() == 2);
    CHECK(west.floor[1].dxTile == 1);
    CHECK(west.floor[1].tileId == 271);
    REQUIRE(west.roof.size() == 1);
    CHECK(west.roof[0].tileId == 2048);

    const auto& south = parsed->variants[1];
    CHECK(south.label == "entrance south");
    CHECK(south.anchorHex == 20050);
    REQUIRE(south.objects.size() == 1);
    CHECK(south.objects[0].direction == 3u);
    CHECK(south.roof.empty()); // omitted section defaults to empty
}

TEST_CASE("PatternSerializer parses a hand-written document", "[pattern]") {
    const QByteArray doc = R"({
        "name": "two_walls",
        "version": 1,
        "variants": [
            { "label": "default", "anchorHex": 12345,
              "objects": [ { "dxHex": 0, "dyHex": 0, "proPid": 33555201, "frmPid": 16777345, "direction": 1 } ],
              "floor": [ { "dxTile": 0, "dyTile": 0, "tileId": 100 } ] }
        ]
    })";

    QString error;
    const auto parsed = PatternSerializer::deserialize(doc, &error);
    INFO("error: " << error.toStdString());
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == "two_walls");
    REQUIRE(parsed->variants.size() == 1);
    const auto& v = parsed->variants[0];
    CHECK(v.anchorHex == 12345);
    CHECK(v.label == "default");
    REQUIRE(v.objects.size() == 1);
    CHECK(v.objects[0].proPid == 33555201u);
    CHECK(v.objects[0].direction == 1u);
    REQUIRE(v.floor.size() == 1);
    CHECK(v.roof.empty()); // omitted section defaults to empty
}

TEST_CASE("PatternSerializer rejects malformed and out-of-spec input", "[pattern]") {
    SECTION("invalid JSON") {
        checkRejected(QByteArray("{ not json"));
    }
    SECTION("top-level array is not an object") {
        checkRejected(QByteArray("[]"));
    }
    SECTION("missing version") {
        checkRejected(QByteArray(R"({ "name": "x", "variants": [] })"));
    }
    SECTION("unsupported version") {
        checkRejected(QByteArray(R"({ "version": 99, "name": "x" })"), "version");
    }
    SECTION("missing name") {
        checkRejected(QByteArray(R"({ "version": 1, "variants": [] })"), "name");
    }
    SECTION("missing variants") {
        checkRejected(patternDoc(""), "variants");
    }
    SECTION("empty variants array") {
        checkRejected(patternDoc(R"("variants": [])"), "variant");
    }
    SECTION("variant missing anchorHex") {
        checkRejected(patternDoc(R"("variants": [ { "objects": [] } ])"), "anchorHex");
    }
    SECTION("object missing a required field") {
        checkRejected(variantDoc(R"("objects": [ { "dxHex": 0, "dyHex": 0, "proPid": 1 } ])"), "frmPid");
    }
    SECTION("a negative proPid would wrap when narrowed, so it is rejected") {
        checkRejected(variantDoc(R"("objects": [ { "dxHex": 0, "dyHex": 0, "proPid": -1, "frmPid": 0 } ])"), "proPid");
    }
    SECTION("a tileId above the engine's 12-bit limit is rejected") {
        checkRejected(variantDoc(R"("floor": [ { "dxTile": 0, "dyTile": 0, "tileId": 4096 } ])"), "tileId");
    }
    SECTION("a present-but-non-numeric direction is rejected, not defaulted") {
        checkRejected(variantDoc(R"("objects": [ { "dxHex": 0, "dyHex": 0, "proPid": 1, "frmPid": 2, "direction": "north" } ])"), "direction");
    }
    SECTION("a direction outside 0..5 is rejected") {
        checkRejected(variantDoc(R"("objects": [ { "dxHex": 0, "dyHex": 0, "proPid": 1, "frmPid": 2, "direction": 6 } ])"), "direction");
    }
}
