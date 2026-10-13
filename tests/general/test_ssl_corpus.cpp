#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <vector>

#include "cli/IntDisassembler.h"
#include "ssl/ScriptFacts.h"
#include "support/GraphicsTestEnv.h"

using namespace geck::ssl;

namespace {

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// The procedures a compiled script's procedure table holds, lowercased name -> argument count,
// without the placeholder entry ("..............") sslc puts first.
std::map<std::string, int, std::less<>> compiledProcedures(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::map<std::string, int, std::less<>> procedures;
    for (const auto& procedure : geck::cli::disassembleInt(bytes).procedures) {
        if (!procedure.name.starts_with('.')) {
            procedures.try_emplace(lower(procedure.name), procedure.argCount);
        }
    }
    return procedures;
}

// The procedures in a script's .int that the parser did not find with the same argument count.
std::vector<std::string> missingFromParse(const Program& program, const std::filesystem::path& intFile) {
    std::map<std::string, int, std::less<>> parsed;
    for (const Procedure& procedure : program.procedures) {
        parsed.insert_or_assign(lower(procedure.name), static_cast<int>(procedure.parameters.size()));
    }
    std::vector<std::string> missing;
    for (const auto& [name, argCount] : compiledProcedures(intFile)) {
        if (const auto it = parsed.find(name); it == parsed.end() || it->second != argCount) {
            missing.push_back(name);
        }
    }
    return missing;
}

} // namespace

// Opt-in, needs a script source tree:
//   GECK_SSL_CORPUS=<Fallout2_Restoration_Project/scripts_src>
//   GECK_SSL_COMPILED=<Fallout2_Restoration_Project/data/scripts>  (optional)
//   general_tests "[.corpus]"
// Every script must preprocess and parse without a diagnostic, as sslc compiles them all. With the
// compiled scripts, only those sslc produced a .int for are held to that (the rest are include-only
// templates), and every procedure in a script's .int must be one the parser found, with as many
// parameters. (Not the reverse: sslc drops procedures nothing calls, such as most of the shared
// header helpers.)
TEST_CASE("Every script in a source tree reads cleanly", "[.corpus][ssl]") {
    const auto root = geck::test::environmentVariable("GECK_SSL_CORPUS");
    if (!root) {
        SKIP("GECK_SSL_CORPUS is not set");
    }
    const auto compiledDir = geck::test::environmentVariable("GECK_SSL_COMPILED");
    std::map<std::string, std::filesystem::path, std::less<>> compiled;
    if (compiledDir) {
        for (const auto& entry : std::filesystem::directory_iterator(*compiledDir)) {
            if (lower(entry.path().extension().string()) == ".int") {
                compiled.try_emplace(lower(entry.path().stem().string()), entry.path());
            }
        }
    }

    const DiskSourceProvider provider;
    int scripts = 0;
    int compared = 0;
    int setAiPacket = 0;
    int setTeam = 0;
    int withLocalVarNames = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(*root)) {
        if (entry.path().extension() != ".ssl") {
            continue;
        }
        const auto intFile = compiled.find(lower(entry.path().stem().string()));
        if (compiledDir && intFile == compiled.end()) {
            continue;
        }
        ++scripts;
        const ScriptUnit unit = ScriptUnit::load(provider, entry.path());
        const ScriptFacts facts = readFacts(unit);
        INFO(entry.path().string());
        for (const auto& problem : facts.problems) {
            UNSCOPED_INFO(problem);
        }
        CHECK(facts.problems.empty());
        setAiPacket += facts.assigns(ScriptFacts::Override::Field::AiPacket) ? 1 : 0;
        setTeam += facts.assigns(ScriptFacts::Override::Field::Team) ? 1 : 0;
        withLocalVarNames += facts.localVarNames.empty() ? 0 : 1;

        if (intFile != compiled.end()) {
            CHECK(missingFromParse(unit.program(), intFile->second) == std::vector<std::string>{});
            ++compared;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::cout << scripts << " scripts in " << elapsed.count() << " ms (" << compared << " compared with their .int); "
              << setAiPacket << " set their own AI packet, " << setTeam << " their team, " << withLocalVarNames
              << " name local variables\n";
    CHECK(scripts > 0);
}
