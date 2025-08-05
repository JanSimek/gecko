#include "WelcomeWidget.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QSvgRenderer>
#include <QPainter>
#include <QCoreApplication>
#include <QFont>
#include <spdlog/spdlog.h>

#include "../../Application.h"
#include "version.h"

namespace geck {

WelcomeWidget::WelcomeWidget(QWidget* parent)
    : QWidget(parent)
    , _layout(nullptr)
    , _imageLabel(nullptr)
    , _versionLabel(nullptr)
{
    setupUI();
}

void WelcomeWidget::setupUI() {
    _layout = new QVBoxLayout(this);
    _layout->setContentsMargins(0, 0, 10, 0);
    _layout->setSpacing(0);
    
    // Create label for the Vault Boy image
    _imageLabel = new QLabel();
    _imageLabel->setAlignment(Qt::AlignCenter);
    // Make background fully transparent
    _imageLabel->setStyleSheet("QLabel { background-color: transparent; }");
    
    // Load Vault Boy SVG using application's resource path method
    std::filesystem::path svgPath = Application::getResourcesPath() / "images" / "vault-boy.svg";
    QString svgPathStr = QString::fromStdString(svgPath.string());
    
    QSvgRenderer svgRenderer;
    if (svgRenderer.load(svgPathStr)) {
        renderSvgToLabel(svgRenderer);
    }
    createVersionLabel();

    // Add stretching to center the content vertically and horizontally
    _layout->addStretch();
    _layout->addWidget(_imageLabel, 0, Qt::AlignCenter);
    if (_versionLabel) {
        _layout->addWidget(_versionLabel, 0, Qt::AlignCenter);
    }
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

void WelcomeWidget::createVersionLabel() {
    _versionLabel = new QLabel();
    _versionLabel->setText(QString("Welcome to %1 v%2")
                          .arg(geck::version::name)
                          .arg(geck::version::string));
    _versionLabel->setAlignment(Qt::AlignCenter);
    
    // Set monospace bold font
    QFont font("Monaco, Consolas, 'Courier New', monospace");
    font.setBold(true);
    font.setPointSize(12);
    _versionLabel->setFont(font);
    
    // Style the label
    _versionLabel->setStyleSheet(
        "QLabel { "
        "background-color: transparent; "
        "}"
    );
}

} // namespace geck