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
#include "ssl/CallGraph.h"
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

struct ProcedureDiff {
    std::vector<std::string> onlyParsed;    // kept by the call graph, not in the .int
    std::vector<std::string> onlyCompiled;  // in the .int, not kept by the call graph
    std::vector<std::string> otherArgCount; // in both, with a different argument count
};

// How the procedures CallGraph keeps differ from those a compiled .int holds.
ProcedureDiff compareWithCompiled(const Program& program, const std::filesystem::path& intFile) {
    const CallGraph graph(program);
    std::map<std::string, int, std::less<>> kept;
    for (const Procedure& procedure : program.procedures) {
        if (graph.isLive(procedure.name)) {
            kept.insert_or_assign(lower(procedure.name), static_cast<int>(procedure.parameters.size()));
        }
    }
    const auto compiled = compiledProcedures(intFile);
    ProcedureDiff diff;
    for (const auto& [name, argCount] : kept) {
        if (const auto it = compiled.find(name); it == compiled.end()) {
            diff.onlyParsed.push_back(name);
        } else if (it->second != argCount) {
            diff.otherArgCount.push_back(name);
        }
    }
    for (const auto& [name, argCount] : compiled) {
        if (!kept.contains(name)) {
            diff.onlyCompiled.push_back(name);
        }
    }
    return diff;
}

} // namespace

// Opt-in, needs a script source tree:
//   GECK_SSL_CORPUS=<Fallout2_Restoration_Project/scripts_src>
//   GECK_SSL_COMPILED=<Fallout2_Restoration_Project/data/scripts>  (optional)
//   general_tests "[.corpus]"
// Every script must preprocess and parse without a diagnostic, as sslc compiles them all. With the
// compiled scripts, only those sslc produced a .int for are held to that (the rest are include-only
// templates), and a script's .int must hold exactly the procedures the call graph says sslc keeps,
// each with as many parameters as the parser found.
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
            const ProcedureDiff diff = compareWithCompiled(unit.program(), intFile->second);
            CHECK(diff.onlyParsed == std::vector<std::string>{});
            CHECK(diff.onlyCompiled == std::vector<std::string>{});
            CHECK(diff.otherArgCount == std::vector<std::string>{});
            ++compared;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::cout << scripts << " scripts in " << elapsed.count() << " ms (" << compared << " compared with their .int); "
              << setAiPacket << " set their own AI packet, " << setTeam << " their team, " << withLocalVarNames
              << " name local variables\n";
    CHECK(scripts > 0);
}
