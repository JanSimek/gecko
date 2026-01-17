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

#include "../../format/frm/Frm.h"
#include "../../format/frm/Frame.h"
#include "../../format/pal/Pal.h"
#include "../../util/ResourceManager.h"
#include "../../util/FrmThumbnailGenerator.h"
#include "../IconHelper.h"
#include "../theme/ThemeManager.h"
#include "../UIConstants.h"
#include "../../reader/ReaderFactory.h"

namespace geck {

ObjectPreviewWidget::ObjectPreviewWidget(QWidget* parent, PreviewOptions options, const QSize& previewSize, double scaleFactor)
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
    , _options(options)
    , _customPreviewSize(previewSize)
    , _scaleFactor(scaleFactor) {
    setupUI();
}

void ObjectPreviewWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Preview image label
    _previewLabel = new QLabel();
    _previewLabel->setAlignment(Qt::AlignCenter);

    // Use custom size if provided
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

    // Title label below image (like armor preview)
    _titleLabel = new QLabel();
    _titleLabel->setAlignment(Qt::AlignCenter);
    _titleLabel->setStyleSheet(ui::theme::styles::compactLabel());
    _titleLabel->hide(); // Initially hidden until title is set
    mainLayout->addWidget(_titleLabel);

    // FID field (optional)
    if (_options & ShowFidField) {
        _fidWidget = new QWidget();
        QHBoxLayout* fidLayout = new QHBoxLayout(_fidWidget);
        fidLayout->setContentsMargins(0, 0, 0, 0);

        QLabel* fidTextLabel = new QLabel("FID:");
        fidTextLabel->setMinimumWidth(30);

        // Create combined button that looks like a label with icon
        _fidButton = new QPushButton("No FRM");
        _fidButton->setToolTip("Click to browse FRM files");

        // Get standard folder icon from system style
        QStyle* style = QApplication::style();
        QIcon folderIcon = style->standardIcon(QStyle::SP_DirOpenIcon);
        _fidButton->setIcon(folderIcon);

        // Style the button to look like a clickable label
        _fidButton->setStyleSheet(ui::theme::styles::fidButton());

        // Make the icon appear on the right side
        _fidButton->setLayoutDirection(Qt::RightToLeft);

        // Make button size to its contents (text + icon + padding)
        _fidButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        connect(_fidButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onFidSelectorClicked);

        fidLayout->addWidget(fidTextLabel);
        fidLayout->addWidget(_fidButton); // Don't expand, size to content
        fidLayout->addStretch();          // Add stretch to push everything to the left
        mainLayout->addWidget(_fidWidget);
    }

    // Animation control overlays (optional)
    if (_options & ShowAnimationControls) {
        // Create play/stop button overlay positioned on the preview label
        _playPauseButton = new QPushButton(this);

        _playPauseButton->setIcon(createIcon(":/icons/actions/play.svg"));

        _playPauseButton->setToolTip("Play/Stop animation");
        _playPauseButton->setFixedSize(ui::constants::sizes::ICON_BUTTON, ui::constants::sizes::ICON_BUTTON);
        _playPauseButton->setStyleSheet(ui::theme::styles::overlayButton());
        _playPauseButton->setIconSize(QSize(ui::constants::sizes::ICON_SIZE_SMALL, ui::constants::sizes::ICON_SIZE_SMALL));
        _playPauseButton->hide(); // Initially hidden until preview is loaded

        // Create rotate button overlay positioned on the preview label
        _rotateButton = new QPushButton(this);

        _rotateButton->setIcon(createIcon(":/icons/actions/rotate.svg"));

        _rotateButton->setToolTip("Rotate object direction");
        _rotateButton->setFixedSize(ui::constants::sizes::ICON_BUTTON, ui::constants::sizes::ICON_BUTTON);
        _rotateButton->setStyleSheet(ui::theme::styles::overlayButton());
        _rotateButton->setIconSize(QSize(ui::constants::sizes::ICON_SIZE_SMALL, ui::constants::sizes::ICON_SIZE_SMALL));
        _rotateButton->hide(); // Initially hidden until preview is loaded

        // Connect animation control signals
        connect(_playPauseButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onPlayPauseClicked);
        connect(_rotateButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onRotateClicked);

        // Connect animation controller signals
        connect(_animationController, &AnimationController::frameChanged, this, &ObjectPreviewWidget::onFrameChanged);
        connect(_animationController, &AnimationController::playStateChanged, this, [this](bool playing) {
            if (_playPauseButton) {
                _playPauseButton->setIcon(createIcon(playing ? ":/icons/actions/stop.svg" : ":/icons/actions/play.svg"));
            }
        });
    }

    // Always create edit button (independent of animation controls)
    _editButton = new QPushButton(this);
    _editButton->setIcon(createIcon(":/icons/actions/edit.svg"));
    _editButton->setToolTip("Change FRM file");
    _editButton->setFixedSize(ui::constants::sizes::ICON_BUTTON, ui::constants::sizes::ICON_BUTTON);
    _editButton->setStyleSheet(ui::theme::styles::overlayButton());
    _editButton->setIconSize(QSize(ui::constants::sizes::ICON_SIZE_SMALL, ui::constants::sizes::ICON_SIZE_SMALL));
    _editButton->setVisible(true); // Always visible

    // Connect edit button signal
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

    // Update FID button if it exists
    if (_fidButton) {
        if (!frmPath.isEmpty()) {
            _fidButton->setText(frmPath.split('/').last());
        } else {
            _fidButton->setText("No FRM");
        }
    }

    // Update tooltip with filename
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
    emit fidChanged(fid);
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

    // Hide overlay buttons when no FRM is loaded
    if (_rotateButton) {
        _rotateButton->hide();
    }
    if (_playPauseButton) {
        _playPauseButton->hide();
    }
}

void ObjectPreviewWidget::setScaleFactor(double scaleFactor) {
    _scaleFactor = scaleFactor;
    // Update preview with new scale factor if FRM is loaded
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

    // Create thumbnail for preview
    QPixmap thumbnail = FrmThumbnailGenerator::fromFrmPath(_currentFrmPath.toStdString(), QSize(250, 250));

    if (!thumbnail.isNull()) {
        // Scale proportionally to fit within widget bounds while preserving aspect ratio
        QSize labelSize = _previewLabel->size();
        if (labelSize.isEmpty() || labelSize.width() <= 0 || labelSize.height() <= 0) {
            // Fallback to custom size or default constraints
            if (!_customPreviewSize.isEmpty()) {
                labelSize = _customPreviewSize;
            } else {
                labelSize = QSize(PREVIEW_MIN_WIDTH, PREVIEW_MIN_HEIGHT);
            }
        }

        // Scale image to configured size, but constrain to widget bounds if too large
        QSize targetSize = QSize(thumbnail.width() * _scaleFactor, thumbnail.height() * _scaleFactor);

        // If 2x size exceeds widget bounds, scale down to fit
        if (targetSize.width() > labelSize.width() || targetSize.height() > labelSize.height()) {
            QPixmap scaled = thumbnail.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            _previewLabel->setPixmap(scaled);
        } else {
            // Use 2x scaled size
            QPixmap scaled = thumbnail.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            _previewLabel->setPixmap(scaled);
        }

        // Load animation frames if animation controls are present
        if (_options & ShowAnimationControls) {
            loadAnimationFrames();
            // Show overlay buttons based on available content
            if (_rotateButton) {
                _rotateButton->setVisible(_totalDirections > 1);
            }
            if (_playPauseButton) {
                _playPauseButton->setVisible(_animationController->hasMultipleFrames());
            }
        }

        // Always position overlay buttons (edit button is always visible)
        positionOverlayButtons();
    } else {
        _previewLabel->setText("Failed to load FRM");
        // Hide rotate button when FRM loading fails
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

    // Scale proportionally to fit within widget bounds while preserving aspect ratio
    QSize labelSize = _previewLabel->size();
    if (labelSize.isEmpty() || labelSize.width() <= 0 || labelSize.height() <= 0) {
        // Fallback to custom size or default constraints
        if (!_customPreviewSize.isEmpty()) {
            labelSize = _customPreviewSize;
        } else {
            labelSize = QSize(PREVIEW_MIN_WIDTH, PREVIEW_MIN_HEIGHT);
        }
    }

    // Scale image to configured size, but constrain to widget bounds if too large
    QSize targetSize = QSize(pixmap.width() * _scaleFactor, pixmap.height() * _scaleFactor);

    // If 2x size exceeds widget bounds, scale down to fit
    if (targetSize.width() > labelSize.width() || targetSize.height() > labelSize.height()) {
        QPixmap scaled = pixmap.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        _previewLabel->setPixmap(scaled);
    } else {
        // Use 2x scaled size
        QPixmap scaled = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        _previewLabel->setPixmap(scaled);
    }
}

void ObjectPreviewWidget::onRotateClicked() {
    // Cycle through directions: 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 0
    _currentDirection = (_currentDirection + 1) % DIRECTIONS_COUNT;

    // Stop animation and reload frames for new direction
    _animationController->stop();
    loadAnimationFrames();

    // Show first frame
    if (_animationController->hasFrames()) {
        onFrameChanged(0);
    }
}

void ObjectPreviewWidget::onFidSelectorClicked() {
    emit fidChangeRequested();
}

void ObjectPreviewWidget::loadAnimationFrames() {
    _animationController->clearFrames();

    if (_currentFrmPath.isEmpty()) {
        return;
    }

    try {
        // Load the FRM from resource manager
        auto& resourceManager = ResourceManager::getInstance();
        auto frm = resourceManager.loadResource<Frm>(_currentFrmPath.toStdString());
        if (!frm) {
            spdlog::error("Failed to load FRM for animation: {}", _currentFrmPath.toStdString());
            return;
        }

        // Get palette
        auto pal = resourceManager.loadResource<Pal>("color.pal");
        if (!pal) {
            spdlog::error("Failed to load palette for animation");
            return;
        }

        // Check if direction exists
        if (_currentDirection >= static_cast<int>(frm->directions().size())) {
            return;
        }

        const auto& direction = frm->directions()[_currentDirection];
        _totalDirections = static_cast<int>(frm->directions().size());

        // Cache all frames for this direction
        std::vector<QPixmap> frameCache;
        frameCache.reserve(direction.frames().size());
        for (const auto& frame : direction.frames()) {
            // Get frame dimensions
            uint16_t frameWidth = frame.width();
            uint16_t frameHeight = frame.height();

            if (frameWidth == 0 || frameHeight == 0) {
                spdlog::debug("Frame has zero dimensions, skipping");
                continue;
            }

            // Use Frame's built-in RGBA conversion (like ObjectPalettePanel)
            uint8_t* rgbaData = const_cast<Frame&>(frame).rgba(const_cast<Pal*>(pal));
            if (!rgbaData) {
                spdlog::debug("Failed to get RGBA data from frame, skipping");
                continue;
            }

            QImage frameImage(rgbaData, frameWidth, frameHeight, QImage::Format_RGBA8888);
            frameImage = frameImage.copy(); // Make a copy since rgbaData might be temporary

            frameCache.push_back(QPixmap::fromImage(frameImage));
        }

        // Load frames into controller
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
        topRight.setX(topRight.x() - _rotateButton->width() - 4); // 4px margin from edge
        topRight.setY(topRight.y() + 4);                          // 4px margin from top
        _rotateButton->move(topRight);
        _rotateButton->raise(); // Ensure it's on top
    }

    // Position play button next to rotate button (top-right area)
    if (_playPauseButton) {
        QPoint topRight = previewRect.topRight();
        topRight.setX(topRight.x() - _playPauseButton->width() - 4);                         // 4px margin from edge
        topRight.setY(topRight.y() + 4 + (_rotateButton ? _rotateButton->height() + 4 : 0)); // Below rotate button with 4px gap
        _playPauseButton->move(topRight);
        _playPauseButton->raise(); // Ensure it's on top
    }

    // Position edit button in top-left corner for balance
    if (_editButton) {
        QPoint topLeft = previewRect.topLeft();
        topLeft.setX(topLeft.x() + 4); // 4px margin from left edge
        topLeft.setY(topLeft.y() + 4); // 4px margin from top
        _editButton->move(topLeft);
        _editButton->raise(); // Ensure it's on top
    }
}

void ObjectPreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // Reposition overlay buttons when widget is resized
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
    // Update the options flag
    if (show) {
        _options |= ShowAnimationControls;
    } else {
        _options &= ~ShowAnimationControls;
    }

    // Show/hide overlay buttons
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
