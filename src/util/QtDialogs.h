#pragma once

#include <QString>
#include <QWidget>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QProgressDialog>
#include <QApplication>
#include <memory>

namespace geck {

/**
 * Utility class for common Qt dialog operations
 * Provides convenient static methods for showing dialogs with consistent styling
 */
class QtDialogs {
public:
    // Message dialogs
    static void showInfo(QWidget* parent, const QString& title, const QString& message);
    static void showWarning(QWidget* parent, const QString& title, const QString& message);
    static void showError(QWidget* parent, const QString& title, const QString& message);
    static bool showQuestion(QWidget* parent, const QString& title, const QString& message);

    // File dialogs
    static QString openFile(QWidget* parent, const QString& title,
        const QString& filter = "All Files (*.*)");
    static QStringList openFiles(QWidget* parent, const QString& title,
        const QString& filter = "All Files (*.*)");
    static QString saveFile(QWidget* parent, const QString& title,
        const QString& filter = "All Files (*.*)");
    static QString selectDirectory(QWidget* parent, const QString& title);

    // Input dialogs
    static QString getString(QWidget* parent, const QString& title,
        const QString& label, const QString& defaultValue = "");
    static int getInt(QWidget* parent, const QString& title, const QString& label,
        int defaultValue = 0, int min = -2147483647, int max = 2147483647);
    static double getDouble(QWidget* parent, const QString& title, const QString& label,
        double defaultValue = 0.0, double min = -2147483647, double max = 2147483647);

    // Progress dialogs
    static std::unique_ptr<QProgressDialog> createProgress(QWidget* parent,
        const QString& labelText,
        int minimum = 0, int maximum = 100);

    // Game-specific dialogs
    static QString openGameFile(QWidget* parent, const QString& title = "Open Game File");
    static QString openDatFile(QWidget* parent, const QString& title = "Open DAT Archive");
    static QString openMapFile(QWidget* parent, const QString& title = "Open Map File");
    static QString openProFile(QWidget* parent, const QString& title = "Open PRO File");

    // Utility methods
    static void centerOnScreen(QWidget* widget);
    static void setDialogIcon(QMessageBox* dialog);

private:
    QtDialogs() = delete; // Static utility class

    // Helper methods
    static QString getLastDirectory();
    static void setLastDirectory(const QString& dir);

    static QString s_lastDirectory;
};

} // namespace geck