#pragma once

#include "util/SslOutputParser.h"

#include <QString>
#include <QStringList>
#include <filesystem>
#include <vector>

namespace geck {

/// Synchronous QProcess wrappers around the two external SSL toolchain programs: the sslc
/// compiler (.ssl -> .int) and the int2ssl decompiler (.int -> .ssl).
///
/// Both are user-provided binaries configured in Settings and invoked at arm's length —
/// neither is bundled: sslc has no upstream license file and int2ssl is GPL-3.0, so they must
/// stay separate executables. Every output line is mirrored into the log (tagged [sslc] /
/// [int2ssl]) so the Log panel doubles as the compiler-output view.
class SslToolchain {
public:
    struct RunResult {
        bool started = false;  // false: the binary could not be launched at all
        bool timedOut = false; // the tool ran past the timeout and was killed
        int exitCode = -1;
        QString output;                           // merged stdout+stderr (both tools print to stdout)
        std::vector<ssl::Diagnostic> diagnostics; // parsed [Error]/[Warning] lines (compile only)

        bool success() const { return started && !timedOut && exitCode == 0; }
    };

    /// Compile `sslPath` to `intPath` with the sslc binary at `compilerPath` ("compile.exe" is
    /// the binary's own upstream name, not a subcommand — `compilerPath` points straight at it).
    /// Arguments: -q -l -p <ssl> -o <int> (-q: never block waiting for a keypress on error,
    /// -l: no logo, -p: run the bundled mcpp preprocessor so #include/#define work). The
    /// working directory is the source file's folder so relative includes resolve.
    static RunResult compile(const QString& compilerPath, const std::filesystem::path& sslPath,
        const std::filesystem::path& intPath);

    /// Decompile `intPath` to `sslPath` with the int2ssl binary at `decompilerPath`.
    /// Invocation: int2ssl <int> <ssl>; it exits 0 only on success.
    static RunResult decompile(const QString& decompilerPath, const std::filesystem::path& intPath,
        const std::filesystem::path& sslPath);

private:
    static RunResult run(const QString& program, const QStringList& arguments,
        const QString& workingDirectory, const char* logTag);
};

} // namespace geck
