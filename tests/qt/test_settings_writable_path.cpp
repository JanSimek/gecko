#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <filesystem>

#include "ui/Settings.h"

namespace fs = std::filesystem;

namespace {

// Settings persists to the (test-mode) ConfigLocation; scrub it so this test neither inherits
// another test's file nor leaves one behind.
void removeSettingsFile() {
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QFile::remove(QDir(configPath).filePath("gecko/settings.json"));
    QFile::remove(QDir(configPath).filePath("settings.json"));
}

} // namespace

TEST_CASE("Settings save-location marker: resolution, invariant, persistence", "[settings][writableroot]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const fs::path dirA = fs::path(temp.path().toStdString()) / "a";
    const fs::path dirB = fs::path(temp.path().toStdString()) / "b";
    fs::create_directories(dirA);
    fs::create_directories(dirB);

    removeSettingsFile();

    SECTION("marker overrides the positional fallback; clearing restores it") {
        geck::Settings settings;
        settings.setDataPaths({ dirA, dirB });
        REQUIRE(settings.resolveWritableDataPath().has_value());
        CHECK(*settings.resolveWritableDataPath() == dirB); // positional rule: last directory wins

        settings.setWritableDataPath(dirA);
        CHECK(settings.getWritableDataPath() == dirA);
        REQUIRE(settings.resolveWritableDataPath().has_value());
        CHECK(*settings.resolveWritableDataPath() == dirA);

        settings.setWritableDataPath({});
        REQUIRE(settings.resolveWritableDataPath().has_value());
        CHECK(*settings.resolveWritableDataPath() == dirB);
    }

    SECTION("the marker never dangles: it clears when its folder leaves the data paths") {
        geck::Settings settings;
        settings.setDataPaths({ dirA, dirB });
        settings.setWritableDataPath(dirA);

        settings.removeDataPath(dirA);
        CHECK(settings.getWritableDataPath().empty());
        REQUIRE(settings.resolveWritableDataPath().has_value());
        CHECK(*settings.resolveWritableDataPath() == dirB);

        settings.setDataPaths({ dirA, dirB });
        settings.setWritableDataPath(dirB);
        settings.setDataPaths({ dirA }); // wholesale replacement drops the marked folder too
        CHECK(settings.getWritableDataPath().empty());
    }

    SECTION("marker round-trips through save() and load()") {
        {
            geck::Settings settings;
            settings.setDataPaths({ dirA, dirB });
            settings.setWritableDataPath(dirA);
            settings.save();
        }

        geck::Settings reloaded;
        reloaded.load();
        CHECK(reloaded.getWritableDataPath() == dirA);
        REQUIRE(reloaded.resolveWritableDataPath().has_value());
        CHECK(*reloaded.resolveWritableDataPath() == dirA);
    }

    SECTION("an unset marker is not written and loads as unset") {
        {
            geck::Settings settings;
            settings.setDataPaths({ dirA, dirB });
            settings.save();
        }

        geck::Settings reloaded;
        reloaded.load();
        CHECK(reloaded.getWritableDataPath().empty());
        REQUIRE(reloaded.resolveWritableDataPath().has_value());
        CHECK(*reloaded.resolveWritableDataPath() == dirB);
    }

    removeSettingsFile();
}
