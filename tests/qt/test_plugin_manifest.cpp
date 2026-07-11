#include <catch2/catch_test_macros.hpp>

#include "ui/plugin/PluginManifest.h"

using geck::plugin::PluginManifest;

namespace {

// Parse and assert failure with a reason containing `expected`. Centralized so each invalid case is
// a single differing line (the JSON) rather than a repeated parse/CHECK block.
void expectInvalid(const char* json, const QString& expected) {
    QString error;
    const auto manifest = PluginManifest::parse(QByteArray(json), error);
    INFO("json: " << json);
    INFO("error: " << error.toStdString());
    CHECK_FALSE(manifest.has_value());
    CHECK(error.contains(expected));
}

} // namespace

TEST_CASE("PluginManifest parses a complete valid manifest", "[qt][plugins][manifest]") {
    QString error;
    const auto manifest = PluginManifest::parse(
        R"({
            "id": "hello",
            "name": "Hello World",
            "version": "1.0.0",
            "entry": "hello.luau",
            "description": "a greeting",
            "author": "Gecko"
        })",
        error);

    REQUIRE(manifest.has_value());
    CHECK(error.isEmpty());
    CHECK(manifest->id == QStringLiteral("hello"));
    CHECK(manifest->name == QStringLiteral("Hello World"));
    CHECK(manifest->version == QStringLiteral("1.0.0"));
    CHECK(manifest->entry == QStringLiteral("hello.luau"));
    CHECK(manifest->description == QStringLiteral("a greeting"));
    CHECK(manifest->author == QStringLiteral("Gecko"));
}

TEST_CASE("PluginManifest omits optional fields without error", "[qt][plugins][manifest]") {
    QString error;
    const auto manifest = PluginManifest::parse(
        R"({ "id": "min", "name": "Minimal", "version": "0.1", "entry": "main.luau" })", error);

    REQUIRE(manifest.has_value());
    CHECK(manifest->description.isEmpty());
    CHECK(manifest->author.isEmpty());
}

TEST_CASE("PluginManifest rejects malformed and invalid manifests", "[qt][plugins][manifest]") {
    expectInvalid("{ not json", QStringLiteral("invalid JSON"));
    expectInvalid("[]", QStringLiteral("must be a JSON object"));
    expectInvalid(R"({ "name": "x", "version": "1", "entry": "e.luau" })",
        QStringLiteral("missing required field \"id\""));
    expectInvalid(R"({ "id": "  ", "name": "x", "version": "1", "entry": "e.luau" })",
        QStringLiteral("must not be blank"));
    expectInvalid(R"({ "id": 5, "name": "x", "version": "1", "entry": "e.luau" })",
        QStringLiteral("must be a string"));
    expectInvalid(R"({ "id": "x", "name": "x", "version": "1", "entry": "e.luau", "author": 5 })",
        QStringLiteral("must be a string"));
    expectInvalid(R"({ "id": "a/b", "name": "x", "version": "1", "entry": "e.luau" })",
        QStringLiteral("\"id\""));
    expectInvalid(R"({ "id": "x", "name": "x", "version": "1", "entry": "/etc/passwd" })",
        QStringLiteral("\"entry\""));
    expectInvalid(R"({ "id": "x", "name": "x", "version": "1", "entry": "../secret.luau" })",
        QStringLiteral("\"entry\""));
}

TEST_CASE("PluginManifest path/id validators", "[qt][plugins][manifest]") {
    CHECK(PluginManifest::isValidId("hello.world_1-2"));
    CHECK_FALSE(PluginManifest::isValidId(""));
    CHECK_FALSE(PluginManifest::isValidId("."));
    CHECK_FALSE(PluginManifest::isValidId(".."));
    CHECK_FALSE(PluginManifest::isValidId("a/b"));
    CHECK_FALSE(PluginManifest::isValidId("a b"));

    CHECK(PluginManifest::isSafeRelativePath("sub/dir/main.luau"));
    CHECK_FALSE(PluginManifest::isSafeRelativePath(""));
    CHECK_FALSE(PluginManifest::isSafeRelativePath("/abs.luau"));
    CHECK_FALSE(PluginManifest::isSafeRelativePath("../up.luau"));
    CHECK_FALSE(PluginManifest::isSafeRelativePath("a/../../b.luau"));
    CHECK_FALSE(PluginManifest::isSafeRelativePath("C:\\win.luau"));
}
