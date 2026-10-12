#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "ssl/Preprocessor.h"

using namespace geck::ssl;

namespace {

// The preprocessed tokens of `main`, joined with single spaces.
std::string preprocess(MemorySourceProvider& files, const std::string& main, std::vector<Diagnostic>* diagnostics = nullptr) {
    SourceManager sources(files);
    Preprocessor pp(sources);
    std::string out;
    for (const Token& token : pp.run(main)) {
        out += (out.empty() ? "" : " ") + std::string(token.text);
    }
    if (diagnostics) {
        *diagnostics = pp.diagnostics();
    }
    return out;
}

std::string preprocessText(const std::string& text, std::vector<Diagnostic>* diagnostics = nullptr) {
    MemorySourceProvider files;
    files.add("main.ssl", text);
    return preprocess(files, "main.ssl", diagnostics);
}

} // namespace

TEST_CASE("Lexer treats comments as blanks and joins backslash-continued lines", "[ssl][lexer]") {
    std::vector<Diagnostic> diagnostics;
    const auto tokens = lex("a := 1; // comment\n/* block\n */ b \\  \n c \"x // y\" 0x1F", 1, diagnostics);
    std::vector<std::string> texts;
    for (const Token& token : tokens) {
        texts.emplace_back(token.text);
    }
    CHECK(texts == std::vector<std::string>{ "a", ":=", "1", ";", "b", "c", "\"x // y\"", "0x1F", "" });
    CHECK(tokens[4].atLineStart);       // b starts a line after the block comment
    CHECK_FALSE(tokens[5].atLineStart); // c is on b's logical line: the backslash (then blanks) joins them
    CHECK(tokens[5].location.line == 4);
    CHECK(diagnostics.empty());
}

TEST_CASE("Preprocessor expands object- and function-like macros and rescans the result", "[ssl][preprocessor]") {
    CHECK(preprocessText("#define TEAM_ARROYO (1)\n"
                         "#define set_team(cr, team) critter_add_trait(cr, TRAIT_OBJECT, OBJECT_TEAM_NUM, team)\n"
                         "#define set_self_team(team) set_team(self_obj, team)\n"
                         "set_self_team(TEAM_ARROYO);")
        == "critter_add_trait ( self_obj , TRAIT_OBJECT , OBJECT_TEAM_NUM , ( 1 ) ) ;");
}

TEST_CASE("Preprocessor does not expand a macro inside its own expansion", "[ssl][preprocessor]") {
    CHECK(preprocessText("#define foo foo + 1\nfoo") == "foo + 1");
    CHECK(preprocessText("#define a b\n#define b a\na") == "a");
    CHECK(preprocessText("#define f(x) x f\nf(1)(2)") == "1 f ( 2 )"); // the f from f's body stays an f
}

TEST_CASE("Preprocessor leaves a function-like macro name alone when no call follows", "[ssl][preprocessor]") {
    CHECK(preprocessText("#define f(x) [x]\nf + f(2)") == "f + [ 2 ]");
}

TEST_CASE("Preprocessor pastes with ## and stringizes with #", "[ssl][preprocessor]") {
    // RP modoc.h: check_seat_party_member(Vic) reads Vic_In_Party.
    CHECK(preprocessText("#define in_party(who) who##_In_Party\n#define str(x) #x\nin_party(Vic) str(a \"b\")")
        == "Vic_In_Party \"a \\\"b\\\"\"");
}

TEST_CASE("Preprocessor supports variadic macros", "[ssl][preprocessor]") {
    // sfall.h picks an overload by argument count this way.
    CHECK(preprocessText("#define SEL(_1,_2,_3,F,...) F\n"
                         "#define box(...) SEL(__VA_ARGS__, box3, box2, box1)(__VA_ARGS__)\n"
                         "box(a, b)")
        == "box2 ( a , b )");
}

TEST_CASE("Preprocessor follows #if, #ifdef, #ifndef, #elif and #else", "[ssl][preprocessor]") {
    CHECK(preprocessText("#define A 2\n"
                         "#ifdef A\nyes1\n#else\nno1\n#endif\n"
                         "#ifndef B\nyes2\n#endif\n"
                         "#if A > 3\nno3\n#elif defined(A) && A == 2\nyes3\n#else\nno3\n#endif\n"
                         "#if 0\n#if 1\nno4\n#endif\n#define HIDDEN 1\n#endif\n"
                         "#ifdef HIDDEN\nno5\n#endif")
        == "yes1 yes2 yes3");
}

TEST_CASE("Preprocessor includes files relative to the includer, ignoring Windows separators", "[ssl][preprocessor]") {
    MemorySourceProvider files;
    files.add("headers/define.h", "#ifndef DEFINE_H\n#define DEFINE_H\n#define TRUE 1\n#endif\n");
    files.add("headers/command.h", "#include \"define.h\"\n#define ok TRUE\n");
    files.add("arroyo/acvillgr.ssl", "#include \"..\\headers\\define.h\"\n#include \"../headers/command.h\"\nok");
    std::vector<Diagnostic> diagnostics;
    CHECK(preprocess(files, "arroyo/acvillgr.ssl", &diagnostics) == "1");
    CHECK(diagnostics.empty());
}

TEST_CASE("Preprocessor includes a file named by a macro", "[ssl][preprocessor]") {
    // RP generic/zilocker.ssl: `#ifdef EXTRA_HEADER  #include EXTRA_HEADER`, set by the script including it.
    MemorySourceProvider files;
    files.add("headers/newreno.h", "#define CITY reno\n");
    files.add("generic/zilocker.ssl", "#ifdef EXTRA_HEADER\n   #include EXTRA_HEADER\n#endif\nCITY");
    files.add("newreno/niowrilk.ssl", "#define EXTRA_HEADER \"../headers/newreno.h\"\n#include \"../generic/zilocker.ssl\"");
    CHECK(preprocess(files, "newreno/niowrilk.ssl") == "reno");
}

TEST_CASE("Preprocessor reports what it cannot do and carries on", "[ssl][preprocessor]") {
    std::vector<Diagnostic> diagnostics;
    CHECK(preprocessText("#include \"missing.h\"\n#if 1\nkept", &diagnostics) == "kept");
    REQUIRE(diagnostics.size() == 2);
    CHECK(diagnostics[0].message == "cannot open include file missing.h");
    CHECK(diagnostics[1].message == "#if without #endif");
}

TEST_CASE("Preprocessor tells where an expanded token was written and through which macros", "[ssl][preprocessor]") {
    MemorySourceProvider files;
    files.add("main.ssl", "#define inner(x) op(x)\n#define outer(x) inner(x)\n\n  outer(V);");
    SourceManager sources(files);
    Preprocessor pp(sources);
    const auto tokens = pp.run("main.ssl");
    REQUIRE(!tokens.empty());
    const Token& op = tokens.front();
    REQUIRE(op.text == "op");
    CHECK(pp.writtenAt(op).line == 4);
    const auto chain = pp.macroChain(op);
    REQUIRE(chain.size() == 2);
    CHECK(chain[0]->name == "outer");
    CHECK(chain[1]->name == "inner");
    CHECK(pp.macroChain(tokens[2]).empty()); // V: an argument, written by the user
}

TEST_CASE("Preprocessor evaluates a macro as a constant", "[ssl][preprocessor]") {
    MemorySourceProvider files;
    files.add("main.ssl", "#define BASE (4)\n#define LVAR_Hostile (BASE + 1)\n#define NOT_CONST self_obj\n");
    SourceManager sources(files);
    Preprocessor pp(sources);
    pp.run("main.ssl");
    REQUIRE(pp.find("LVAR_Hostile") != nullptr);
    CHECK(pp.evaluate(*pp.find("LVAR_Hostile")) == 5);
    CHECK_FALSE(pp.evaluate(*pp.find("NOT_CONST")).has_value());
}
