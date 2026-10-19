#pragma once

#include <QString>
#include <QWidget>
#include <QMessageBox>

namespace geck {

/**
 * @brief Common Qt dialog operations with consistent styling
 *
 * Provides convenient static methods for showing dialogs.
 */
class QtDialogs {
public:
    // Message dialogs
    static void showInfo(QWidget* parent, const QString& title, const QString& message);
    static void showWarning(QWidget* parent, const QString& title, const QString& message);
    static void showError(QWidget* parent, const QString& title, const QString& message);
    static bool showQuestion(QWidget* parent, const QString& title, const QString& message);

    // File dialogs
    static QString saveFile(QWidget* parent, const QString& title,
        const QString& filter = "All Files (*.*)",
        const QString& suggestedName = QString());
    static QString openMapFile(QWidget* parent, const QString& title = "Open Map File");

private:
    QtDialogs() = delete; // Static utility class

    // Helper methods
    static void setDialogIcon(QMessageBox* dialog);
    static QString getLastDirectory();
    static void setLastDirectory(const QString& dir);

    static QString s_lastDirectory;
};

} // namespace geck