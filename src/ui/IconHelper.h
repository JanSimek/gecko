#pragma once

#include <QIcon>
#include <QString>
#include <QApplication>
#include <QPalette>
#include <QPainter>
#include <QPixmap>
#include <QFile>
#include <QSvgRenderer>
#include <QHash>

namespace geck {

// Helper to load SVG with palette-aware colors
inline QByteArray loadThemedSvg(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }

    QByteArray svgData = file.readAll();
    file.close();

    // Replace black colors with palette color
    QPalette palette = QApplication::palette();
    QString textColor = palette.color(QPalette::WindowText).name();

    // Replace various black color formats
    svgData.replace("#000000", textColor.toUtf8());
    svgData.replace("stroke:black", QString("stroke:%1").arg(textColor).toUtf8());
    svgData.replace("fill:black", QString("fill:%1").arg(textColor).toUtf8());

    // Also replace currentColor
    svgData.replace("currentColor", textColor.toUtf8());

    return svgData;
}

// Cache icons to avoid re-rendering
inline QIcon createIcon(const QString& path) {
    static QHash<QString, QIcon> iconCache;
    static QPalette lastPalette;

    // Check if palette changed (theme switch)
    QPalette currentPalette = QApplication::palette();
    if (lastPalette != currentPalette) {
        iconCache.clear();
        lastPalette = currentPalette;
    }

    // Check cache first
    if (iconCache.contains(path)) {
        return iconCache[path];
    }

    // Load the SVG with theme colors
    QByteArray svgData = loadThemedSvg(path);
    if (svgData.isEmpty()) {
        QIcon icon(path); // Fallback to regular icon
        iconCache[path] = icon;
        return icon;
    }

    QIcon icon;

    // Create pixmaps at common sizes for normal mode
    for (int size : { 16, 22, 32 }) {
        QSvgRenderer renderer(svgData);
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        icon.addPixmap(pixmap, QIcon::Normal);
    }

    // For disabled mode - reload with disabled colors
    QByteArray disabledData = svgData;
    QString disabledColor = currentPalette.color(QPalette::Disabled, QPalette::WindowText).name();
    disabledData.replace(currentPalette.color(QPalette::WindowText).name().toUtf8(), disabledColor.toUtf8());

    for (int size : { 16, 22, 32 }) {
        QSvgRenderer renderer(disabledData);
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        icon.addPixmap(pixmap, QIcon::Disabled);
    }

    iconCache[path] = icon;
    return icon;
}

} // namespace geck