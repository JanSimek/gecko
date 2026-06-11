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
    p.anchorHex = 19998;
    p.sizeHexW = 6;
    p.sizeHexH = 5;
    p.rotatable = true;

    // PIDs chosen to exceed 2^24 so the round-trip proves no precision loss.
    p.objects.push_back(PatternObject{ 0, 0, 33555201u, 16777345u, 0u, 0u });
    p.objects.push_back(PatternObject{ 2, 0, 33555201u, 16777345u, 2u, 0x10u });
    // Negative offsets must survive (objects can sit "before" the anchor).
    p.objects.push_back(PatternObject{ -3, -1, 33554435u, 16777220u, 5u, 0u });

    p.floor.push_back(PatternTile{ 0, 0, 271 });
    p.floor.push_back(PatternTile{ 1, 0, 271 });
    p.roof.push_back(PatternTile{ 0, 0, 4096 });
    return p;
}

// Wraps an extra top-level fragment (e.g. an "objects"/"floor" array) into an
// otherwise-minimal valid pattern document, so rejection cases differ only by the
// part under test.
QByteArray patternDoc(const QByteArray& extraFields) {
    QByteArray fields = R"("name": "x", "version": 1, "anchorHex": 0, "size": { "hexW": 1, "hexH": 1 })";
    if (!extraFields.isEmpty()) {
        fields += ", " + extraFields;
    }
    return "{ " + fields + " }";
}

// Asserts a document is rejected. When `needle` is given, also asserts the error
// names the offending field; otherwise just that some error was reported.
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
    CHECK(parsed->anchorHex == original.anchorHex);
    CHECK(parsed->sizeHexW == original.sizeHexW);
    CHECK(parsed->sizeHexH == original.sizeHexH);
    CHECK(parsed->rotatable == original.rotatable);

    REQUIRE(parsed->objects.size() == original.objects.size());
    for (size_t i = 0; i < original.objects.size(); ++i) {
        const auto& a = original.objects[i];
        const auto& b = parsed->objects[i];
        CHECK(b.dxHex == a.dxHex);
        CHECK(b.dyHex == a.dyHex);
        CHECK(b.proPid == a.proPid); // verbatim engine PID, no precision loss
        CHECK(b.frmPid == a.frmPid);
        CHECK(b.direction == a.direction);
        CHECK(b.flags == a.flags);
    }

    REQUIRE(parsed->floor.size() == 2);
    CHECK(parsed->floor[1].dxTile == 1);
    CHECK(parsed->floor[1].tileId == 271);
    REQUIRE(parsed->roof.size() == 1);
    CHECK(parsed->roof[0].tileId == 4096);
}

TEST_CASE("PatternSerializer parses a hand-written document", "[pattern]") {
    const QByteArray doc = R"({
        "name": "two_walls",
        "version": 1,
        "anchorHex": 12345,
        "size": { "hexW": 3, "hexH": 1 },
        "rotatable": false,
        "objects": [
            { "dxHex": 0, "dyHex": 0, "proPid": 33555201, "frmPid": 16777345, "direction": 1, "flags": 0 }
        ],
        "floor": [ { "dxTile": 0, "dyTile": 0, "tileId": 100 } ]
    })";

    QString error;
    const auto parsed = PatternSerializer::deserialize(doc, &error);
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == "two_walls");
    CHECK(parsed->anchorHex == 12345);
    CHECK(parsed->sizeHexW == 3);
    CHECK_FALSE(parsed->rotatable);
    REQUIRE(parsed->objects.size() == 1);
    CHECK(parsed->objects[0].proPid == 33555201u);
    CHECK(parsed->objects[0].direction == 1u);
    REQUIRE(parsed->floor.size() == 1);
    CHECK(parsed->roof.empty()); // omitted section defaults to empty
}

TEST_CASE("PatternSerializer rejects malformed and out-of-spec input", "[pattern]") {
    SECTION("invalid JSON") {
        checkRejected(QByteArray("{ not json"));
    }
    SECTION("top-level array is not an object") {
        checkRejected(QByteArray("[]"));
    }
    SECTION("missing version") {
        checkRejected(QByteArray(R"({ "name": "x", "anchorHex": 0 })"));
    }
    SECTION("unsupported version") {
        checkRejected(QByteArray(R"({ "version": 99, "anchorHex": 0 })"), "version");
    }
    SECTION("missing name") {
        checkRejected(QByteArray(R"({ "version": 1, "anchorHex": 0, "size": { "hexW": 1, "hexH": 1 } })"), "name");
    }
    SECTION("missing size") {
        checkRejected(QByteArray(R"({ "name": "x", "version": 1, "anchorHex": 0 })"), "size");
    }
    SECTION("object missing a required field") {
        checkRejected(patternDoc(R"("objects": [ { "dxHex": 0, "dyHex": 0, "proPid": 1 } ])"), "frmPid");
    }
    SECTION("a negative proPid would wrap when narrowed, so it is rejected") {
        checkRejected(patternDoc(R"("objects": [ { "dxHex": 0, "dyHex": 0, "proPid": -1, "frmPid": 0 } ])"), "proPid");
    }
    SECTION("a tileId above 65535 is rejected") {
        checkRejected(patternDoc(R"("floor": [ { "dxTile": 0, "dyTile": 0, "tileId": 70000 } ])"), "tileId");
    }
    SECTION("a present-but-non-numeric direction is rejected, not defaulted") {
        checkRejected(patternDoc(R"("objects": [ { "dxHex": 0, "dyHex": 0, "proPid": 1, "frmPid": 2, "direction": "north" } ])"), "direction");
    }
}
