#include "ModToolsRunner.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QList>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <memory>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace geck {

namespace {

    QString toQString(const std::filesystem::path& path) {
        return QString::fromStdU16String(path.u16string());
    }

#ifdef _WIN32
    /// One argument quoted for CreateProcess the way CommandLineToArgvW reads it back: wrapped in
    /// quotes when it has a space, tab or quote; a run of backslashes doubled only before a quote.
    QString quoteArgument(const QString& argument) {
        const bool plain = !argument.isEmpty()
            && std::none_of(argument.begin(), argument.end(),
                [](QChar c) { return c == u' ' || c == u'\t' || c == u'"'; });
        if (plain) {
            return argument;
        }
        QString quoted = QStringLiteral("\"");
        int backslashes = 0;
        for (const QChar c : argument) {
            if (c == u'\\') {
                ++backslashes;
                continue;
            }
            if (c == u'"') {
                quoted += QString(backslashes * 2 + 1, u'\\');
            } else {
                quoted += QString(backslashes, u'\\');
            }
            quoted += c;
            backslashes = 0;
        }
        quoted += QString(backslashes * 2, u'\\');
        quoted += u'"';
        return quoted;
    }
#endif

    QString lastLines(const QByteArray& text, int count) {
        QStringList lines = QString::fromUtf8(text).split(u'\n', Qt::SkipEmptyParts);
        if (lines.size() > count) {
            lines = lines.mid(lines.size() - count);
        }
        return lines.join(u'\n').trimmed();
    }

} // namespace

ModToolsRunner::ModToolsRunner(std::filesystem::path root, QString python, QObject* parent)
    : QObject(parent)
    , _root(std::move(root))
    , _python(python.trimmed().isEmpty() ? QStringLiteral("python") : python.trimmed()) {
}

std::filesystem::path ModToolsRunner::scriptFor(const std::filesystem::path& root) {
    return root / "tools" / "gecko_assets.py";
}

bool ModToolsRunner::isModToolsRoot(const std::filesystem::path& root) {
    std::error_code ec;
    return !root.empty() && std::filesystem::is_regular_file(scriptFor(root), ec);
}

std::filesystem::path ModToolsRunner::detectRootAboveApplication() {
    std::filesystem::path dir(QCoreApplication::applicationDirPath().toStdU16String());
    for (int level = 0; level < 8 && !dir.empty(); ++level) {
        if (isModToolsRoot(dir)) {
            return dir.lexically_normal();
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return {};
}

std::filesystem::path ModToolsRunner::resolveRoot(const std::filesystem::path& configured) {
    if (isModToolsRoot(configured)) {
        return configured;
    }
    return detectRootAboveApplication();
}

QStringList ModToolsRunner::scriptArguments(const QStringList& args) const {
    return QStringList{ toQString(scriptFor(_root)) } + args;
}

QProcess* ModToolsRunner::run(const QStringList& args, std::function<void(const ModToolResult&)> done) {
    auto* process = new QProcess(this);
    process->setProgram(_python);
    process->setArguments(scriptArguments(args));
    process->setWorkingDirectory(toQString(_root));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    process->setProcessEnvironment(environment);

    spdlog::debug("Mod tools: {} {}", _python.toStdString(), args.join(u' ').toStdString());

    // Shared so whichever signal arrives first answers, and only once.
    auto answer = std::make_shared<std::function<void(const ModToolResult&)>>(std::move(done));
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [process, answer](int exitCode, QProcess::ExitStatus) {
            const ModToolResult result
                = parseOutput(exitCode, process->readAllStandardOutput(), process->readAllStandardError());
            process->deleteLater();
            if (*answer) {
                std::exchange(*answer, nullptr)(result);
            }
        });
    connect(process, &QProcess::errorOccurred, this, [this, process, answer](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return; // a crash or kill still ends in finished()
        }
        ModToolResult result;
        result.error = QString("Couldn't start \"%1\". Check the Python setting in File > Preferences > Mod Tools.")
                           .arg(_python);
        process->deleteLater();
        if (*answer) {
            std::exchange(*answer, nullptr)(result);
        }
    });
    process->start();
    return process;
}

ModToolResult ModToolsRunner::runAndWait(const QStringList& args, int timeoutMs) {
    struct Wait {
        ModToolResult result;
        bool finished = false;
        QEventLoop* loop = nullptr;
    };
    auto wait = std::make_shared<Wait>();
    QEventLoop loop;
    wait->loop = &loop;

    QPointer<QProcess> process = run(args, [wait](const ModToolResult& result) {
        wait->result = result;
        wait->finished = true;
        if (wait->loop) {
            wait->loop->quit();
        }
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    if (!wait->finished) {
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    wait->loop = nullptr;

    if (!wait->finished) {
        if (process) {
            process->kill();
        }
        ModToolResult result;
        result.error = QString("The mod tools took longer than %1 seconds and were stopped.").arg(timeoutMs / 1000);
        return result;
    }
    return wait->result;
}

ModToolResult ModToolsRunner::parseOutput(int exitCode, const QByteArray& standardOutput, const QByteArray& standardError) {
    ModToolResult result;
    const QList<QByteArray> lines = standardOutput.split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QByteArray line = it->trimmed();
        if (!line.startsWith('{')) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            result.json = document.object();
            result.ok = result.json.value("ok").toBool();
            result.error = result.json.value("error").toString();
            return result;
        }
    }

    // No answer: Python itself failed (a missing package, a syntax error) or was killed.
    QString detail = lastLines(standardError, 12);
    if (detail.isEmpty()) {
        detail = lastLines(standardOutput, 12);
    }
    result.error = QString("The mod tools stopped without an answer (exit code %1).").arg(exitCode);
    if (!detail.isEmpty()) {
        result.error += "\n\n" + detail;
    }
    return result;
}

bool ModToolsRunner::startApply(qint64 geckoPid, const QString& geckoExe, const QString& mapPath, QString* error) const {
    QStringList arguments = scriptArguments(
        { QStringLiteral("apply"), QStringLiteral("--wait-pid"), QString::number(geckoPid), QStringLiteral("--gecko"), geckoExe });
    if (!mapPath.isEmpty()) {
        arguments << QStringLiteral("--map") << mapPath;
    }

#ifdef _WIN32
    // CreateProcess directly: the deploy has to show in a console window of its own that outlives
    // gecko, and QProcess::startDetached gives a GUI parent's child no console at all.
    QString commandLine = quoteArgument(_python);
    for (const QString& argument : arguments) {
        commandLine += u' ' + quoteArgument(argument);
    }
    std::wstring command = commandLine.toStdWString();
    const std::wstring directory = _root.wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
            CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP, nullptr, directory.c_str(), &startup, &info)) {
        if (error) {
            *error = QString("Couldn't start \"%1\" (Windows error %2). Check the Python setting in "
                             "File > Preferences > Mod Tools.")
                         .arg(_python)
                         .arg(GetLastError());
        }
        return false;
    }
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return true;
#else
    if (!QProcess::startDetached(_python, arguments, toQString(_root))) {
        if (error) {
            *error = QString("Couldn't start \"%1\". Check the Python setting in Preferences > Mod Tools.").arg(_python);
        }
        return false;
    }
    return true;
#endif
}

} // namespace geck
