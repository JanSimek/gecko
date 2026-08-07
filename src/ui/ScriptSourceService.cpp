#include "ScriptSourceService.h"

#include "ExternalEditorLauncher.h"
#include "QtDialogs.h"
#include "Settings.h"
#include "format/lst/Lst.h"
#include "reader/ReaderExceptions.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "resource/ScriptSourceLocator.h"
#include "resource/WritableDataRoot.h"

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
        return false; // not under the source roots — let the caller try the in-VFS paths
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

    // 1) A loose .ssl in the mounted data: open it directly.
    if (const auto source = resource::locateScriptSource(files, baseName)) {
        if (!source->insideDat && !source->diskPath.empty()) {
            // A native path — openFile() would try to resolve it as a VFS path and fail, so open it
            // directly (no workspace: this loose file isn't part of a marked source tree).
            _editorLauncher.openFileInWorkspace(QString::fromStdString(source->diskPath.string()), QString());
            return;
        }

        // 2) The .ssl exists but only inside a DAT: extract an editable copy that shadows it.
        const auto writableRoot = _settings->resolveWritableDataPath();
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

    // 3) No .ssl source anywhere. Gecko does not decompile — the source has to come from a source
    //    tree. Point the user at "Mark as Script Source" (e.g. the Restoration Project's scripts_src).
    QtDialogs::showError(_dialogParent, "Edit Script",
        QString("No SSL source (scripts/%1.ssl) was found for \"%1\" in the mounted data.\n\n"
                "Add the script sources — e.g. the Restoration Project's scripts_src — as a data "
                "path and mark it with \"Mark as Script Source\" in Preferences › Data Paths.")
            .arg(QString::fromStdString(baseName)));
}

} // namespace geck
