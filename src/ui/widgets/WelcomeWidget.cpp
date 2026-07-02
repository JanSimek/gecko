#include "WelcomeWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSvgRenderer>
#include <QPainter>
#include <QCoreApplication>
#include <QFont>
#include <spdlog/spdlog.h>

#include "Application.h"
#include "ui/IconHelper.h"
#include "ui/theme/ThemeManager.h"
#include "ui/UIConstants.h"
#include "version.h"

namespace geck {

WelcomeWidget::WelcomeWidget(QWidget* parent)
    : QWidget(parent)
    , _layout(nullptr)
    , _imageLabel(nullptr)
    , _versionLabel(nullptr)
    , _newMapButton(nullptr)
    , _browseButton(nullptr) {
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
    createActionButtons();
    _layout->addStretch();
}

void WelcomeWidget::createActionButtons() {
    // Give the no-map screen a way to act: create a map or open the browser, mirroring the
    // File menu's New Map / Browse Maps actions. MainWindow connects these signals to those handlers.
    _newMapButton = new QPushButton(createIcon(":/icons/actions/new.svg"), "New Map", this);
    _browseButton = new QPushButton(createIcon(":/icons/actions/open.svg"), "Browse Maps…", this);
    for (QPushButton* button : { _newMapButton, _browseButton }) {
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumWidth(150);
    }

    connect(_newMapButton, &QPushButton::clicked, this, &WelcomeWidget::newMapRequested);
    connect(_browseButton, &QPushButton::clicked, this, &WelcomeWidget::browseMapsRequested);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    buttonRow->addWidget(_newMapButton);
    buttonRow->addSpacing(ui::theme::spacing::NORMAL);
    buttonRow->addWidget(_browseButton);
    buttonRow->addStretch();

    _layout->addSpacing(ui::theme::spacing::LOOSE);
    _layout->addLayout(buttonRow);
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