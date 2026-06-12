#include "ObjectPreviewWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QApplication>
#include <QStyle>
#include <QResizeEvent>
#include <spdlog/spdlog.h>

#include <vector>

#include "AnimationLayout.h"
#include "format/frm/Frm.h"
#include "format/frm/Frame.h"
#include "format/pal/Pal.h"
#include "resource/GameResources.h"
#include "ui/FrmThumbnailGenerator.h"
#include "ui/IconHelper.h"
#include "ui/theme/ThemeManager.h"
#include "ui/UIConstants.h"
#include "reader/ReaderFactory.h"

namespace geck {

ObjectPreviewWidget::ObjectPreviewWidget(resource::GameResources& resources, QWidget* parent, PreviewOptions options, const QSize& previewSize, double scaleFactor)
    : QWidget(parent)
    , _previewLabel(nullptr)
    , _titleLabel(nullptr)
    , _fidWidget(nullptr)
    , _fidButton(nullptr)
    , _playPauseButton(nullptr)
    , _rotateButton(nullptr)
    , _editButton(nullptr)
    , _animationController(new AnimationController(this))
    , _currentDirection(0)
    , _totalDirections(0)
    , _currentFid(0)
    , _resources(resources)
    , _options(options)
    , _customPreviewSize(previewSize)
    , _scaleFactor(scaleFactor) {
    setupUI();
}

void ObjectPreviewWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    _previewLabel = new QLabel();
    _previewLabel->setAlignment(Qt::AlignCenter);

    if (!_customPreviewSize.isEmpty()) {
        _previewLabel->setMinimumSize(_customPreviewSize);
        _previewLabel->setMaximumSize(_customPreviewSize);
    } else {
        _previewLabel->setMinimumHeight(PREVIEW_MIN_HEIGHT);
        _previewLabel->setMaximumHeight(PREVIEW_MAX_HEIGHT);
        _previewLabel->setMinimumWidth(PREVIEW_MIN_WIDTH);
    }

    _previewLabel->setScaledContents(false);
    _previewLabel->setStyleSheet(ui::theme::styles::previewArea());
    _previewLabel->setText("No FRM loaded");
    mainLayout->addWidget(_previewLabel);

    _titleLabel = new QLabel();
    _titleLabel->setAlignment(Qt::AlignCenter);
    _titleLabel->setStyleSheet(ui::theme::styles::compactLabel());
    _titleLabel->hide();
    mainLayout->addWidget(_titleLabel);

    if (_options & ShowFidField) {
        _fidWidget = new QWidget();
        QHBoxLayout* fidLayout = new QHBoxLayout(_fidWidget);
        fidLayout->setContentsMargins(0, 0, 0, 0);

        QLabel* fidTextLabel = new QLabel("FID:");
        fidTextLabel->setMinimumWidth(30);

        _fidButton = new QPushButton("No FRM");
        _fidButton->setToolTip("Click to browse FRM files");

        QStyle* style = QApplication::style();
        QIcon folderIcon = style->standardIcon(QStyle::SP_DirOpenIcon);
        _fidButton->setIcon(folderIcon);

        _fidButton->setStyleSheet(ui::theme::styles::fidButton());
        _fidButton->setLayoutDirection(Qt::RightToLeft);
        _fidButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        connect(_fidButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onFidSelectorClicked);

        fidLayout->addWidget(fidTextLabel);
        fidLayout->addWidget(_fidButton);
        fidLayout->addStretch();
        mainLayout->addWidget(_fidWidget);
    }

    if (_options & ShowAnimationControls) {
        _playPauseButton = new QPushButton(this);

        _playPauseButton->setIcon(createIcon(":/icons/actions/play.svg"));

        _playPauseButton->setToolTip("Play/Stop animation");
        _playPauseButton->setFixedSize(ui::constants::sizes::ICON_BUTTON, ui::constants::sizes::ICON_BUTTON);
        _playPauseButton->setStyleSheet(ui::theme::styles::overlayButton());
        _playPauseButton->setIconSize(QSize(ui::constants::sizes::ICON_SIZE_SMALL, ui::constants::sizes::ICON_SIZE_SMALL));
        _playPauseButton->hide();

        _rotateButton = new QPushButton(this);

        _rotateButton->setIcon(createIcon(":/icons/actions/rotate.svg"));

        _rotateButton->setToolTip("Rotate object direction");
        _rotateButton->setFixedSize(ui::constants::sizes::ICON_BUTTON, ui::constants::sizes::ICON_BUTTON);
        _rotateButton->setStyleSheet(ui::theme::styles::overlayButton());
        _rotateButton->setIconSize(QSize(ui::constants::sizes::ICON_SIZE_SMALL, ui::constants::sizes::ICON_SIZE_SMALL));
        _rotateButton->hide();

        connect(_playPauseButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onPlayPauseClicked);
        connect(_rotateButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onRotateClicked);

        connect(_animationController, &AnimationController::frameChanged, this, &ObjectPreviewWidget::onFrameChanged);
        connect(_animationController, &AnimationController::playStateChanged, this, [this](bool playing) {
            if (_playPauseButton) {
                _playPauseButton->setIcon(createIcon(playing ? ":/icons/actions/stop.svg" : ":/icons/actions/play.svg"));
            }
        });
    }

    _editButton = new QPushButton(this);
    _editButton->setIcon(createIcon(":/icons/actions/edit.svg"));
    _editButton->setToolTip("Change FRM file");
    _editButton->setFixedSize(ui::constants::sizes::ICON_BUTTON, ui::constants::sizes::ICON_BUTTON);
    _editButton->setStyleSheet(ui::theme::styles::overlayButton());
    _editButton->setIconSize(QSize(ui::constants::sizes::ICON_SIZE_SMALL, ui::constants::sizes::ICON_SIZE_SMALL));
    _editButton->setVisible(true);

    if (_editButton) {
        connect(_editButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onFidSelectorClicked);
    }

    mainLayout->addStretch();
}

void ObjectPreviewWidget::setFrmPath(const QString& frmPath) {
    if (frmPath == _currentFrmPath) {
        return;
    }

    _currentFrmPath = frmPath;

    if (_fidButton) {
        if (!frmPath.isEmpty()) {
            _fidButton->setText(frmPath.split('/').last());
        } else {
            _fidButton->setText("No FRM");
        }
    }

    if (_previewLabel) {
        if (!frmPath.isEmpty()) {
            _previewLabel->setToolTip(QString("FRM: %1").arg(frmPath));
        } else {
            _previewLabel->setToolTip("No FRM loaded");
        }
    }

    updatePreview();
}

void ObjectPreviewWidget::setFid(int32_t fid) {
    if (fid == _currentFid) {
        return;
    }

    _currentFid = fid;
}

void ObjectPreviewWidget::clear() {
    _animationController->stop();
    _animationController->clearFrames();

    _currentFid = 0;
    _currentFrmPath.clear();
    _totalDirections = 0;
    _currentDirection = 0;

    _previewLabel->setText("No FRM loaded");

    if (_fidButton) {
        _fidButton->setText("No FRM");
    }

    if (_rotateButton) {
        _rotateButton->hide();
    }
    if (_playPauseButton) {
        _playPauseButton->hide();
    }
}

void ObjectPreviewWidget::setScaleFactor(double scaleFactor) {
    _scaleFactor = scaleFactor;
    if (!_currentFrmPath.isEmpty()) {
        updatePreview();
    }
}

void ObjectPreviewWidget::updatePreview() {
    _animationController->stop();

    if (_currentFrmPath.isEmpty()) {
        _previewLabel->setText("No FRM loaded");
        return;
    }

    QPixmap thumbnail = FrmThumbnailGenerator::fromFrmPath(_resources, _currentFrmPath.toStdString(), QSize(250, 250));

    if (!thumbnail.isNull()) {
        QSize labelSize = _previewLabel->size();
        if (labelSize.isEmpty() || labelSize.width() <= 0 || labelSize.height() <= 0) {
            if (!_customPreviewSize.isEmpty()) {
                labelSize = _customPreviewSize;
            } else {
                labelSize = QSize(PREVIEW_MIN_WIDTH, PREVIEW_MIN_HEIGHT);
            }
        }

        QSize targetSize = QSize(thumbnail.width() * _scaleFactor, thumbnail.height() * _scaleFactor);

        if (targetSize.width() > labelSize.width() || targetSize.height() > labelSize.height()) {
            QPixmap scaled = thumbnail.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            _previewLabel->setPixmap(scaled);
        } else {
            QPixmap scaled = thumbnail.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            _previewLabel->setPixmap(scaled);
        }

        if (_options & ShowAnimationControls) {
            loadAnimationFrames();
            if (_rotateButton) {
                _rotateButton->setVisible(_totalDirections > 1);
            }
            if (_playPauseButton) {
                _playPauseButton->setVisible(_animationController->hasMultipleFrames());
            }
        }

        positionOverlayButtons();
    } else {
        _previewLabel->setText("Failed to load FRM");
        if (_rotateButton) {
            _rotateButton->hide();
        }
    }
}

void ObjectPreviewWidget::stopAnimation() {
    _animationController->stop();
}

void ObjectPreviewWidget::onPlayPauseClicked() {
    if (_animationController->isPlaying()) {
        _animationController->stop();
    } else {
        _animationController->play();
    }
}

void ObjectPreviewWidget::onFrameChanged(int frame) {
    const QPixmap& pixmap = _animationController->frame(frame);
    if (pixmap.isNull()) {
        return;
    }

    QSize labelSize = _previewLabel->size();
    if (labelSize.isEmpty() || labelSize.width() <= 0 || labelSize.height() <= 0) {
        if (!_customPreviewSize.isEmpty()) {
            labelSize = _customPreviewSize;
        } else {
            labelSize = QSize(PREVIEW_MIN_WIDTH, PREVIEW_MIN_HEIGHT);
        }
    }

    QSize targetSize = QSize(pixmap.width() * _scaleFactor, pixmap.height() * _scaleFactor);

    if (targetSize.width() > labelSize.width() || targetSize.height() > labelSize.height()) {
        QPixmap scaled = pixmap.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        _previewLabel->setPixmap(scaled);
    } else {
        QPixmap scaled = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        _previewLabel->setPixmap(scaled);
    }
}

void ObjectPreviewWidget::onRotateClicked() {
    _currentDirection = (_currentDirection + 1) % DIRECTIONS_COUNT;

    _animationController->stop();
    loadAnimationFrames();

    if (_animationController->hasFrames()) {
        onFrameChanged(0);
    }
}

void ObjectPreviewWidget::onFidSelectorClicked() {
    Q_EMIT fidChangeRequested();
}

void ObjectPreviewWidget::loadAnimationFrames() {
    _animationController->clearFrames();

    if (_currentFrmPath.isEmpty()) {
        return;
    }

    try {
        auto frm = _resources.repository().load<Frm>(_currentFrmPath.toStdString());
        if (!frm) {
            spdlog::error("Failed to load FRM for animation: {}", _currentFrmPath.toStdString());
            return;
        }

        auto pal = _resources.repository().load<Pal>("color.pal");
        if (!pal) {
            spdlog::error("Failed to load palette for animation");
            return;
        }

        if (_currentDirection >= static_cast<int>(frm->directions().size())) {
            return;
        }

        const auto& direction = frm->directions()[_currentDirection];
        _totalDirections = static_cast<int>(frm->directions().size());
        const auto& frames = direction.frames();

        // Lay the frames out on a single canvas so playback keeps the sprite
        // anchored (per-frame offsets accumulated, bottom-centre anchor) instead
        // of re-centring each differently-sized frame — the "shaky camera" bug.
        std::vector<FrameBox> boxes;
        boxes.reserve(frames.size());
        for (const auto& frame : frames) {
            boxes.push_back({ frame.offsetX(), frame.offsetY(), frame.width(), frame.height() });
        }
        const AnimationLayout layout = computeAnimationLayout(boxes);
        if (!layout.valid()) {
            _animationController->clearFrames();
            return; // no renderable frames
        }

        // Composite each frame onto an identically-sized transparent canvas at
        // its laid-out position; equal sizes make the preview's centring stable.
        std::vector<QPixmap> frameCache;
        frameCache.reserve(frames.size());
        for (size_t i = 0; i < frames.size(); ++i) {
            const FrameRect& place = layout.placements[i];
            if (place.width == 0 || place.height == 0) {
                continue;
            }

            uint8_t* rgbaData = frames[i].rgba(pal);
            if (!rgbaData) {
                spdlog::debug("Failed to get RGBA data from frame, skipping");
                continue;
            }

            QImage frameImage(rgbaData, place.width, place.height, QImage::Format_RGBA8888);
            frameImage = frameImage.copy();

            QPixmap canvas(layout.canvasWidth, layout.canvasHeight);
            canvas.fill(Qt::transparent);
            QPainter painter(&canvas);
            painter.drawImage(place.left, place.top, frameImage);
            painter.end();

            frameCache.push_back(std::move(canvas));
        }

        _animationController->loadFrames(std::move(frameCache));
    } catch (const std::exception& e) {
        spdlog::error("Error loading animation frames: {}", e.what());
        _animationController->clearFrames();
    }
}

void ObjectPreviewWidget::positionOverlayButtons() {
    if (!_previewLabel) {
        return;
    }

    QRect previewRect = _previewLabel->geometry();

    // Position rotate button in top-right corner
    if (_rotateButton) {
        QPoint topRight = previewRect.topRight();
        topRight.setX(topRight.x() - _rotateButton->width() - 4);
        topRight.setY(topRight.y() + 4);
        _rotateButton->move(topRight);
        _rotateButton->raise();
    }

    // Position play button just below the rotate button
    if (_playPauseButton) {
        QPoint topRight = previewRect.topRight();
        topRight.setX(topRight.x() - _playPauseButton->width() - 4);
        topRight.setY(topRight.y() + 4 + (_rotateButton ? _rotateButton->height() + 4 : 0));
        _playPauseButton->move(topRight);
        _playPauseButton->raise();
    }

    // Position edit button in top-left corner for balance
    if (_editButton) {
        QPoint topLeft = previewRect.topLeft();
        topLeft.setX(topLeft.x() + 4);
        topLeft.setY(topLeft.y() + 4);
        _editButton->move(topLeft);
        _editButton->raise();
    }
}

void ObjectPreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    if ((_rotateButton && _rotateButton->isVisible()) || (_playPauseButton && _playPauseButton->isVisible()) || (_editButton && _editButton->isVisible())) {
        positionOverlayButtons();
    }
}

void ObjectPreviewWidget::setTitle(const QString& title) {
    _title = title;
    if (_titleLabel) {
        _titleLabel->setText(title);
        _titleLabel->setVisible(!title.isEmpty());
    }
}

QString ObjectPreviewWidget::getTitle() const {
    return _title;
}

void ObjectPreviewWidget::setPreviewSize(const QSize& size) {
    _customPreviewSize = size;
    if (_previewLabel) {
        _previewLabel->setMinimumSize(size);
        _previewLabel->setMaximumSize(size);
    }
}

void ObjectPreviewWidget::setShowAnimationControls(bool show) {
    if (show) {
        _options |= ShowAnimationControls;
    } else {
        _options &= ~ShowAnimationControls;
    }

    if (_rotateButton) {
        _rotateButton->setVisible(show && _totalDirections > 1);
    }
    if (_playPauseButton) {
        _playPauseButton->setVisible(show && _animationController->hasMultipleFrames());
    }
}

void ObjectPreviewWidget::setShowFidField(bool show) {
    if (_fidWidget) {
        _fidWidget->setVisible(show);
    }
}

} // namespace geck
