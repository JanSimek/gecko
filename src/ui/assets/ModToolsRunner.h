#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <functional>

class QProcess;

namespace geck {

/// One answer from the mod's tools/gecko_assets.py: its last line of output is a JSON object,
/// {"ok": true, ...} or {"ok": false, "error": "..."}.
struct ModToolResult {
    bool ok = false;
    QJsonObject json; ///< the whole answer, when there was one
    QString error;    ///< the tool's own message, or why it could not run
};

/**
 * Runs the mod's tools/gecko_assets.py for the Tools menu: scenery from a picture, mirrored and
 * resized copies, names and examine text, and Apply New Art. The mod's rules (names, ids, the
 * registry, the deploy chain) all live on the Python side; this starts it and reads the answer.
 *
 * The mod folder and the Python to run it come from Preferences > Mod Tools.
 */
class ModToolsRunner : public QObject {
    Q_OBJECT

public:
    ModToolsRunner(std::filesystem::path root, QString python, QObject* parent = nullptr);

    /// root/tools/gecko_assets.py
    static std::filesystem::path scriptFor(const std::filesystem::path& root);
    /// Whether @p root holds the script, i.e. the Tools menu can work.
    static bool isModToolsRoot(const std::filesystem::path& root);

    [[nodiscard]] const std::filesystem::path& root() const { return _root; }
    [[nodiscard]] const QString& python() const { return _python; }

    /// Starts a command without blocking; @p done receives the answer on the GUI thread. When the
    /// runner is destroyed first, the process is killed and @p done is never called.
    QProcess* run(const QStringList& args, std::function<void(const ModToolResult&)> done);

    /// Runs a command and waits for it in a local event loop (the window keeps painting; input is
    /// held back). Gives up after @p timeoutMs and kills the process.
    ModToolResult runAndWait(const QStringList& args, int timeoutMs = 180000);

    /// Starts `apply` in a console window of its own, independent of gecko: it waits for
    /// @p geckoPid to exit, deploys the mod, and opens @p geckoExe again (on @p mapPath if given).
    bool startApply(qint64 geckoPid, const QString& geckoExe, const QString& mapPath, QString* error) const;

    /// The answer in a finished command's output: the last line of stdout that is a JSON object.
    static ModToolResult parseOutput(int exitCode, const QByteArray& standardOutput, const QByteArray& standardError);

private:
    QStringList scriptArguments(const QStringList& args) const;

    std::filesystem::path _root;
    QString _python;
};

} // namespace geck
