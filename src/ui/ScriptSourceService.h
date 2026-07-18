#pragma once

#include <QString>
#include <memory>

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
/// script_id and each MapScript.script_id) to files the user can work on:
///
///  - editScriptSource(): open the script's .ssl in the configured editor — extracting it from
///    a DAT into the writable data folder, or decompiling the .int via int2ssl (best-effort,
///    lossy), when no loose source exists.
///  - compileScript() / decompileScript(): file-picker driven sslc / int2ssl runs, placing the
///    compiled .int where the engine (and our VFS) will load it.
///
/// Tool binaries come from Settings; when one is missing the user is prompted to locate it.
/// Tool output lands in the Log panel (tagged [sslc] / [int2ssl]).
class ScriptSourceService {
public:
    ScriptSourceService(resource::GameResources& resources, std::shared_ptr<Settings> settings,
        ExternalEditorLauncher& editorLauncher, QWidget* dialogParent);

    /// Open the .ssl source behind the 0-based scripts.lst `programIndex` in the user's editor.
    void editScriptSource(int programIndex);

    /// Pick a .ssl file and compile it with sslc, placing the .int under the engine's scripts/.
    void compileScript();

    /// Pick an .int file and decompile it with int2ssl to an .ssl next to it.
    void decompileScript();

private:
    /// The scripts.lst entry at `programIndex` reduced to its bare program name ("artemple"),
    /// or an empty string (with an error dialog shown) when it can't be resolved.
    std::string resolveBaseName(int programIndex);

    /// The configured, existing path of a tool binary — prompting the user to locate (and
    /// persist) it when unset or stale. Empty when unavailable.
    QString ensureCompilerPath();
    QString ensureDecompilerPath();

    /// Run sslc on `sslPath`, placing the output at scripts/<baseName>.int, and report the
    /// outcome. Returns true on success.
    bool compileFileForProgram(const QString& sslPath, const std::string& baseName);

    /// Decompile `intVfsOrDiskPath` (native path) to `sslTarget` and open the result on success.
    bool runDecompiler(const QString& decompilerPath, const std::string& intDiskPath,
        const std::string& sslTarget);

    resource::GameResources& _resources;
    std::shared_ptr<Settings> _settings;
    ExternalEditorLauncher& _editorLauncher;
    QWidget* _dialogParent;
};

} // namespace geck
