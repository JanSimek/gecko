#pragma once

#include <QString>
#include <memory>
#include <string>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace geck {

namespace resource {
    class GameResources;
}

class ExternalEditorLauncher;
class Settings;

/// @brief Service connecting scripts.lst program indices to their editable SSL source.
///
/// Bridges the data model (a 0-based scripts.lst program index, as stored in the map header's
/// script_id and each MapScript.script_id) to the .ssl the user edits: given a program index it
/// resolves scripts.lst → base name → the matching `<name>.ssl` (in a marked script-source tree,
/// loose in the mounted data, or extracted from a DAT) and opens it in the configured editor.
///
/// Gecko does not compile or decompile — that is owned by the external editor (VS Code + the
/// BGforge MLS extension, which bundles the compiler). See the Text Editor preferences for how a
/// compiled .int is deployed where the engine loads it.
class ScriptSourceService {
public:
    ScriptSourceService(resource::GameResources& resources, std::shared_ptr<Settings> settings,
        ExternalEditorLauncher& editorLauncher, QWidget* dialogParent);

    /// Open the .ssl source behind the 0-based scripts.lst `programIndex` in the user's editor.
    void editScriptSource(int programIndex);

private:
    /// The scripts.lst entry at `programIndex` reduced to its bare program name ("artemple"),
    /// or an empty string (with an error dialog shown) when it can't be resolved.
    std::string resolveBaseName(int programIndex);

    /// Open `<baseName>.ssl` from a marked script-source tree (with the tree as the editor's
    /// workspace) when one is configured and holds it. Returns true when it handled the request
    /// (opened the file); false to fall through to the in-VFS / DAT-extract handling.
    bool openFromScriptSourceRoots(const std::string& baseName);

    resource::GameResources& _resources;
    std::shared_ptr<Settings> _settings;
    ExternalEditorLauncher& _editorLauncher;
    QWidget* _dialogParent;
};

} // namespace geck
