#include "ExternalEditorLauncher.h"

#include "resource/GameResources.h"
#include "ui/GameEnums.h"
#include "Settings.h"
#include "QtDialogs.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryFile>
#include <QUrl>
#include <spdlog/spdlog.h>

namespace geck {

ExternalEditorLauncher::ExternalEditorLauncher(resource::GameResources& resources,
    std::shared_ptr<Settings> settings, QWidget* dialogParent)
    : _resources(resources)
    , _settings(std::move(settings))
    , _dialogParent(dialogParent) {
}

bool ExternalEditorLauncher::isTextFile(const QString& filePath) {
    QString suffix = QFileInfo(filePath).suffix().toLower();
    return game::enums::textFileExtensions().contains(suffix);
}

void ExternalEditorLauncher::openFile(const QString& vfsFilePath) {
    try {
        auto& settings = *_settings;
        auto editorMode = settings.getTextEditorMode();
        QString customEditorPath = settings.getCustomEditorPath();

        spdlog::debug("ExternalEditorLauncher: Opening text file with {} editor: {}",
            (editorMode == Settings::TextEditorMode::CUSTOM) ? "custom" : "system",
            vfsFilePath.toStdString());

        auto fileData = _resources.files().readRawBytes(vfsFilePath.toStdString());
        if (!fileData) {
            QtDialogs::showError(_dialogParent, "Error",
                QString("Failed to open file: %1").arg(vfsFilePath));
            return;
        }

        // Create temporary file with same extension (if needed)
        QFileInfo fileInfo(vfsFilePath);
        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString tempFileName = QString("%1_XXXXXX.%2")
                                   .arg(fileInfo.baseName())
                                   .arg(fileInfo.suffix());
        QString tempFilePath = tempDir + "/" + tempFileName;

        QString targetFilePath;
        bool usedTemporaryFile = false;

        // Prefer the original path from the browser; only fall back to a temp copy if it isn't a readable file
        QFileInfo requestedPathInfo(vfsFilePath);
        if (requestedPathInfo.exists() && requestedPathInfo.isReadable() && requestedPathInfo.isFile()) {
            targetFilePath = requestedPathInfo.absoluteFilePath();
            spdlog::debug("ExternalEditorLauncher: Opening native file directly at {}", targetFilePath.toStdString());
        } else {
            QTemporaryFile tempFile(tempFilePath);
            tempFile.setAutoRemove(false);

            if (!tempFile.open()) {
                QtDialogs::showError(_dialogParent, "Error",
                    QString("Failed to create temporary file for: %1").arg(vfsFilePath));
                return;
            }

            tempFile.write(reinterpret_cast<const char*>(fileData->data()), static_cast<qsizetype>(fileData->size()));
            tempFile.close();

            targetFilePath = tempFile.fileName();
            usedTemporaryFile = true;
        }

        if (targetFilePath.isEmpty()) {
            QtDialogs::showError(_dialogParent, "Error",
                QString("Failed to resolve path for: %1").arg(vfsFilePath));
            return;
        }

        bool opened = false;
        bool customAttempted = false;

        if (editorMode == Settings::TextEditorMode::CUSTOM && !customEditorPath.isEmpty()) {
            customAttempted = true;
            QStringList arguments;
            arguments << targetFilePath;

            opened = QProcess::startDetached(customEditorPath, arguments);

            if (opened) {
                spdlog::debug("ExternalEditorLauncher: Successfully opened file with custom editor: {} -> {}",
                    customEditorPath.toStdString(), targetFilePath.toStdString());
            } else {
                spdlog::warn("ExternalEditorLauncher: Failed to start custom editor: {}", customEditorPath.toStdString());
            }
        }

        // Fall back to the system default editor if the custom one failed or isn't configured
        if (!opened) {
            QUrl fileUrl = QUrl::fromLocalFile(targetFilePath);
            opened = QDesktopServices::openUrl(fileUrl);

            if (!opened) {
                QString errorText = customAttempted
                    ? QString("Failed to open file with custom editor (%1) or system default.").arg(customEditorPath)
                    : QString("Failed to open file with system default editor.");
                QtDialogs::showError(_dialogParent, "Error", errorText);

                if (usedTemporaryFile) {
                    QFile::remove(targetFilePath);
                }
            } else {
                spdlog::debug("ExternalEditorLauncher: Successfully opened file with system default editor: {}", targetFilePath.toStdString());
            }
        }

    } catch (const std::exception& e) {
        QtDialogs::showError(_dialogParent, "Error",
            QString("Failed to open text file: %1").arg(e.what()));
    }
}

void ExternalEditorLauncher::openFileInWorkspace(const QString& nativeFilePath, const QString& workspaceRoot) {
    auto& settings = *_settings;
    const bool useCustom = settings.getTextEditorMode() == Settings::TextEditorMode::CUSTOM
        && !settings.getCustomEditorPath().isEmpty();

    if (useCustom) {
        const QString editor = settings.getCustomEditorPath();
        // `<editor> <workspaceRoot> <file>` — VS Code opens (or reuses) the folder as its workspace
        // and reveals the file; other editors that don't understand a folder arg still open the file.
        QStringList arguments;
        if (!workspaceRoot.isEmpty()) {
            arguments << workspaceRoot;
        }
        arguments << nativeFilePath;

        if (QProcess::startDetached(editor, arguments)) {
            spdlog::debug("ExternalEditorLauncher: opened {} (workspace {}) in custom editor {}",
                nativeFilePath.toStdString(), workspaceRoot.toStdString(), editor.toStdString());
            return;
        }
        spdlog::warn("ExternalEditorLauncher: custom editor {} failed to start; falling back to system default",
            editor.toStdString());
    }

    // No custom editor configured (or it failed): open just the file with the system default. This
    // can't carry a workspace folder, so an SSL extension won't see the headers — the Text Editor
    // preferences nudge the user to configure VS Code for the full flow.
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(nativeFilePath))) {
        QtDialogs::showError(_dialogParent, "Edit Script",
            QString("Failed to open the script source:\n%1").arg(nativeFilePath));
    }
}

} // namespace geck
