#include <catch2/catch_test_macros.hpp>

// QProcess-driving tests for the SslToolchain wrapper, using fake shell-script "tools" so no
// real sslc/int2ssl binary is needed. POSIX-only: Windows can't execute shebang scripts via
// QProcess, and faking .bat quoting there tests cmd.exe more than our wrapper.
#ifndef _WIN32

#include "state/SslToolchain.h"
#include "ui/ScriptSourceService.h"
#include "util/FileIo.h"

#include <filesystem>
#include <sys/stat.h>

#ifndef GECK_TEST_TMP_DIR
#error "GECK_TEST_TMP_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

using namespace geck;
namespace fs = std::filesystem;

namespace {

fs::path writeToolScript(const fs::path& path, const std::string& body) {
    io::writeFile(path, "#!/bin/sh\n" + body);
    REQUIRE(::chmod(path.c_str(), 0755) == 0);
    return path;
}

} // namespace

TEST_CASE("SslToolchain::compile parses diagnostics from a failing run", "[ssl][toolchain]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "ssl_toolchain_fail";
    fs::remove_all(base);
    const fs::path source = base / "broken.ssl";
    io::writeFile(source, "procedure start begin end");

    // Args are: -q -l -p <ssl> -o <int>; a real failing sslc prints the bracketed diagnostics
    // to stdout and exits 1 (see parse.c / compile.c upstream).
    const fs::path fakeSslc = writeToolScript(base / "sslc",
        "echo \"Compiling $4\"\n"
        "echo \"[Warning] <broken.ssl>:1:5: unused variable\"\n"
        "echo \"[Error] <broken.ssl>:3:1: ';' expected.\"\n"
        "echo \"*** THERE WERE ERRORS (1 of them) ***\"\n"
        "exit 1\n");

    const auto result = SslToolchain::compile(QString::fromStdString(fakeSslc.string()),
        source, base / "broken.int");

    CHECK(result.started);
    CHECK_FALSE(result.timedOut);
    CHECK(result.exitCode == 1);
    CHECK_FALSE(result.success());
    REQUIRE(result.diagnostics.size() == 2);
    CHECK(result.diagnostics[0].severity == ssl::DiagnosticSeverity::Warning);
    CHECK(result.diagnostics[1].severity == ssl::DiagnosticSeverity::Error);
    CHECK(result.diagnostics[1].line == 3);
    CHECK(result.output.contains("THERE WERE ERRORS"));

    fs::remove_all(base);
}

TEST_CASE("SslToolchain::compile succeeds when the tool writes the output and exits 0", "[ssl][toolchain]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "ssl_toolchain_ok";
    fs::remove_all(base);
    const fs::path source = base / "good.ssl";
    const fs::path output = base / "good.int";
    io::writeFile(source, "procedure start begin end");

    const fs::path fakeSslc = writeToolScript(base / "sslc",
        "echo \"Compiling $4\"\n"
        "echo bytecode > \"$6\"\n"
        "exit 0\n");

    const auto result = SslToolchain::compile(QString::fromStdString(fakeSslc.string()), source, output);

    CHECK(result.success());
    CHECK(result.exitCode == 0);
    CHECK(result.diagnostics.empty());
    CHECK(fs::exists(output));

    fs::remove_all(base);
}

TEST_CASE("SslToolchain reports a missing tool binary as not started", "[ssl][toolchain]") {
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "ssl_toolchain_missing";
    fs::remove_all(base);

    const auto result = SslToolchain::decompile(
        QString::fromStdString((base / "no-such-int2ssl").string()),
        base / "a.int", base / "a.ssl");

    CHECK_FALSE(result.started);
    CHECK_FALSE(result.success());
}

TEST_CASE("compileToTarget never damages the deployed .int on failure", "[ssl][toolchain]") {
    using Status = ScriptSourceService::CompileToTargetResult::Status;
    const fs::path base = fs::path{ GECK_TEST_TMP_DIR } / "ssl_compile_target";
    fs::remove_all(base);
    const fs::path source = base / "s.ssl";
    const fs::path target = base / "s.int";
    io::writeFile(source, "procedure start begin end");

    // Args are: -q -l -p <ssl> -o <int>, so the output path is $6.
    const fs::path okTool = writeToolScript(base / "ok", "printf bytecode > \"$6\"\nexit 0\n");
    // A real sslc removes its own output on a parse error (parse.c) and exits non-zero.
    const fs::path failTool = writeToolScript(base / "fail",
        "echo \"[Error] <s.ssl>:1:1: bad\"\nrm -f \"$6\"\nexit 1\n");
    // Exit 0 but write nothing (a missing input is reported as a warning upstream).
    const fs::path noOutputTool = writeToolScript(base / "noout", "exit 0\n");

    const QString okPath = QString::fromStdString(okTool.string());
    const QString failPath = QString::fromStdString(failTool.string());
    const QString nooutPath = QString::fromStdString(noOutputTool.string());

    SECTION("a good build writes and then replaces the target") {
        const auto first = ScriptSourceService::compileToTarget(okPath, source, target);
        CHECK(first.status == Status::Success);
        CHECK(io::readFile(target) == "bytecode");

        // A second good build atomically replaces the existing target.
        const fs::path okTool2 = writeToolScript(base / "ok2", "printf REBUILT > \"$6\"\nexit 0\n");
        const auto second
            = ScriptSourceService::compileToTarget(QString::fromStdString(okTool2.string()), source, target);
        CHECK(second.status == Status::Success);
        CHECK(io::readFile(target) == "REBUILT");
    }

    SECTION("a failing compile leaves an existing target byte-for-byte intact (finding 2)") {
        io::writeFile(target, "GOOD-BUILD"); // the last working bytecode
        const auto outcome = ScriptSourceService::compileToTarget(failPath, source, target);
        CHECK(outcome.status == Status::CompileFailed);
        CHECK(outcome.errors == 1);
        REQUIRE(fs::exists(target));
        CHECK(io::readFile(target) == "GOOD-BUILD"); // not deleted, not truncated
    }

    SECTION("an exit-0 run that writes no output is a failure, not a false success (finding 3)") {
        io::writeFile(target, "GOOD-BUILD");
        const auto outcome = ScriptSourceService::compileToTarget(nooutPath, source, target);
        CHECK(outcome.status == Status::CompileFailed);
        CHECK(io::readFile(target) == "GOOD-BUILD"); // the stale target must not be reported as success
    }

    SECTION("a missing compiler binary reports NotStarted, target untouched") {
        io::writeFile(target, "GOOD-BUILD");
        const auto outcome = ScriptSourceService::compileToTarget(
            QString::fromStdString((base / "no-such-sslc").string()), source, target);
        CHECK(outcome.status == Status::NotStarted);
        CHECK(io::readFile(target) == "GOOD-BUILD");
    }

    fs::remove_all(base);
}

#endif // _WIN32
