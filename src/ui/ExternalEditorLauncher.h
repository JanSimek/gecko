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

class Settings;

/**
 * @brief Service that opens text files from the file browser in an external editor.
 *
 * Extracted from MainWindow. Reads the requested file (preferring the native path,
 * falling back to a temporary copy from the VFS) and opens it with the user's
 * configured custom editor, falling back to the system default editor.
 */
class ExternalEditorLauncher {
public:
    ExternalEditorLauncher(resource::GameResources& resources, std::shared_ptr<Settings> settings,
        QWidget* dialogParent);

    static bool isTextFile(const QString& filePath);

    void openFile(const QString& vfsFilePath);

    /// Open a native file with the configured editor, opening `workspaceRoot` as the editor's
    /// workspace/project folder first. For VS Code this is `code <root> <file>`, which lets an
    /// SSL extension (BGforge MLS) scan the workspace for headers and drive compilation. Falls back
    /// to opening just the file (custom editor with no root, or the system default) when no custom
    /// editor is configured. `workspaceRoot` may be empty to open the file with no workspace.
    void openFileInWorkspace(const QString& nativeFilePath, const QString& workspaceRoot);

private:
    resource::GameResources& _resources;
    std::shared_ptr<Settings> _settings;
    QWidget* _dialogParent;
};

} // namespace geck
