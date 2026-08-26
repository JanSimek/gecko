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
const QLineEdit* dataDirectoryEdit(const GameLocationWidget& widget) {
    const QLineEdit* edit = widget.findChild<QLineEdit*>("dataDirectoryEdit");
    REQUIRE(edit != nullptr);
    return edit;
}

std::filesystem::path makeBundle(const std::filesystem::path& root, const std::string& baseDirType) {
    const std::filesystem::path bundle = root / "Fallout II Community Edition.app";
    std::filesystem::create_directories(bundle / "Contents" / "MacOS");
    std::ofstream(bundle / "Contents" / "MacOS" / "fallout2-ce") << "binary";
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
    const QLineEdit* dataDir = dataDirectoryEdit(widget);

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
        // The row explains itself again; releasing it must not leave it with no tooltip at all.
        CHECK(dataDir->toolTip().contains("contains data/"));

        widget.setDataDirectory("/games/fallout2");
        CHECK(widget.getDataDirectory() == std::filesystem::path("/games/fallout2"));
    }

    SECTION("A bundle whose plist cannot be read leaves the field editable") {
        // macOsBundleDataRoot yields nothing there, and the setting really is used - locking an
        // empty field would stop the user pointing Play anywhere at all.
        const std::filesystem::path bundle = root / "Broken.app";
        std::filesystem::create_directories(bundle / "Contents");
        std::ofstream(bundle / "Contents" / "Info.plist") << "bplist00" << '\x01' << '\x02';

        widget.setExecutableLocation(bundle);

        CHECK_FALSE(dataDir->isReadOnly());
        widget.setDataDirectory("/games/fallout2");
        CHECK(widget.getDataDirectory() == std::filesystem::path("/games/fallout2"));
    }
}

TEST_CASE("An .app is validated as an executable, not as an installation folder", "[qt][settings]") {
    // A bundle is a directory, so it used to fall to the installation check, which looks for a
    // binary sitting directly in the folder - a bundle keeps its own in Contents/MacOS, so every
    // one of them was reported as "may not be a valid Fallout 2 installation".
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    const std::filesystem::path root = tempDir.path().toStdString();

    GameLocationWidget widget;
    QString lastStatus;
    QString lastStyle;
    QObject::connect(&widget, &GameLocationWidget::statusChanged,
        [&lastStatus, &lastStyle](const QString& message, const QString& styleClass) {
            lastStatus = message;
            lastStyle = styleClass;
        });

    SECTION("A real bundle validates as an executable") {
        const auto bundle = makeBundle(root, "parent");
        std::filesystem::create_directories(root / "data"); // "parent" root, as a real install has

        widget.setExecutableLocation(bundle);
        widget.validateSelection();

        CHECK(lastStyle.toStdString() == "success");
        CHECK_FALSE(lastStatus.contains("may not be a valid Fallout 2 installation"));
    }

    SECTION("A bundle with no binary is called out as such") {
        const std::filesystem::path empty = root / "Empty.app";
        std::filesystem::create_directories(empty / "Contents");

        widget.setExecutableLocation(empty);
        widget.validateSelection();

        CHECK(lastStyle.toStdString() == "warning");
        CHECK(lastStatus.contains("no executable"));
    }

    SECTION("A plain directory is still checked as an installation") {
        widget.setExecutableLocation(root);
        widget.validateSelection();

        CHECK(lastStyle.toStdString() == "warning");
        CHECK(lastStatus.contains("installation"));
    }
}

TEST_CASE("Choosing an executable notifies once, not once per field it touches", "[qt][settings]") {
    // Deriving the data directory writes to that field, and its own textChanged would raise a
    // second configurationChanged nested inside the first - the dialog then re-entered its refresh
    // while still handling the executable change.
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    const std::filesystem::path root = tempDir.path().toStdString();

    GameLocationWidget widget;
    int notifications = 0;
    QObject::connect(&widget, &GameLocationWidget::configurationChanged, [&notifications] { ++notifications; });

    // cppcheck cannot follow a signal to its connected lambda, so it reads `notifications` as never
    // incremented and calls every comparison below constant. The suppressions say that, rather than
    // weakening the assertions - counting exactly one notification is the whole point of this case.

    SECTION("selecting a bundle") {
        widget.setExecutableLocation(makeBundle(root, "parent"));
        // cppcheck-suppress knownConditionTrueFalse
        CHECK(notifications == 1);
    }

    SECTION("releasing the field again") {
        widget.setExecutableLocation(makeBundle(root, "parent"));
        notifications = 0;

        widget.setExecutableLocation(root / "fallout2-ce"); // clears the derived value
        // cppcheck-suppress knownConditionTrueFalse
        CHECK(notifications == 1);
    }

    SECTION("a user editing the data directory still notifies") {
        widget.setDataDirectory("/games/fallout2");
        // cppcheck-suppress knownConditionTrueFalse
        CHECK(notifications == 1); // the field is only silenced while we drive it
    }
}
