#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "editor/helper/ObjectQueries.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "writer/pro/ProWriter.h"

namespace fs = std::filesystem;
using namespace geck;
using geck::resource::GameResources;

namespace {

constexpr uint32_t WALL_TYPE = static_cast<uint32_t>(Pro::OBJECT_TYPE::WALL);

uint32_t wallPid(uint32_t index) {
    return (WALL_TYPE << 24) | index;
}

void writeLine(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

// Writes a minimal valid WALL .pro carrying the given header flags. object_query
// only reads header.flags, but the file must still parse as a real PRO.
void writeWallPro(const fs::path& path, uint32_t index, uint32_t flags) {
    fs::create_directories(path.parent_path());

    Pro pro{ path }; // constructor zero-initialises the header and all data blocks
    pro.header.PID = static_cast<int32_t>(wallPid(index));
    pro.header.flags = flags;

    ProWriter writer;
    writer.openFile(path);
    REQUIRE(writer.write(pro));
}

MapObject wallObject(uint32_t index) {
    MapObject object;
    object.pro_pid = wallPid(index);
    return object;
}

// Unique-per-call suffix so concurrently-constructed fixtures never collide.
int nextFixtureId() {
    static std::atomic counter{ 0 };
    return counter++;
}

// A temp data tree with a proto/walls list + three crafted wall PROs, mounted
// into a GameResources so object_query resolves them through the resource stack.
struct ProFixture {
    fs::path root;
    GameResources resources;

    // walls.lst line indices (1-based), each pointing at a wall PRO crafted below.
    static constexpr uint32_t BLOCKING = 1;      // PRO has no NoBlock flag    -> blocks
    static constexpr uint32_t NON_BLOCKING = 2;  // PRO has the NoBlock flag    -> passable
    static constexpr uint32_t SHOOT_THROUGH = 3; // PRO has the ShootThrough flag

    // Build the tree under the build-tree scratch dir (not the world-writable
    // system temp dir), with a unique suffix so fixtures never collide.
    ProFixture()
        : root(fs::path(GECK_TEST_TMP_DIR) / ("object_query_" + std::to_string(nextFixtureId()))) {
        std::error_code ec;
        fs::remove_all(root, ec);

        writeLine(root / "proto/walls/walls.lst", "blocking.pro\nnoblock.pro\nshoot.pro\n");
        writeWallPro(root / "proto/walls/blocking.pro", BLOCKING, 0x00000000);
        writeWallPro(root / "proto/walls/noblock.pro", NON_BLOCKING, 0x00000010); // NoBlock
        writeWallPro(root / "proto/walls/shoot.pro", SHOOT_THROUGH, 0x80000000);  // ShootThrough

        resources.files().addDataPath(root.string());
    }

    ~ProFixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    ProFixture(const ProFixture&) = delete;
    ProFixture& operator=(const ProFixture&) = delete;
};

} // namespace

TEST_CASE("object_query::blocksMovement reads the PRO NoBlock flag", "[object_query]") {
    ProFixture fx;

    // No NoBlock flag -> the object blocks movement (and is a wall blocker).
    const auto blocking = wallObject(ProFixture::BLOCKING);
    CHECK(object_query::blocksMovement(blocking, fx.resources));
    CHECK(object_query::isWallBlocker(blocking, fx.resources));

    // NoBlock flag set -> passable.
    const auto passable = wallObject(ProFixture::NON_BLOCKING);
    CHECK_FALSE(object_query::blocksMovement(passable, fx.resources));
    CHECK_FALSE(object_query::isWallBlocker(passable, fx.resources));
}

TEST_CASE("object_query::isShootThroughWallBlocker reads the PRO ShootThrough flag", "[object_query]") {
    ProFixture fx;

    const auto shootThrough = wallObject(ProFixture::SHOOT_THROUGH);
    CHECK(object_query::isShootThroughWallBlocker(shootThrough, fx.resources));
    // ShootThrough does not imply NoBlock, so it still blocks movement.
    CHECK(object_query::blocksMovement(shootThrough, fx.resources));

    const auto plain = wallObject(ProFixture::BLOCKING);
    CHECK_FALSE(object_query::isShootThroughWallBlocker(plain, fx.resources));
}

TEST_CASE("object_query treats an unresolvable PRO as non-blocking", "[object_query]") {
    ProFixture fx;

    // Index 99 is past the end of walls.lst, so the PRO cannot be resolved.
    const auto unknown = wallObject(99);
    CHECK_FALSE(object_query::blocksMovement(unknown, fx.resources));
    CHECK_FALSE(object_query::isShootThroughWallBlocker(unknown, fx.resources));
}
