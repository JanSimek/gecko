#include <catch2/catch_test_macros.hpp>

#include "ui/widgets/GameLocationWidget.h"

#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>

#include <filesystem>
#include <fstream>

using geck::GameLocationWidget;

namespace {

// Read-only state is not exposed on the widget's API, so reach the field by objectName - by
// position would silently follow a reordered layout onto the wrong edit.
QLineEdit* dataDirectoryEdit(GameLocationWidget& widget) {
    auto* edit = widget.findChild<QLineEdit*>("dataDirectoryEdit");
    REQUIRE(edit != nullptr);
    return edit;
}

std::filesystem::path makeBundle(const std::filesystem::path& root, const std::string& baseDirType) {
    const std::filesystem::path bundle = root / "Fallout II Community Edition.app";
    std::filesystem::create_directories(bundle / "Contents");
    std::ofstream(bundle / "Contents" / "Info.plist")
        << R"(<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
<dict>
    <key>SDL_FILESYSTEM_BASE_DIR_TYPE</key>
    <string>)"
        << baseDirType << R"(</string>
</dict>
</plist>
)";
    return bundle;
}

} // namespace

TEST_CASE("Choosing an .app fills the data directory from it and locks the field", "[qt][settings]") {
    // The engine chdir's to the bundle's base path, so for a bundle this setting cannot change
    // where the map goes. Leaving it editable invites exactly the mistake it cannot correct.
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    const std::filesystem::path root = tempDir.path().toStdString();

    GameLocationWidget widget;
    QLineEdit* dataDir = dataDirectoryEdit(widget);

    SECTION("A bundle drives the field and takes it out of the user's hands") {
        widget.setExecutableLocation(makeBundle(root, "parent"));

        CHECK(widget.getDataDirectory() == root); // "parent" -> the folder holding the .app
        CHECK(dataDir->isReadOnly());
        CHECK_FALSE(dataDir->toolTip().isEmpty()); // says why it cannot be edited
    }

    SECTION("A stored directory cannot overwrite the bundle's own") {
        // Preferences restores the executable and then the directory; the saved one is the value
        // that was being ignored all along, so it must not win.
        widget.setExecutableLocation(makeBundle(root, "parent"));
        widget.setDataDirectory("/somewhere/the/engine/never/reads");

        CHECK(widget.getDataDirectory() == root);
    }

    SECTION("Switching to a plain executable hands the field back") {
        widget.setExecutableLocation(makeBundle(root, "parent"));
        REQUIRE(dataDir->isReadOnly());

        widget.setExecutableLocation(root / "fallout2-ce");

        CHECK_FALSE(dataDir->isReadOnly());
        CHECK(widget.getDataDirectory().empty()); // the bundle's value is not left behind
        CHECK(dataDir->toolTip().isEmpty());

        widget.setDataDirectory("/games/fallout2");
        CHECK(widget.getDataDirectory() == std::filesystem::path("/games/fallout2"));
    }

    SECTION("A bundle whose plist cannot be read leaves the field editable") {
        // macOsBundleDataRoot yields nothing there, and the setting really is used - locking an
        // empty field would stop the user pointing Play anywhere at all.
        const std::filesystem::path bundle = root / "Broken.app";
        std::filesystem::create_directories(bundle / "Contents");
        std::ofstream(bundle / "Contents" / "Info.plist") << "bplist00\x01\x02";

        widget.setExecutableLocation(bundle);

        CHECK_FALSE(dataDir->isReadOnly());
        widget.setDataDirectory("/games/fallout2");
        CHECK(widget.getDataDirectory() == std::filesystem::path("/games/fallout2"));
    }
}
