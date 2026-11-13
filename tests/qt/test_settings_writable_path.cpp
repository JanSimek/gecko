#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

#include "support/TempFile.h"
#include "ui/Settings.h"

namespace fs = std::filesystem;

TEST_CASE("Settings keeps its save-location marker listed and persists it", "[settings]") {
    const geck::test::TempDir temp("settings-paths");
    const fs::path dirA = temp.path() / "a";
    const fs::path dirB = temp.path() / "b";
    fs::create_directories(dirA);
    fs::create_directories(dirB);

    SECTION("the marker never dangles: it clears when its folder leaves the data paths") {
        geck::Settings settings;
        settings.setDataPaths({ dirA, dirB });
        settings.setWritableDataPath(dirA);

        settings.setDataPaths({ dirB });
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
}

TEST_CASE("Settings keeps its script-source markers among the data paths and persists them", "[settings]") {
    const geck::test::TempDir temp("settings-paths");
    const fs::path dirA = temp.path() / "a";
    const fs::path dirB = temp.path() / "b";
    fs::create_directories(dirA);
    fs::create_directories(dirB);

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
        REQUIRE(sources.size() == 1);
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
}
