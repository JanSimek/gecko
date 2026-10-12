#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "ssl/ScriptFacts.h"

using namespace geck::ssl;

// Opt-in, needs a script source tree: GECK_SSL_CORPUS=/path/to/Fallout2_Restoration_Project/scripts_src
// general_tests "[.corpus]". Every script must preprocess without an error, as sslc compiles them all.
TEST_CASE("Every script in a source tree preprocesses cleanly", "[.corpus][ssl]") {
    const char* root = std::getenv("GECK_SSL_CORPUS");
    if (root == nullptr) {
        SKIP("GECK_SSL_CORPUS is not set");
    }
    const DiskSourceProvider provider;
    int scripts = 0;
    int setAiPacket = 0;
    int setTeam = 0;
    int withLocalVarNames = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() != ".ssl") {
            continue;
        }
        ++scripts;
        const auto facts = inspectScript(provider, entry.path());
        INFO(entry.path().string());
        for (const auto& problem : facts.problems) {
            UNSCOPED_INFO(problem);
        }
        CHECK(facts.problems.empty());
        setAiPacket += facts.assigns(ScriptFacts::Override::Field::AiPacket) ? 1 : 0;
        setTeam += facts.assigns(ScriptFacts::Override::Field::Team) ? 1 : 0;
        withLocalVarNames += facts.localVarNames.empty() ? 0 : 1;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::cout << scripts << " scripts in " << elapsed.count() << " ms; " << setAiPacket << " set their own AI packet, " << setTeam << " their team, "
              << withLocalVarNames << " name local variables\n";
    CHECK(scripts > 0);
}
