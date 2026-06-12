#include "pattern/PatternLibrary.h"

#include <QDir>
#include <QStandardPaths>

namespace geck::pattern {

QString PatternLibrary::rootDir() {
    // Mirror Settings' location convention: <ConfigLocation>/gecko/<...>.
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    const QString patternsPath = QDir(configPath).filePath(QStringLiteral("gecko/patterns"));
    QDir().mkpath(patternsPath);
    return patternsPath;
}

} // namespace geck::pattern
