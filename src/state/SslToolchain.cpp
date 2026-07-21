#include "SslToolchain.h"

#include <QProcess>
#include <spdlog/spdlog.h>

namespace geck {

namespace {
    // Generous ceiling for a single tool run; sslc/int2ssl finish in well under a second on real
    // scripts, so hitting this means a hung tool, not a slow one.
    constexpr int TOOL_TIMEOUT_MS = 60'000;

    QString toQString(const std::filesystem::path& path) {
        return QString::fromStdString(path.string());
    }
} // namespace

SslToolchain::RunResult SslToolchain::compile(const QString& compilerPath,
    const std::filesystem::path& sslPath, const std::filesystem::path& intPath) {
    const QStringList arguments{ QStringLiteral("-q"), QStringLiteral("-l"), QStringLiteral("-p"),
        toQString(sslPath), QStringLiteral("-o"), toQString(intPath) };

    RunResult result = run(compilerPath, arguments, toQString(sslPath.parent_path()), "sslc");
    result.diagnostics = ssl::parseSslcOutput(result.output.toStdString());
    return result;
}

SslToolchain::RunResult SslToolchain::decompile(const QString& decompilerPath,
    const std::filesystem::path& intPath, const std::filesystem::path& sslPath) {
    const QStringList arguments{ toQString(intPath), toQString(sslPath) };
    return run(decompilerPath, arguments, toQString(intPath.parent_path()), "int2ssl");
}

SslToolchain::RunResult SslToolchain::run(const QString& program, const QStringList& arguments,
    const QString& workingDirectory, const char* logTag) {
    RunResult result;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    if (!workingDirectory.isEmpty()) {
        process.setWorkingDirectory(workingDirectory);
    }

    spdlog::info("[{}] running: {} {}", logTag, program.toStdString(),
        arguments.join(QLatin1Char(' ')).toStdString());

    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        spdlog::error("[{}] failed to start '{}': {}", logTag, program.toStdString(),
            process.errorString().toStdString());
        return result;
    }
    result.started = true;

    if (!process.waitForFinished(TOOL_TIMEOUT_MS)) {
        result.timedOut = true;
        spdlog::error("[{}] timed out after {} ms; killing it", logTag, TOOL_TIMEOUT_MS);
        process.kill();
        process.waitForFinished(5000);
        return result;
    }

    result.output = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;

    // Mirror the tool's own lines into the log at a level matching their severity, so the Log
    // panel is the compiler-output view (filter on the [sslc]/[int2ssl] tag).
    const QStringList lines = result.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString trimmedLine = line.trimmed();
        if (trimmedLine.isEmpty()) {
            continue;
        }
        if (trimmedLine.startsWith(QLatin1String("[Error]")) || trimmedLine.startsWith(QLatin1String("Error:"))) {
            spdlog::error("[{}] {}", logTag, trimmedLine.toStdString());
        } else if (trimmedLine.startsWith(QLatin1String("[Warning]"))) {
            spdlog::warn("[{}] {}", logTag, trimmedLine.toStdString());
        } else {
            spdlog::info("[{}] {}", logTag, trimmedLine.toStdString());
        }
    }
    spdlog::log(result.exitCode == 0 ? spdlog::level::info : spdlog::level::err,
        "[{}] finished with exit code {}", logTag, result.exitCode);

    return result;
}

} // namespace geck
