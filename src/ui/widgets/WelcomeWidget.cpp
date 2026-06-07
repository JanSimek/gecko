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
#include "../IconHelper.h"
#include "../theme/ThemeManager.h"
#include "../UIConstants.h"
#include "version.h"

namespace geck {

WelcomeWidget::WelcomeWidget(QWidget* parent)
    : QWidget(parent)
    , _layout(nullptr)
    , _imageLabel(nullptr)
    , _versionLabel(nullptr) {
    setupUI();
}

void WelcomeWidget::setupUI() {
    _layout = new QVBoxLayout(this);
    _layout->setContentsMargins(0, 0, 10, 0);
    _layout->setSpacing(0);

    _imageLabel = new QLabel();
    _imageLabel->setAlignment(Qt::AlignCenter);
    _imageLabel->setStyleSheet(ui::theme::styles::transparentWidget());

    std::filesystem::path svgPath = Application::getResourcesPath() / "images" / "vault-boy.svg";
    QString svgPathStr = QString::fromStdString(svgPath.string());

    QByteArray svgData = loadThemedSvg(svgPathStr);
    if (!svgData.isEmpty()) {
        QSvgRenderer svgRenderer(svgData);
        renderSvgToLabel(svgRenderer);
    }
    createVersionLabel();

    _layout->addStretch();
    _layout->addWidget(_imageLabel, 0, Qt::AlignCenter);
    if (_versionLabel) {
        _layout->addWidget(_versionLabel, 0, Qt::AlignCenter);
    }
    _layout->addStretch();
}

void WelcomeWidget::renderSvgToLabel(QSvgRenderer& svgRenderer) {
    QSize svgSize = svgRenderer.defaultSize();

    // Source SVG is 210mm x 297mm (portrait); scale to max 400px height, keeping aspect ratio
    int maxHeight = 400;
    QSize renderSize = svgSize.scaled(QSize(maxHeight * svgSize.width() / svgSize.height(), maxHeight), Qt::KeepAspectRatio);

    QPixmap pixmap(renderSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    svgRenderer.render(&painter);

    _imageLabel->setPixmap(pixmap);
    _imageLabel->setScaledContents(false);
    // Remove size constraints so the label can center properly
    _imageLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    _imageLabel->setMinimumSize(0, 0);
}

void WelcomeWidget::createVersionLabel() {
    _versionLabel = new QLabel();
    _versionLabel->setText(QString("Welcome to %1 v%2")
            .arg(geck::version::name)
            .arg(geck::version::string));
    _versionLabel->setAlignment(Qt::AlignCenter);

    QFont font("Monaco, Consolas, 'Courier New', monospace");
    font.setBold(true);
    font.setPointSize(ui::constants::fonts::SIZE_TITLE);
    _versionLabel->setFont(font);

    _versionLabel->setStyleSheet(
        "QLabel { "
        "background-color: transparent; "
        "}");
}

} // namespace geck