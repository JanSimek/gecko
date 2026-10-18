#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "support/TempFile.h"
#include "ui/core/EditorWidget.h"

namespace test = geck::test;

TEST_CASE("EditorWidget::canSaveInPlace accepts only real on-disk files, not VFS paths", "[editor_widget]") {
    // A map opened from the game data carries a vfspp path that looks absolute but is not a writable
    // filesystem location — saving it in place would try to create "/maps" at the root (the reported bug).
    CHECK_FALSE(geck::EditorWidget::canSaveInPlace("/maps/arbridge.map"));
    // A brand-new / unsaved map has no real file yet.
    CHECK_FALSE(geck::EditorWidget::canSaveInPlace("arbridge.map"));
    CHECK_FALSE(geck::EditorWidget::canSaveInPlace(std::filesystem::path{}));

    // A real, existing file can be saved straight back.
    const test::TempDir dir("save-in-place");
    const std::filesystem::path real = dir.path() / "saved.map";
    test::writeFile(real, "map");
    CHECK(geck::EditorWidget::canSaveInPlace(real));
}
