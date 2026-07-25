#include "ScriptSourceService.h"

#include "ExternalEditorLauncher.h"
#include "QtDialogs.h"
#include "Settings.h"
#include "format/lst/Lst.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "resource/ScriptSourceLocator.h"
#include "reader/ReaderExceptions.h"
#include "resource/WritableDataRoot.h"
#include "state/SslToolchain.h"
#include "util/FileIo.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <spdlog/spdlog.h>

#include <filesystem>

namespace geck {

namespace fs = std::filesystem;

ScriptSourceService::ScriptSourceService(resource::GameResources& resources,
    std::shared_ptr<Settings> settings, ExternalEditorLauncher& editorLauncher, QWidget* dialogParent)
    : _resources(resources)
    , _settings(std::move(settings))
    , _editorLauncher(editorLauncher)
    , _dialogParent(dialogParent) {
}

std::string ScriptSourceService::resolveBaseName(int programIndex) {
    try {
        const Lst* lst = _resources.repository().load<Lst>(std::string(ResourcePaths::Lst::SCRIPTS));
        if (lst != nullptr && programIndex >= 0
            && static_cast<size_t>(programIndex) < lst->list().size()) {
            const std::string baseName
                = resource::scriptBaseName(lst->list().at(static_cast<size_t>(programIndex)));
            if (!baseName.empty()) {
                return baseName;
            }
        }
    } catch (const FileReaderException& e) {
        spdlog::warn("scripts.lst not available: {}", e.what());
    }
    QtDialogs::showError(_dialogParent, "Edit Script",
        QString("Script #%1 could not be resolved through scripts.lst — is the game data mounted?")
            .arg(programIndex));
    return {};
}

namespace {

    // The tool path stored in Settings when it exists, prompting a locate dialog otherwise.
    // Shared shape for both binaries; `describe` names the tool in the prompts.
    template <typename PersistFn>
    QString ensureToolPath(QWidget* parent, const QString& configured, const QString& describe,
        PersistFn persist) {
        if (!configured.isEmpty() && QFileInfo::exists(configured)) {
            return configured;
        }

        if (const QString reason = configured.isEmpty()
                ? QString("%1 is not configured.").arg(describe)
                : QString("The configured %1 no longer exists:\n%2").arg(describe, configured);
            !QtDialogs::showQuestion(parent, "Script Tools",
                reason + "\n\nLocate the executable now? (It can also be set later in "
                         "Preferences › Script Tools.)")) {
            return {};
        }

        const QString chosen = QFileDialog::getOpenFileName(parent,
            QString("Select the %1").arg(describe),
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
            "Executable Files (*.exe *);;All Files (*)");
        if (chosen.isEmpty()) {
            return {};
        }
        persist(chosen);
        return chosen;
    }

} // namespace

QString ScriptSourceService::ensureCompilerPath() {
    return ensureToolPath(_dialogParent, _settings->getSslCompilerPath(), "sslc compiler",
        [this](const QString& path) {
            _settings->setSslCompilerPath(path);
            _settings->save();
        });
}

QString ScriptSourceService::ensureDecompilerPath() {
    return ensureToolPath(_dialogParent, _settings->getSslDecompilerPath(), "int2ssl decompiler",
        [this](const QString& path) {
            _settings->setSslDecompilerPath(path);
            _settings->save();
        });
}

bool ScriptSourceService::openFromScriptSourceRoots(const std::string& baseName) {
    // A marked script-source tree (e.g. FRP scripts_src): open <name>.ssl there with its root as
    // the editor's workspace, so VS Code + BGforge MLS can resolve the tree's headers and compile.
    const auto sourcePaths = _settings->getScriptSourcePaths();
    if (sourcePaths.empty()) {
        return false;
    }
    bool ambiguous = false;
    const auto match = resource::findScriptSourceInRoots(sourcePaths, baseName, &ambiguous);
    if (!match) {
        return false; // not under the source roots — let the caller try the VFS / decompile paths
    }
    if (ambiguous) {
        spdlog::warn("Edit Script: multiple sources named '{}.ssl' under the script-source roots; opening {}",
            baseName, match->file.string());
    }
    _editorLauncher.openFileInWorkspace(QString::fromStdString(match->file.string()),
        QString::fromStdString(match->sourceRoot.string()));
    return true;
}

void ScriptSourceService::editScriptSource(int programIndex) {
    const std::string baseName = resolveBaseName(programIndex);
    if (baseName.empty()) {
        return;
    }

    // 0) The primary path when the user has pointed Gecko at a source tree.
    if (openFromScriptSourceRoots(baseName)) {
        return;
    }

    auto& files = _resources.files();
    const auto writableRoot = _settings->resolveWritableDataPath();

    // 1) A loose .ssl on disk: open it directly.
    if (const auto source = resource::locateScriptSource(files, baseName)) {
        if (!source->insideDat && !source->diskPath.empty()) {
            // A native path — openFile() would try to resolve it as a VFS path and fail, so open it
            // directly (no workspace: this loose file isn't part of a marked source tree).
            _editorLauncher.openFileInWorkspace(QString::fromStdString(source->diskPath.string()), QString());
            return;
        }

        // 2) The .ssl exists but only inside a DAT: extract an editable copy that shadows it.
        if (!writableRoot) {
            QtDialogs::showError(_dialogParent, "Edit Script",
                "The source lives inside a DAT archive, but no writable data folder is "
                "configured to extract it to. Add a folder in Preferences › Data Paths.");
            return;
        }
        if (!QtDialogs::showQuestion(_dialogParent, "Edit Script",
                QString("The source for \"%1\" is inside a DAT archive.\n\nExtract an editable "
                        "copy to %2?")
                    .arg(QString::fromStdString(baseName),
                        QString::fromStdString((*writableRoot / source->vfsPath).string())))) {
            return;
        }
        try {
            const fs::path copy
                = resource::ensureWritableCopy(files, *writableRoot, source->vfsPath.generic_string());
            files.refresh(); // the loose copy must shadow the DAT on the next lookup
            _editorLauncher.openFileInWorkspace(QString::fromStdString(copy.string()), QString());
        } catch (const resource::WritableCopyError& e) {
            QtDialogs::showError(_dialogParent, "Edit Script",
                QString("Extracting the source failed: %1").arg(e.what()));
        }
        return;
    }

    // 3) No source anywhere: offer to decompile the compiled .int (clearly flagged as lossy).
    const auto compiled = resource::locateCompiledScript(files, baseName);
    if (!compiled) {
        QtDialogs::showError(_dialogParent, "Edit Script",
            QString("Neither scripts/%1.ssl nor scripts/%1.int was found in the mounted data.")
                .arg(QString::fromStdString(baseName)));
        return;
    }

    const auto sslTarget = resource::decompiledSourceTarget(files, writableRoot, baseName);
    if (!sslTarget) {
        QtDialogs::showError(_dialogParent, "Edit Script",
            "No writable data folder is configured to hold the decompiled source. Add a folder "
            "in Preferences › Data Paths.");
        return;
    }

    if (!QtDialogs::showQuestion(_dialogParent, "Edit Script",
            QString("No .ssl source was found for \"%1\".\n\nDecompile the compiled script with "
                    "int2ssl? The result is best-effort and lossy — comments and original names "
                    "are not recoverable.")
                .arg(QString::fromStdString(baseName)))) {
        return;
    }

    const QString decompilerPath = ensureDecompilerPath();
    if (decompilerPath.isEmpty()) {
        return;
    }

    // int2ssl needs the .int as a real file; extract DAT-resident bytes to a temp location
    // rather than littering the user's data folder with unpacked bytecode.
    std::string intDiskPath;
    if (!compiled->insideDat && !compiled->diskPath.empty()) {
        intDiskPath = compiled->diskPath.string();
    } else {
        const auto bytes = files.readRawBytes(compiled->vfsPath);
        if (!bytes) {
            QtDialogs::showError(_dialogParent, "Edit Script",
                QString("Reading %1 from the mounted data failed.")
                    .arg(QString::fromStdString(compiled->vfsPath.generic_string())));
            return;
        }
        const fs::path tempInt = fs::path(
                                     QStandardPaths::writableLocation(QStandardPaths::TempLocation).toStdString())
            / (baseName + ".int");
        io::writeFile(tempInt, std::string(bytes->begin(), bytes->end()));
        intDiskPath = tempInt.string();
    }

    if (runDecompiler(decompilerPath, intDiskPath, sslTarget->string())) {
        files.refresh(); // the new .ssl should be visible to the VFS immediately
        _editorLauncher.openFileInWorkspace(QString::fromStdString(sslTarget->string()), QString());
    }
}

bool ScriptSourceService::runDecompiler(const QString& decompilerPath, const std::string& intDiskPath,
    const std::string& sslTarget) {
    const fs::path targetPath(sslTarget);
    if (targetPath.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(targetPath.parent_path(), ec);
    }

    const auto result = SslToolchain::decompile(decompilerPath, fs::path(intDiskPath), targetPath);
    if (result.success()) {
        return true;
    }

    QString detail;
    if (!result.started) {
        detail = QString("int2ssl could not be started.");
    } else if (result.timedOut) {
        detail = QString("int2ssl timed out.");
    } else {
        detail = QString("int2ssl exited with code %1.").arg(result.exitCode);
    }
    QtDialogs::showError(_dialogParent, "Decompile Script",
        detail + "\nSee the Log panel ([int2ssl]) for the tool output.");
    return false;
}

void ScriptSourceService::compileScript() {
    const auto writableRoot = _settings->resolveWritableDataPath();
    const QString startDir = writableRoot
        ? QString::fromStdString((*writableRoot / "scripts").string())
        : QString();

    const QString sslPath = QFileDialog::getOpenFileName(_dialogParent, "Compile Script",
        startDir, "SSL Scripts (*.ssl);;All Files (*)");
    if (sslPath.isEmpty()) {
        return;
    }

    const std::string baseName
        = resource::scriptBaseName(QFileInfo(sslPath).fileName().toStdString());
    if (baseName.empty()) {
        // e.g. a file literally named ".ssl" — without a program name there is nothing to derive
        // the scripts/<name>.int target from.
        QtDialogs::showError(_dialogParent, "Compile Script",
            QString("\"%1\" has no usable script name to derive the compiled .int from.")
                .arg(QFileInfo(sslPath).fileName()));
        return;
    }
    compileFileForProgram(sslPath, baseName);
}

ScriptSourceService::CompileToTargetResult ScriptSourceService::compileToTarget(
    const QString& compilerPath, const std::filesystem::path& sslPath, const std::filesystem::path& target) {
    CompileToTargetResult out;

    std::error_code ec;
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
    }

    // Compile to a fresh temporary sibling, never straight onto the deployed .int: sslc removes its
    // output file on any parse error (parse.c), so a syntax error in a recompile would otherwise
    // delete the last working bytecode. Same directory as the target so the final replace is an
    // atomic rename on one filesystem.
    fs::path tempOut = target;
    tempOut += ".sslc-tmp";
    fs::remove(tempOut, ec); // clear any stale temp so "exists afterwards" means "written by this run"

    const auto result = SslToolchain::compile(compilerPath, sslPath, tempOut);
    out.errors = ssl::countDiagnostics(result.diagnostics, ssl::DiagnosticSeverity::Error);
    out.warnings = ssl::countDiagnostics(result.diagnostics, ssl::DiagnosticSeverity::Warning);

    if (!result.started) {
        out.status = CompileToTargetResult::Status::NotStarted;
        return out;
    }
    if (result.timedOut) {
        fs::remove(tempOut, ec);
        out.status = CompileToTargetResult::Status::TimedOut;
        return out;
    }

    // Require a freshly written, non-empty output. This rejects a run that produced nothing (e.g. a
    // missing input reported as a warning with exit 0) that a stale pre-existing target would
    // otherwise pass off as success.
    std::error_code sizeEc;
    if (const bool producedOutput = fs::exists(tempOut, ec) && fs::file_size(tempOut, sizeEc) > 0 && !sizeEc;
        !result.success() || !producedOutput) {
        fs::remove(tempOut, ec); // discard any partial temp; the deployed .int was never touched
        out.status = CompileToTargetResult::Status::CompileFailed;
        return out;
    }

    // Good build — replace the deployed .int now. rename() replaces the target atomically on POSIX;
    // if a platform refuses to rename over an existing file, fall back to remove-then-rename.
    std::error_code renameEc;
    fs::rename(tempOut, target, renameEc);
    if (renameEc) {
        fs::remove(target, ec);
        fs::rename(tempOut, target, renameEc);
    }
    if (renameEc) {
        fs::remove(tempOut, ec);
        out.status = CompileToTargetResult::Status::PlaceFailed;
        return out;
    }

    out.status = CompileToTargetResult::Status::Success;
    return out;
}

bool ScriptSourceService::compileFileForProgram(const QString& sslPath, const std::string& baseName) {
    const QString compilerPath = ensureCompilerPath();
    if (compilerPath.isEmpty()) {
        return false;
    }

    auto& files = _resources.files();
    const auto target
        = resource::compiledScriptTarget(files, _settings->resolveWritableDataPath(), baseName);
    if (!target) {
        QtDialogs::showError(_dialogParent, "Compile Script",
            "There is no writable data folder to place the compiled .int in. Add a folder in "
            "Preferences › Data Paths.");
        return false;
    }

    const auto outcome = compileToTarget(compilerPath, fs::path(sslPath.toStdString()), *target);
    using enum CompileToTargetResult::Status;
    switch (outcome.status) {
        case NotStarted:
            QtDialogs::showError(_dialogParent, "Compile Script", "sslc could not be started.");
            return false;
        case TimedOut:
            QtDialogs::showError(_dialogParent, "Compile Script", "sslc timed out.");
            return false;
        case PlaceFailed:
            QtDialogs::showError(_dialogParent, "Compile Script",
                QString("Compiled successfully but could not place the output at %1.")
                    .arg(QString::fromStdString(target->string())));
            return false;
        case CompileFailed: {
            QString summary = QString("Compilation of %1 failed").arg(QFileInfo(sslPath).fileName());
            if (outcome.errors > 0) {
                summary += QString(" with %1 error(s)").arg(outcome.errors);
            }
            QtDialogs::showError(_dialogParent, "Compile Script",
                summary + ".\nSee the Log panel ([sslc]) for the compiler output.");
            return false;
        }
        case Success:
            break;
    }

    files.refresh(); // make the fresh .int visible to VFS lookups this session
    QString summary = QString("Compiled %1 → %2")
                          .arg(QFileInfo(sslPath).fileName(), QString::fromStdString(target->string()));
    if (outcome.warnings > 0) {
        summary += QString("\n%1 warning(s) — see the Log panel ([sslc]).").arg(outcome.warnings);
    }
    QtDialogs::showInfo(_dialogParent, "Compile Script", summary);
    return true;
}

void ScriptSourceService::decompileScript() {
    const QString intPath = QFileDialog::getOpenFileName(_dialogParent, "Decompile Script",
        QString(), "Compiled Scripts (*.int);;All Files (*)");
    if (intPath.isEmpty()) {
        return;
    }

    const QString decompilerPath = ensureDecompilerPath();
    if (decompilerPath.isEmpty()) {
        return;
    }

    const fs::path source(intPath.toStdString());
    fs::path target = source;
    target.replace_extension(".ssl");

    if (fs::exists(target)
        && !QtDialogs::showQuestion(_dialogParent, "Decompile Script",
            QString("%1 already exists.\n\nOverwrite it with the decompiled (lossy) source?")
                .arg(QString::fromStdString(target.string())))) {
        return;
    }

    if (runDecompiler(decompilerPath, source.string(), target.string())) {
        _resources.files().refresh();
        _editorLauncher.openFileInWorkspace(QString::fromStdString(target.string()), QString());
    }
}

} // namespace geck
