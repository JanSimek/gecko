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

QString PatternLibrary::customDir() {
    const QString customPath = QDir(rootDir()).filePath(QStringLiteral("Custom"));
    QDir().mkpath(customPath);
    return customPath;
}

} // namespace geck::pattern
