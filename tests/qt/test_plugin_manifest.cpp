#include <catch2/catch_test_macros.hpp>

#include "ui/plugin/PluginManifest.h"

using geck::plugin::PluginManifest;

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
    QString error;

    SECTION("malformed JSON") {
        CHECK_FALSE(PluginManifest::parse("{ not json", error).has_value());
        CHECK(error.contains("invalid JSON"));
    }
    SECTION("non-object root") {
        CHECK_FALSE(PluginManifest::parse("[]", error).has_value());
        CHECK(error.contains("must be a JSON object"));
    }
    SECTION("missing required field") {
        CHECK_FALSE(PluginManifest::parse(
            R"({ "name": "x", "version": "1", "entry": "e.luau" })", error)
                .has_value());
        CHECK(error.contains("missing required field \"id\""));
    }
    SECTION("blank required field") {
        CHECK_FALSE(PluginManifest::parse(
            R"({ "id": "  ", "name": "x", "version": "1", "entry": "e.luau" })", error)
                .has_value());
        CHECK(error.contains("must not be blank"));
    }
    SECTION("wrong type for a required field") {
        CHECK_FALSE(PluginManifest::parse(
            R"({ "id": 5, "name": "x", "version": "1", "entry": "e.luau" })", error)
                .has_value());
        CHECK(error.contains("must be a string"));
    }
    SECTION("wrong type for an optional field") {
        CHECK_FALSE(PluginManifest::parse(
            R"({ "id": "x", "name": "x", "version": "1", "entry": "e.luau", "author": 5 })", error)
                .has_value());
        CHECK(error.contains("must be a string"));
    }
    SECTION("unsafe id with a path separator") {
        CHECK_FALSE(PluginManifest::parse(
            R"({ "id": "a/b", "name": "x", "version": "1", "entry": "e.luau" })", error)
                .has_value());
        CHECK(error.contains("\"id\""));
    }
    SECTION("absolute entry path") {
        CHECK_FALSE(PluginManifest::parse(
            R"({ "id": "x", "name": "x", "version": "1", "entry": "/etc/passwd" })", error)
                .has_value());
        CHECK(error.contains("\"entry\""));
    }
    SECTION("entry escaping the plugin dir") {
        CHECK_FALSE(PluginManifest::parse(
            R"({ "id": "x", "name": "x", "version": "1", "entry": "../secret.luau" })", error)
                .has_value());
        CHECK(error.contains("\"entry\""));
    }
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
