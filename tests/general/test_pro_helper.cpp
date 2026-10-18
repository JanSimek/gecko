#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <system_error>

#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"

#include "support/DataTree.h"

namespace fs = std::filesystem;
using geck::ProHelper;
using geck::resource::GameResources;

TEST_CASE("ProHelper::basePath resolves a 1-based PID index to its LST entry", "[prohelper][pro]") {
    geck::test::DataTree tree{ "geck_prohelper_test" };
    tree.write("proto/items/items.lst", "knife.pro\nspear.pro\n");

    auto& resources = tree.mount();

    // The PID index is 1-based: index 1 maps to the first LST line.
    CHECK(ProHelper::basePath(resources, geck::ProtoId(geck::ObjectType::Item, 1).pid()) == "proto/items/knife.pro");
    CHECK(ProHelper::basePath(resources, geck::ProtoId(geck::ObjectType::Item, 2).pid()) == "proto/items/spear.pro");
}

TEST_CASE("ProHelper::basePath rejects out-of-range PID indices", "[prohelper][pro]") {
    geck::test::DataTree tree{ "geck_prohelper_test_oob" };
    tree.write("proto/items/items.lst", "knife.pro\n");

    auto& resources = tree.mount();

    // Index 0 is invalid (PIDs are 1-based) and must be rejected rather than
    // underflowing the index - 1 LST lookup into an out-of-range access.
    CHECK_THROWS(ProHelper::basePath(resources, geck::ProtoId(geck::ObjectType::Item, 0).pid()));
    // An index past the end of the LST is rejected too.
    CHECK_THROWS(ProHelper::basePath(resources, geck::ProtoId(geck::ObjectType::Item, 2).pid()));
}
