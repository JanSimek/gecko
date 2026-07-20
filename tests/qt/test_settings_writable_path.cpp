#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
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

    SECTION("a marker spelled differently from its list entry is not cleared by mistake") {
        geck::Settings settings;
        settings.setDataPaths({ dirA, dirB });
        // Same directory, non-canonical spelling: equivalence, not exact equality, must decide.
        settings.setWritableDataPath(dirA / ".");
        CHECK_FALSE(settings.getWritableDataPath().empty());
        REQUIRE(settings.resolveWritableDataPath().has_value());

        settings.setDataPaths({ dirA, dirB }); // re-set must keep the equivalent marker listed
        CHECK_FALSE(settings.getWritableDataPath().empty());
    }

    SECTION("loading a file without the key resets an in-memory marker") {
        {
            geck::Settings settings;
            settings.setDataPaths({ dirA, dirB });
            settings.save(); // no marker in the file
        }

        geck::Settings stale;
        stale.setDataPaths({ dirA, dirB });
        stale.setWritableDataPath(dirA);
        stale.load(); // absent key means unset — the in-memory marker must not survive
        CHECK(stale.getWritableDataPath().empty());
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

TEST_CASE("Settings script-source markers: subset invariant and persistence", "[settings][ssl]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const fs::path dirA = fs::path(temp.path().toStdString()) / "a";
    const fs::path dirB = fs::path(temp.path().toStdString()) / "b";
    fs::create_directories(dirA);
    fs::create_directories(dirB);

    removeSettingsFile();

    SECTION("markers round-trip through save() and load(), de-duplicated") {
        {
            geck::Settings settings;
            settings.setDataPaths({ dirA, dirB });
            settings.setScriptSourcePaths({ dirB, dirB, dirA }); // duplicate collapses
            settings.save();
        }

        geck::Settings reloaded;
        reloaded.load();
        const auto sources = reloaded.getScriptSourcePaths();
        REQUIRE(sources.size() == 2);
        CHECK(std::find(sources.begin(), sources.end(), dirA) != sources.end());
        CHECK(std::find(sources.begin(), sources.end(), dirB) != sources.end());
    }

    SECTION("loading drops a marker that is not among the data paths (e.g. a hand-edited file)") {
        {
            geck::Settings settings;
            settings.setDataPaths({ dirA });               // only dirA is a data path...
            settings.setScriptSourcePaths({ dirA, dirB }); // ...but dirB is (wrongly) marked too
            settings.save();
        }

        geck::Settings reloaded;
        reloaded.load(); // prune against the file's data paths -> dirB is dropped
        const auto sources = reloaded.getScriptSourcePaths();
        CHECK(sources.size() == 1);
        CHECK(sources.front() == dirA);
    }

    SECTION("an empty marker set is not written and loads empty") {
        {
            geck::Settings settings;
            settings.setDataPaths({ dirA, dirB });
            settings.save();
        }
        geck::Settings reloaded;
        reloaded.load();
        CHECK(reloaded.getScriptSourcePaths().empty());
    }

    removeSettingsFile();
}
