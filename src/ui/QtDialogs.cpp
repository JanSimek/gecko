#include "QtDialogs.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
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

QString QtDialogs::openMapFile(QWidget* parent, const QString& title) {
    QString fileName = QFileDialog::getOpenFileName(parent, title, getLastDirectory(), "Map Files (*.map);;All Files (*.*)");
    if (!fileName.isEmpty()) {
        setLastDirectory(QFileInfo(fileName).absolutePath());
    }
    return fileName;
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
    return s_lastDirectory;
}

void QtDialogs::setLastDirectory(const QString& dir) {
    s_lastDirectory = dir;
    QSettings settings;
    settings.setValue("lastDirectory", dir);
}

} // namespace geck