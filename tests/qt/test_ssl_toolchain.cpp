#include <catch2/catch_test_macros.hpp>

// QProcess-driving tests for the SslToolchain wrapper, using fake shell-script "tools" so no
// real sslc/int2ssl binary is needed. POSIX-only: Windows can't execute shebang scripts via
// QProcess, and faking .bat quoting there tests cmd.exe more than our wrapper.
#ifndef _WIN32

#include "state/SslToolchain.h"
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

#endif // _WIN32
