#include "QtDialogs.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QScreen>
#include <QSettings>
#include <spdlog/spdlog.h>

namespace geck {

QString QtDialogs::s_lastDirectory = QString();

void QtDialogs::showInfo(QWidget* parent, const QString& title, const QString& message) {
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(title);
    msgBox.setText(message);
    msgBox.setIcon(QMessageBox::Information);
    setDialogIcon(&msgBox);
    msgBox.exec();
}

void QtDialogs::showWarning(QWidget* parent, const QString& title, const QString& message) {
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(title);
    msgBox.setText(message);
    msgBox.setIcon(QMessageBox::Warning);
    setDialogIcon(&msgBox);
    msgBox.exec();
}

void QtDialogs::showError(QWidget* parent, const QString& title, const QString& message) {
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(title);
    msgBox.setText(message);
    msgBox.setIcon(QMessageBox::Critical);
    setDialogIcon(&msgBox);
    msgBox.exec();

    spdlog::error("Dialog error shown: {} - {}", title.toStdString(), message.toStdString());
}

bool QtDialogs::showQuestion(QWidget* parent, const QString& title, const QString& message) {
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(title);
    msgBox.setText(message);
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    setDialogIcon(&msgBox);

    return msgBox.exec() == QMessageBox::Yes;
}

QString QtDialogs::openFile(QWidget* parent, const QString& title, const QString& filter) {
    QString fileName = QFileDialog::getOpenFileName(parent, title, getLastDirectory(), filter);
    if (!fileName.isEmpty()) {
        setLastDirectory(QFileInfo(fileName).absolutePath());
    }
    return fileName;
}

QStringList QtDialogs::openFiles(QWidget* parent, const QString& title, const QString& filter) {
    QStringList fileNames = QFileDialog::getOpenFileNames(parent, title, getLastDirectory(), filter);
    if (!fileNames.isEmpty()) {
        setLastDirectory(QFileInfo(fileNames.first()).absolutePath());
    }
    return fileNames;
}

QString QtDialogs::saveFile(QWidget* parent, const QString& title, const QString& filter, const QString& suggestedName) {
    // Seed the dialog with the last directory and, when given, a default file name
    // (e.g. the current map). getLastDirectory() always returns a directory, so the
    // suggested name is never prefixed with a stale file path.
    QString initialPath = getLastDirectory();
    if (!suggestedName.isEmpty()) {
        initialPath = QDir(initialPath).filePath(suggestedName);
    }

    QString fileName = QFileDialog::getSaveFileName(parent, title, initialPath, filter);
    if (!fileName.isEmpty()) {
        setLastDirectory(QFileInfo(fileName).absolutePath());
    }
    return fileName;
}

QString QtDialogs::selectDirectory(QWidget* parent, const QString& title) {
    QString dirName = QFileDialog::getExistingDirectory(parent, title, getLastDirectory());
    if (!dirName.isEmpty()) {
        setLastDirectory(dirName);
    }
    return dirName;
}

QString QtDialogs::getString(QWidget* parent, const QString& title,
    const QString& label, const QString& defaultValue) {
    bool ok;
    QString text = QInputDialog::getText(parent, title, label, QLineEdit::Normal, defaultValue, &ok);
    return ok ? text : QString();
}

int QtDialogs::getInt(QWidget* parent, const QString& title, const QString& label,
    int defaultValue, int min, int max) {
    bool ok;
    int value = QInputDialog::getInt(parent, title, label, defaultValue, min, max, 1, &ok);
    return ok ? value : defaultValue;
}

double QtDialogs::getDouble(QWidget* parent, const QString& title, const QString& label,
    double defaultValue, double min, double max) {
    bool ok;
    double value = QInputDialog::getDouble(parent, title, label, defaultValue, min, max, 2, &ok);
    return ok ? value : defaultValue;
}

std::unique_ptr<QProgressDialog> QtDialogs::createProgress(QWidget* parent,
    const QString& labelText,
    int minimum, int maximum) {
    auto progress = std::make_unique<QProgressDialog>(labelText, "Cancel", minimum, maximum, parent);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(1000); // Show after 1 second
    return progress;
}

QString QtDialogs::openGameFile(QWidget* parent, const QString& title) {
    QString filter = "All Game Files (*.dat *.pro *.frm *.map *.msg *.gam *.lst);;"
                     "DAT Archives (*.dat);;"
                     "PRO Objects (*.pro);;"
                     "FRM Animations (*.frm);;"
                     "Map Files (*.map);;"
                     "Message Files (*.msg);;"
                     "Game Files (*.gam);;"
                     "List Files (*.lst);;"
                     "All Files (*.*)";
    return openFile(parent, title, filter);
}

QString QtDialogs::openDatFile(QWidget* parent, const QString& title) {
    return openFile(parent, title, "DAT Archives (*.dat);;All Files (*.*)");
}

QString QtDialogs::openMapFile(QWidget* parent, const QString& title) {
    return openFile(parent, title, "Map Files (*.map);;All Files (*.*)");
}

QString QtDialogs::openProFile(QWidget* parent, const QString& title) {
    return openFile(parent, title, "PRO Objects (*.pro);;All Files (*.*)");
}

void QtDialogs::centerOnScreen(QWidget* widget) {
    if (!widget)
        return;

    QScreen* screen = QApplication::primaryScreen();
    if (!screen)
        return;

    QRect screenGeometry = screen->geometry();
    int x = (screenGeometry.width() - widget->width()) / 2;
    int y = (screenGeometry.height() - widget->height()) / 2;
    widget->move(x, y);
}

void QtDialogs::setDialogIcon(QMessageBox* dialog) {
    if (QApplication::windowIcon().isNull() == false) {
        dialog->setWindowIcon(QApplication::windowIcon());
    }
}

QString QtDialogs::getLastDirectory() {
    if (s_lastDirectory.isEmpty()) {
        QSettings settings;
        s_lastDirectory = settings.value("lastDirectory",
                                      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                              .toString();
    }
    // Defensive: earlier versions stored a file path here (a file's path, not its
    // directory). If so, fall back to the containing directory so save dialogs do
    // not pre-fill an unrelated file name.
    QFileInfo info(s_lastDirectory);
    if (info.exists() && info.isFile()) {
        return info.absolutePath();
    }
    return s_lastDirectory;
}

void QtDialogs::setLastDirectory(const QString& dir) {
    s_lastDirectory = dir;
    QSettings settings;
    settings.setValue("lastDirectory", dir);
}

} // namespace geck