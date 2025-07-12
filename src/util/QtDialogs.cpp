#include "QtDialogs.h"

#include <QApplication>
#include <QFileDialog>
#include <QString>
#include <QDir>
#include <spdlog/spdlog.h>

namespace geck {

void QtDialogs::ensureQtApplication() {
    // Check if QApplication exists, create a minimal one if not
    if (!QApplication::instance()) {
        static int argc = 1;
        static char* argv[] = {"geck-mapper", nullptr};
        static QApplication app(argc, argv);
        spdlog::debug("Created minimal QApplication for dialogs");
    }
}

QString QtDialogs::formatFilters(const std::vector<std::pair<std::string, std::string>>& filters) {
    if (filters.empty()) {
        return "All Files (*)";
    }
    
    QStringList filterList;
    for (const auto& [name, pattern] : filters) {
        filterList << QString("%1 (%2)").arg(QString::fromStdString(name), QString::fromStdString(pattern));
    }
    
    // Add "All Files" option at the end
    filterList << "All Files (*)";
    
    return filterList.join(";;");
}

std::string QtDialogs::openFile(const std::string& title, 
                               const std::string& default_path,
                               const std::vector<std::pair<std::string, std::string>>& filters) {
    ensureQtApplication();
    
    QString qTitle = QString::fromStdString(title);
    QString qDefaultPath = QString::fromStdString(default_path);
    QString qFilters = formatFilters(filters);
    
    QString selectedFile = QFileDialog::getOpenFileName(
        nullptr,
        qTitle,
        qDefaultPath,
        qFilters
    );
    
    if (selectedFile.isEmpty()) {
        spdlog::debug("User cancelled file open dialog");
        return "";
    }
    
    std::string result = selectedFile.toStdString();
    spdlog::debug("User selected file: {}", result);
    return result;
}

std::string QtDialogs::saveFile(const std::string& title,
                               const std::string& default_path,
                               const std::vector<std::pair<std::string, std::string>>& filters,
                               bool force_overwrite) {
    ensureQtApplication();
    
    QString qTitle = QString::fromStdString(title);
    QString qDefaultPath = QString::fromStdString(default_path);
    QString qFilters = formatFilters(filters);
    
    QFileDialog dialog(nullptr, qTitle, qDefaultPath, qFilters);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    
    if (!force_overwrite) {
        dialog.setOption(QFileDialog::DontConfirmOverwrite, false);
    } else {
        dialog.setOption(QFileDialog::DontConfirmOverwrite, true);
    }
    
    if (dialog.exec() == QDialog::Accepted) {
        QStringList selectedFiles = dialog.selectedFiles();
        if (!selectedFiles.isEmpty()) {
            std::string result = selectedFiles.first().toStdString();
            spdlog::debug("User selected save file: {}", result);
            return result;
        }
    }
    
    spdlog::debug("User cancelled file save dialog");
    return "";
}

std::string QtDialogs::selectFolder(const std::string& title,
                                   const std::string& default_path) {
    ensureQtApplication();
    
    QString qTitle = QString::fromStdString(title);
    QString qDefaultPath = QString::fromStdString(default_path);
    
    QString selectedDir = QFileDialog::getExistingDirectory(
        nullptr,
        qTitle,
        qDefaultPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    
    if (selectedDir.isEmpty()) {
        spdlog::debug("User cancelled folder selection dialog");
        return "";
    }
    
    std::string result = selectedDir.toStdString();
    spdlog::debug("User selected folder: {}", result);
    return result;
}

} // namespace geck