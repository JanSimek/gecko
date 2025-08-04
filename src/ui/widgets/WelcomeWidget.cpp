#include "WelcomeWidget.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QSvgRenderer>
#include <QPainter>
#include <QCoreApplication>
#include <spdlog/spdlog.h>

namespace geck {

WelcomeWidget::WelcomeWidget(QWidget* parent)
    : QWidget(parent)
    , _layout(nullptr)
    , _imageLabel(nullptr)
{
    setupUI();
}

void WelcomeWidget::setupUI() {
    _layout = new QVBoxLayout(this);
    _layout->setContentsMargins(0, 0, 0, 0);
    _layout->setSpacing(0);
    
    // Create label for the Vault Boy image
    _imageLabel = new QLabel();
    _imageLabel->setAlignment(Qt::AlignCenter);
    // Make background fully transparent
    _imageLabel->setStyleSheet("QLabel { background-color: transparent; }");
    
    // Try to load and display the Vault Boy SVG
    // Try multiple possible paths since working directory may vary
    QStringList possiblePaths = {
        ":/icons/welcome/vault-boy.svg",                    // Qt resource system (preferred)
        "resources/images/vault-boy.svg",                    // From build directory
        "../resources/images/vault-boy.svg",                // From src directory
        "../../resources/images/vault-boy.svg",             // From deeper subdirectory
        // macOS app bundle paths
        QCoreApplication::applicationDirPath() + "/../Resources/resources/images/vault-boy.svg",
        QCoreApplication::applicationDirPath() + "/resources/images/vault-boy.svg"
    };
    
    QSvgRenderer svgRenderer;
    bool loaded = false;
    QString usedPath;
    
    for (const QString& path : possiblePaths) {
        spdlog::debug("Trying to load Vault Boy SVG from: {}", path.toStdString());
        if (svgRenderer.load(path)) {
            loaded = true;
            usedPath = path;
            break;
        }
    }
    
    if (loaded) {
        spdlog::info("Successfully loaded Vault Boy SVG from: {}", usedPath.toStdString());
        renderSvgToLabel(svgRenderer);
    } else {
        spdlog::warn("Failed to load Vault Boy SVG from any of the attempted paths");
        // Fallback to a styled text message
        _imageLabel->setText("Welcome to GECK Map Editor\n\nUse File > New Map or File > Open Map to get started");
        _imageLabel->setWordWrap(true);
        _imageLabel->setStyleSheet(
            "QLabel { "
            "color: #555; "
            "font-size: 18px; "
            "font-weight: bold; "
            "background-color: #f8f8f8; "
            "padding: 40px; "
            "border-radius: 10px; "
            "border: 2px solid #ddd; "
            "}"
        );
    }
    
    // Add stretching to center the image vertically and horizontally
    _layout->addStretch();
    _layout->addWidget(_imageLabel, 0, Qt::AlignCenter);
    _layout->addStretch();
}

void WelcomeWidget::renderSvgToLabel(QSvgRenderer& svgRenderer) {
    // Get the default size from the SVG (maintains original aspect ratio)
    QSize svgSize = svgRenderer.defaultSize();
    
    // Scale to a reasonable size while maintaining aspect ratio
    // Original is 210mm x 297mm (portrait), scale to max 400px height
    int maxHeight = 400;
    QSize renderSize = svgSize.scaled(QSize(maxHeight * svgSize.width() / svgSize.height(), maxHeight), Qt::KeepAspectRatio);
    
    QPixmap pixmap(renderSize);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    svgRenderer.render(&painter);
    
    _imageLabel->setPixmap(pixmap);
    _imageLabel->setScaledContents(false); // Keep original size/aspect ratio
    // Remove size constraints to allow proper centering
    _imageLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    _imageLabel->setMinimumSize(0, 0);
}

} // namespace geck