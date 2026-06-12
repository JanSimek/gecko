#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

#include "format/frm/Frm.h"
#include "format/lst/Lst.h"
#include "reader/ReaderExceptions.h"
#include "resource/GameResources.h"
#include "resource/ResourceRepository.h"

namespace fs = std::filesystem;
using namespace geck;
using geck::resource::GameResources;

namespace {

void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << contents;
}

// A GameResources mounting one small LST fixture, with its ResourceRepository.
struct RepoFixture {
    fs::path root;
    GameResources resources;

    RepoFixture() {
        root = fs::temp_directory_path() / "geck_resourcerepo_test";
        fs::remove_all(root);
        writeFile(root / "art/tiles/tiles.lst", "floor.frm\ngrass.frm\n");
        resources.files().addDataPath(root.string());
    }
    ~RepoFixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    resource::ResourceRepository& repo() { return resources.repository(); }
};

constexpr const char* LST_PATH = "art/tiles/tiles.lst";

} // namespace

TEST_CASE("ResourceRepository caches and reuses loaded resources", "[resource]") {
    RepoFixture fx;
    auto& repo = fx.repo();

    CHECK(repo.find<Lst>(LST_PATH) == nullptr); // miss before load

    Lst* first = repo.load<Lst>(LST_PATH);
    REQUIRE(first != nullptr);
    REQUIRE(first->list().size() == 2);
    CHECK(first->list()[0] == "floor.frm");

    // Hit: a second load returns the SAME cached object, not a fresh parse.
    Lst* second = repo.load<Lst>(LST_PATH);
    CHECK(second == first);
    CHECK(repo.find<Lst>(LST_PATH) == first);
}

TEST_CASE("ResourceRepository rejects a type-mismatched lookup", "[resource]") {
    RepoFixture fx;
    auto& repo = fx.repo();
    REQUIRE(repo.load<Lst>(LST_PATH) != nullptr);

    // Cached as an Lst; requesting a Frm on the same key must throw rather than
    // silently miss (or hand back a bad cast).
    CHECK_THROWS(repo.find<Frm>(LST_PATH));
}

TEST_CASE("ResourceRepository clear() drops cached resources", "[resource]") {
    RepoFixture fx;
    auto& repo = fx.repo();
    REQUIRE(repo.load<Lst>(LST_PATH) != nullptr);
    REQUIRE(repo.find<Lst>(LST_PATH) != nullptr);

    repo.clear();
    CHECK(repo.find<Lst>(LST_PATH) == nullptr);
    CHECK(repo.load<Lst>(LST_PATH) != nullptr); // reload works after clear
}

TEST_CASE("ResourceRepository load throws on a missing file", "[resource]") {
    RepoFixture fx;
    CHECK_THROWS_AS(fx.repo().load<Lst>("art/tiles/nonexistent.lst"), geck::IOException);
}
