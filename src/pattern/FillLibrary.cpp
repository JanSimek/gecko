#include "pattern/FillLibrary.h"

#include <QDir>
#include <QStandardPaths>

#include "Application.h"

namespace geck::pattern {

QString FillLibrary::rootDir() {
    // Mirror Settings/PatternLibrary: <ConfigLocation>/gecko/<...>.
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    const QString fillsPath = QDir(configPath).filePath(QStringLiteral("gecko/fills"));
    QDir().mkpath(fillsPath);
    return fillsPath;
}

QString FillLibrary::bundledDir() {
    // The same place the script console finds bundled stamps: <resources>/scripts/<...>.
    return QString::fromStdString((Application::getResourcesPath() / "scripts" / "fills").string());
}

} // namespace geck::pattern
