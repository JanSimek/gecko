#include <catch2/catch_test_macros.hpp>

#include "state/GameLauncher.h"

#include <string>

using namespace geck;

TEST_CASE("applyStartingMapToDdrawIni sets the starting map", "[ddraw_ini]") {
    SECTION("StartingMap is added when the key is absent in an existing [Misc] section") {
        const std::string input =
            "[Misc]\n"
            "Foo=1\n"
            "[Sound]\n"
            "Volume=5\n";

        const std::string result = applyStartingMapToDdrawIni(input, "artemple.map");

        // The new key is inserted into [Misc], before the next section starts
        REQUIRE(result.find("StartingMap=artemple.map") != std::string::npos);

        const size_t startingMapPos = result.find("StartingMap=artemple.map");
        const size_t soundSectionPos = result.find("[Sound]");
        REQUIRE(startingMapPos < soundSectionPos);

        // Unrelated content is preserved
        REQUIRE(result.find("Foo=1") != std::string::npos);
        REQUIRE(result.find("Volume=5") != std::string::npos);
    }

    SECTION("StartingMap is replaced when present with a different value") {
        const std::string input =
            "[Misc]\n"
            "StartingMap=oldmap.map\n";

        const std::string result = applyStartingMapToDdrawIni(input, "newmap.map");

        REQUIRE(result.find("StartingMap=newmap.map") != std::string::npos);
        REQUIRE(result.find("oldmap.map") == std::string::npos);
        // Only a single StartingMap entry should exist
        REQUIRE(result.find("StartingMap=", result.find("StartingMap=") + 1) == std::string::npos);
    }

    SECTION("A commented-out ;StartingMap entry is activated") {
        const std::string input =
            "[Misc]\n"
            ";StartingMap=disabled.map\n";

        const std::string result = applyStartingMapToDdrawIni(input, "enabled.map");

        REQUIRE(result.find("StartingMap=enabled.map") != std::string::npos);
        REQUIRE(result.find(";StartingMap=") == std::string::npos);
    }

    SECTION("A [Misc] section and StartingMap are created when neither exists") {
        const std::string input =
            "[Graphics]\n"
            "Mode=2\n";

        const std::string result = applyStartingMapToDdrawIni(input, "fresh.map");

        REQUIRE(result.find("[Misc]") != std::string::npos);
        REQUIRE(result.find("StartingMap=fresh.map") != std::string::npos);
        // The existing section is preserved
        REQUIRE(result.find("[Graphics]") != std::string::npos);
        REQUIRE(result.find("Mode=2") != std::string::npos);
    }

    SECTION("Unrelated lines outside [Misc] are preserved verbatim") {
        const std::string input =
            "[Graphics]\n"
            "GraphicsMode=0\n"
            "[Misc]\n"
            "StartingMap=old.map\n"
            "ScrollDist=10\n";

        const std::string result = applyStartingMapToDdrawIni(input, "target.map");

        REQUIRE(result.find("[Graphics]") != std::string::npos);
        REQUIRE(result.find("GraphicsMode=0") != std::string::npos);
        REQUIRE(result.find("ScrollDist=10") != std::string::npos);
        REQUIRE(result.find("StartingMap=target.map") != std::string::npos);
        REQUIRE(result.find("old.map") == std::string::npos);
    }
}
