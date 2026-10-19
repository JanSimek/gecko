#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTableWidget>

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "support/MacBundle.h"
#include "support/QtFind.h"
#include "support/TempFile.h"
#include "ui/Settings.h"
#include "ui/widgets/DataPathsWidget.h"
#include "ui/widgets/GameLocationWidget.h"

namespace test = geck::test;
using geck::GameLocationWidget;

namespace {

// Read-only state is not exposed on the widget's API, so reach the field by objectName - by
// position would silently follow a reordered layout onto the wrong edit.
const QLineEdit* dataDirectoryEdit(const GameLocationWidget& widget) {
    const QLineEdit* edit = widget.findChild<QLineEdit*>("dataDirectoryEdit");
    REQUIRE(edit != nullptr);
    return edit;
}

} // namespace

TEST_CASE("Choosing an .app fills the data directory from it and locks the field", "[preferences][macos]") {
    // The engine chdir's to the bundle's base path, so for a bundle this setting cannot change
    // where the map goes. Leaving it editable invites exactly the mistake it cannot correct.
    const geck::test::TempDir tempDir("game-location");
    const std::filesystem::path& root = tempDir.path();

    GameLocationWidget widget;
    const QLineEdit* dataDir = dataDirectoryEdit(widget);

    SECTION("A bundle drives the field and takes it out of the user's hands") {
        widget.setExecutableLocation(geck::test::makeAppBundle(root, geck::test::bundlePlist("parent")));

        CHECK(widget.getDataDirectory() == root); // "parent" -> the folder holding the .app
        CHECK(dataDir->isReadOnly());
        CHECK_FALSE(dataDir->toolTip().isEmpty()); // says why it cannot be edited
    }

    SECTION("A stored directory cannot overwrite the bundle's own") {
        // Preferences restores the executable and then the directory; the saved one is the value
        // that was being ignored all along, so it must not win.
        widget.setExecutableLocation(geck::test::makeAppBundle(root, geck::test::bundlePlist("parent")));
        widget.setDataDirectory("/somewhere/the/engine/never/reads");

        CHECK(widget.getDataDirectory() == root);
    }

    SECTION("Switching to a plain executable hands the field back") {
        widget.setExecutableLocation(geck::test::makeAppBundle(root, geck::test::bundlePlist("parent")));
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
        // A real binary plist: the XML reader chokes on it, which is the point. Byte values, not
        // escapes - the bounded \x{..} form the analyser asks for is C++23.
        constexpr char kBinaryPlist[] = { 'b', 'p', 'l', 'i', 's', 't', '0', '0', 0x01, 0x02 };
        geck::test::writeFile(bundle / "Contents" / "Info.plist", std::string_view(kBinaryPlist, sizeof(kBinaryPlist)));

        widget.setExecutableLocation(bundle);

        CHECK_FALSE(dataDir->isReadOnly());
        widget.setDataDirectory("/games/fallout2");
        CHECK(widget.getDataDirectory() == std::filesystem::path("/games/fallout2"));
    }
}

TEST_CASE("An .app is validated as an executable, and a plain folder is not one", "[preferences][macos]") {
    // A bundle is a directory that is nonetheless an executable choice: its binary is in
    // Contents/MacOS. Any other directory cannot be launched.
    const geck::test::TempDir tempDir("game-location");
    const std::filesystem::path& root = tempDir.path();

    GameLocationWidget widget;
    QSignalSpy statusSpy(&widget, &GameLocationWidget::statusChanged); // (message, styleClass)
    REQUIRE(statusSpy.isValid());
    const auto lastStatus = [&statusSpy]() {
        REQUIRE_FALSE(statusSpy.isEmpty());
        return statusSpy.last().at(0).toString();
    };
    const auto lastStyle = [&statusSpy]() {
        REQUIRE_FALSE(statusSpy.isEmpty());
        return statusSpy.last().at(1).toString();
    };

    SECTION("A real bundle validates as an executable") {
        const auto bundle = geck::test::makeAppBundle(root, geck::test::bundlePlist("parent"));
        std::filesystem::create_directories(root / "data"); // "parent" root, as a real install has

        widget.setExecutableLocation(bundle);
        widget.validateSelection();

        CHECK(lastStyle().toStdString() == "success");
        CHECK_FALSE(lastStatus().contains("may not be a valid Fallout 2 installation"));
    }

    SECTION("A bundle with no binary is called out as such") {
        const std::filesystem::path empty = root / "Empty.app";
        std::filesystem::create_directories(empty / "Contents");

        widget.setExecutableLocation(empty);
        widget.validateSelection();

        CHECK(lastStyle().toStdString() == "warning");
        CHECK(lastStatus().contains("no executable"));
    }

    SECTION("A plain folder is rejected as an executable") {
        std::filesystem::create_directories(root / "data");
        geck::test::writeFile(root / "fallout2.exe", "");

        widget.setExecutableLocation(root);
        widget.validateSelection();

        CHECK(lastStyle().toStdString() == "warning");
        CHECK(lastStatus().contains("not a folder"));
    }
}

TEST_CASE("Choosing an executable notifies once, not once per field it touches", "[preferences]") {
    // Deriving the data directory writes to that field, and its own textChanged would raise a
    // second configurationChanged nested inside the first - the dialog then re-entered its refresh
    // while still handling the executable change.
    const geck::test::TempDir tempDir("game-location");
    const std::filesystem::path& root = tempDir.path();

    GameLocationWidget widget;
    QSignalSpy notifications(&widget, &GameLocationWidget::configurationChanged);
    REQUIRE(notifications.isValid());

    SECTION("selecting a bundle") {
        widget.setExecutableLocation(geck::test::makeAppBundle(root, geck::test::bundlePlist("parent")));
        CHECK(notifications.count() == 1);
    }

    SECTION("releasing the field again") {
        widget.setExecutableLocation(geck::test::makeAppBundle(root, geck::test::bundlePlist("parent")));
        notifications.clear();

        widget.setExecutableLocation(root / "fallout2-ce"); // clears the derived value
        CHECK(notifications.count() == 1);
    }

    SECTION("a user editing the data directory still notifies") {
        widget.setDataDirectory("/games/fallout2");
        CHECK(notifications.count() == 1); // the field is only silenced while we drive it
    }
}

TEST_CASE("DataPathsWidget shows highest priority on top and preserves stored order", "[data_paths_widget]") {
    auto settings = std::make_shared<geck::Settings>();
    geck::DataPathsWidget widget(settings);

    // Stored order is lowest-priority-first: the loader mounts in this order and the last-mounted
    // source wins. (.dat paths normalise to themselves, so the round-trip is exact.)
    const std::vector<std::filesystem::path> stored{ "/data/base.dat", "/data/patch.dat", "/data/mymod.dat" };
    widget.setDataPaths(stored);

    // Round-trip: the stored order is preserved exactly, so existing configs load identically.
    CHECK(widget.getDataPaths() == stored);

    // The table displays it reversed, highest priority first: the stored-last source is the top row.
    auto* table = widget.findChild<QTableWidget*>();
    REQUIRE(table != nullptr);
    REQUIRE(table->rowCount() == 3);
    CHECK(test::requireItem(*table, 0, 1).text().toStdString() == "/data/mymod.dat"); // top row = highest priority
    CHECK(test::requireItem(*table, 2, 1).text().toStdString() == "/data/base.dat");  // bottom row = lowest
    // Priority counts from 1 at the top. The cell is right-aligned with the marker arrow leading,
    // so the number is what the text ends with.
    CHECK(test::requireItem(*table, 0, 0).text().endsWith("1"));
    CHECK(test::requireItem(*table, 2, 0).text().endsWith("3"));

    // Every row can be reordered: priority is the user's call. The built-in resources folder used
    // to block this - a row directly above it had Move Down greyed out, which stranded any path
    // added after it, since new paths land on top.
    table->setCurrentCell(0, 1);
    QAbstractButton* moveDown = test::findButton(widget, "Move Down");
    REQUIRE(moveDown != nullptr);
    CHECK(moveDown->isEnabled());
    moveDown->click();
    CHECK(test::requireItem(*table, 1, 1).text().toStdString() == "/data/mymod.dat"); // moved one step down
    CHECK(test::requireItem(*table, 0, 1).text().toStdString() == "/data/patch.dat");
}
