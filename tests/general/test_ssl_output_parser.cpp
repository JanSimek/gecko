#include <catch2/catch_test_macros.hpp>

#include "util/SslOutputParser.h"

using geck::ssl::countDiagnostics;
using geck::ssl::DiagnosticSeverity;
using geck::ssl::parseSslcOutput;

TEST_CASE("parseSslcOutput reads sslc's [Error]/[Warning] location lines", "[ssl]") {
    // Exact shape printed by sslc's parseError/parseWarning: "[Error] <%s>:%d:%d: %s\n".
    const auto diagnostics = parseSslcOutput(
        "Compiling test.ssl\n"
        "[Warning] <test.ssl>:4:1: Unrecognised escape character 'q'\n"
        "[Error] <test.ssl>:12:5: ';' expected.\n"
        "*** THERE WERE ERRORS (1 of them) ***\n");

    REQUIRE(diagnostics.size() == 2);

    CHECK(diagnostics[0].severity == DiagnosticSeverity::Warning);
    CHECK(diagnostics[0].file == "test.ssl");
    CHECK(diagnostics[0].line == 4);
    CHECK(diagnostics[0].column == 1);
    CHECK(diagnostics[0].message == "Unrecognised escape character 'q'");

    CHECK(diagnostics[1].severity == DiagnosticSeverity::Error);
    CHECK(diagnostics[1].file == "test.ssl");
    CHECK(diagnostics[1].line == 12);
    CHECK(diagnostics[1].column == 5);
    CHECK(diagnostics[1].message == "';' expected.");

    CHECK(countDiagnostics(diagnostics, DiagnosticSeverity::Error) == 1);
    CHECK(countDiagnostics(diagnostics, DiagnosticSeverity::Warning) == 1);
}

TEST_CASE("parseSslcOutput tolerates a missing column and CRLF line endings", "[ssl]") {
    const auto diagnostics = parseSslcOutput("[Error] <foo.ssl>:7: Undefined symbol\r\n");

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].file == "foo.ssl");
    CHECK(diagnostics[0].line == 7);
    CHECK(diagnostics[0].column == 0);
    CHECK(diagnostics[0].message == "Undefined symbol");
}

TEST_CASE("parseSslcOutput keeps messages containing colons and brackets intact", "[ssl]") {
    const auto diagnostics = parseSslcOutput(
        "[Error] <dir/my.ssl>:3:9: expected '>' before ':' near include\n");

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].file == "dir/my.ssl");
    CHECK(diagnostics[0].line == 3);
    CHECK(diagnostics[0].column == 9);
    CHECK(diagnostics[0].message == "expected '>' before ':' near include");
}

TEST_CASE("parseSslcOutput degrades gracefully on an unexpected tail shape", "[ssl]") {
    // A prefixed line that doesn't match the location grammar must survive as a bare message,
    // not vanish — losing an error is worse than losing its position.
    const auto diagnostics = parseSslcOutput("[Error] something went wrong\n");

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].file.empty());
    CHECK(diagnostics[0].line == 0);
    CHECK(diagnostics[0].message == "something went wrong");
}

TEST_CASE("parseSslcOutput ignores non-diagnostic output entirely", "[ssl]") {
    CHECK(parseSslcOutput("").empty());
    CHECK(parseSslcOutput("Startrek Scripting Language compiler\nCompiling a.ssl\n").empty());
}
