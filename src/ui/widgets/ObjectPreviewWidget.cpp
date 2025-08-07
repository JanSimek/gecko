#include "ObjectPreviewWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QApplication>
#include <QStyle>
#include <QResizeEvent>
#include <spdlog/spdlog.h>

#include "../../format/frm/Frm.h"
#include "../../format/frm/Frame.h"
#include "../../format/pal/Pal.h"
#include "../../util/ResourceManager.h"
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
    , _animationTimer(nullptr)
    , _currentFrame(0)
    , _currentDirection(0)
    , _totalFrames(0)
    , _totalDirections(0)
    , _isAnimating(false)
    , _currentFid(0)
    , _options(options)
    , _customPreviewSize(previewSize)
    , _scaleFactor(scaleFactor)
{
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
    _previewLabel->setStyleSheet("QLabel { border: 1px solid gray; background-color: #f0f0f0; }");
    _previewLabel->setText("No FRM loaded");
    mainLayout->addWidget(_previewLabel);
    
    // Title label below image (like armor preview)
    _titleLabel = new QLabel();
    _titleLabel->setAlignment(Qt::AlignCenter);
    _titleLabel->setStyleSheet("QLabel { font-size: 10px; margin: 0px; padding: 0px; }");
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
        _fidButton->setStyleSheet(
            "QPushButton {"
            "  border: 1px solid #d0d0d0;"
            "  padding: 2px 4px;"
            "  background-color: white;"
            "  text-align: left;"
            "}"
            "QPushButton:hover {"
            "  background-color: #f5f5f5;"
            "  border-color: #999;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #e0e0e0;"
            "}"
        );
        
        // Make the icon appear on the right side
        _fidButton->setLayoutDirection(Qt::RightToLeft);
        
        // Make button size to its contents (text + icon + padding)
        _fidButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        
        connect(_fidButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onFidSelectorClicked);
        
        fidLayout->addWidget(fidTextLabel);
        fidLayout->addWidget(_fidButton); // Don't expand, size to content
        fidLayout->addStretch(); // Add stretch to push everything to the left
        mainLayout->addWidget(_fidWidget);
    }
    
    // Animation control overlays (optional)
    if (_options & ShowAnimationControls) {
        // Create play/stop button overlay positioned on the preview label
        _playPauseButton = new QPushButton(this);
        
        _playPauseButton->setIcon(QIcon(":/icons/actions/play.svg"));
        
        _playPauseButton->setToolTip("Play/Stop animation");
        _playPauseButton->setFixedSize(24, 24);
        _playPauseButton->setStyleSheet(
            "QPushButton {"
            "  background-color: rgba(255, 255, 255, 180);"
            "  border: 1px solid rgba(0, 0, 0, 100);"
            "  border-radius: 12px;"
            "}"
            "QPushButton:hover {"
            "  background-color: rgba(255, 255, 255, 220);"
            "  border-color: rgba(0, 0, 0, 150);"
            "}"
            "QPushButton:pressed {"
            "  background-color: rgba(255, 255, 255, 255);"
            "}"
        );
        _playPauseButton->setIconSize(QSize(18, 18));
        _playPauseButton->hide(); // Initially hidden until preview is loaded
        
        // Setup animation timer
        _animationTimer = new QTimer(this);
        _animationTimer->setSingleShot(false);
        _animationTimer->setInterval(ANIMATION_TIMER_INTERVAL);
        
        // Create rotate button overlay positioned on the preview label
        _rotateButton = new QPushButton(this);
        
        _rotateButton->setIcon(QIcon(":/icons/actions/rotate.svg"));
        
        _rotateButton->setToolTip("Rotate object direction");
        _rotateButton->setFixedSize(24, 24);
        _rotateButton->setStyleSheet(
            "QPushButton {"
            "  background-color: rgba(255, 255, 255, 180);"
            "  border: 1px solid rgba(0, 0, 0, 100);"
            "  border-radius: 12px;"
            "}"
            "QPushButton:hover {"
            "  background-color: rgba(255, 255, 255, 220);"
            "  border-color: rgba(0, 0, 0, 150);"
            "}"
            "QPushButton:pressed {"
            "  background-color: rgba(255, 255, 255, 255);"
            "}"
        );
        _rotateButton->setIconSize(QSize(18, 18));
        _rotateButton->hide(); // Initially hidden until preview is loaded
        
        // Connect animation control signals
        if (_playPauseButton) {
            connect(_playPauseButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onPlayPauseClicked);
        }
        if (_rotateButton) {
            connect(_rotateButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onRotateClicked);
        }
        if (_animationTimer) {
            connect(_animationTimer, &QTimer::timeout, this, &ObjectPreviewWidget::onAnimationTick);
        }
    }
    
    // Always create edit button (independent of animation controls)
    _editButton = new QPushButton(this);
    _editButton->setIcon(QIcon(":/icons/actions/edit.svg"));
    _editButton->setToolTip("Change FRM file");
    _editButton->setFixedSize(24, 24);
    _editButton->setStyleSheet(
        "QPushButton {"
        "  background-color: rgba(255, 255, 255, 180);"
        "  border: 1px solid rgba(0, 0, 0, 100);"
        "  border-radius: 12px;"
        "}"
        "QPushButton:hover {"
        "  background-color: rgba(255, 255, 255, 220);"
        "  border-color: rgba(0, 0, 0, 150);"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(255, 255, 255, 255);"
        "}"
    );
    _editButton->setIconSize(QSize(18, 18));
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
    stopAnimation();
    
    _currentFid = 0;
    _currentFrmPath.clear();
    _frameCache.clear();
    _totalFrames = 0;
    _totalDirections = 0;
    _currentFrame = 0;
    _currentDirection = 0;
    
    _previewLabel->setText("No FRM loaded");
    
    if (_fidButton) {
        _fidButton->setText("No FRM");
    }
    
    // Hide rotate button when no FRM is loaded
    if (_rotateButton) {
        _rotateButton->hide();
    }
    
    // Animation controls removed - using overlay buttons instead
}

void ObjectPreviewWidget::setScaleFactor(double scaleFactor) {
    _scaleFactor = scaleFactor;
    // Update preview with new scale factor if FRM is loaded
    if (!_currentFrmPath.isEmpty()) {
        updatePreview();
    }
}

void ObjectPreviewWidget::updatePreview() {
    stopAnimation();
    
    if (_currentFrmPath.isEmpty()) {
        _previewLabel->setText("No FRM loaded");
        // Animation controls removed - using overlay buttons instead
        return;
    }
    
    // Create thumbnail for preview
    QPixmap thumbnail = createFrmThumbnail(_currentFrmPath.toStdString());
    
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
                _playPauseButton->setVisible(_totalFrames > 1);
            }
        }
        
        // Always position overlay buttons (edit button is always visible)
        positionOverlayButtons();
    } else {
        _previewLabel->setText("Failed to load FRM");
        // Animation controls removed - using overlay buttons instead
        // Hide rotate button when FRM loading fails
        if (_rotateButton) {
            _rotateButton->hide();
        }
    }
}

void ObjectPreviewWidget::stopAnimation() {
    if (_animationTimer && _animationTimer->isActive()) {
        _animationTimer->stop();
        _isAnimating = false;
        if (_playPauseButton) {
            _playPauseButton->setIcon(QIcon(":/icons/actions/play.svg"));
        }
        // Reset to first frame when stopping
        _currentFrame = 0;
        onFrameChanged(0);
    }
}

void ObjectPreviewWidget::onPlayPauseClicked() {
    if (_isAnimating) {
        // Stop animation and reset to first frame
        _animationTimer->stop();
        _isAnimating = false;
        _playPauseButton->setIcon(QIcon(":/icons/actions/play.svg"));
        _currentFrame = 0;
        onFrameChanged(0); // Reset to first frame
    } else {
        if (_totalFrames > 1) {
            _animationTimer->start();
            _isAnimating = true;
            _playPauseButton->setIcon(QIcon(":/icons/actions/stop.svg"));
        }
    }
}

void ObjectPreviewWidget::onFrameChanged(int frame) {
    _currentFrame = frame;
    
    if (frame < static_cast<int>(_frameCache.size()) && !_frameCache[frame].isNull()) {
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
        QSize targetSize = QSize(_frameCache[frame].width() * _scaleFactor, _frameCache[frame].height() * _scaleFactor);
        
        // If 2x size exceeds widget bounds, scale down to fit
        if (targetSize.width() > labelSize.width() || targetSize.height() > labelSize.height()) {
            QPixmap scaled = _frameCache[frame].scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            _previewLabel->setPixmap(scaled);
        } else {
            // Use 2x scaled size
            QPixmap scaled = _frameCache[frame].scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            _previewLabel->setPixmap(scaled);
        }
    }
}

void ObjectPreviewWidget::onRotateClicked() {
    // Cycle through directions: 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 0
    _currentDirection = (_currentDirection + 1) % DIRECTIONS_COUNT;
    
    // Stop animation when changing direction
    if (_isAnimating) {
        _animationTimer->stop();
        _isAnimating = false;
        _playPauseButton->setIcon(QIcon(":/icons/actions/play.svg"));
    }
    
    // Reload frames for new direction
    loadAnimationFrames();
    
    // Reset to first frame
    _currentFrame = 0;
    onFrameChanged(0);
}

void ObjectPreviewWidget::onAnimationTick() {
    if (_totalFrames <= 1) {
        return;
    }
    
    _currentFrame = (_currentFrame + 1) % _totalFrames;
    // Frame slider removed, call onFrameChanged directly
    onFrameChanged(_currentFrame);
}

void ObjectPreviewWidget::onFidSelectorClicked() {
    emit fidChangeRequested();
}

void ObjectPreviewWidget::loadAnimationFrames() {
    _frameCache.clear();
    _totalFrames = 0;
    
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
        _totalFrames = direction.frames().size();
        _totalDirections = frm->directions().size();
        
        // Cache all frames for this direction
        _frameCache.reserve(_totalFrames);
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
            
            _frameCache.push_back(QPixmap::fromImage(frameImage));
        }
    } catch (const std::exception& e) {
        spdlog::error("Error loading animation frames: {}", e.what());
        _frameCache.clear();
        _totalFrames = 0;
    }
}

QPixmap ObjectPreviewWidget::createFrmThumbnail(const std::string& frmPath, const QSize& targetSize) {
    try {
        auto& resourceManager = ResourceManager::getInstance();
        auto frm = resourceManager.loadResource<Frm>(frmPath);
        if (!frm) {
            spdlog::error("Failed to load FRM: {}", frmPath);
            return QPixmap();
        }
        
        // Get palette
        auto pal = resourceManager.loadResource<Pal>("color.pal");
        if (!pal) {
            spdlog::error("Failed to load palette");
            return QPixmap();
        }
        
        // Get first frame of first direction
        if (frm->directions().empty() || frm->directions()[0].frames().empty()) {
            spdlog::error("FRM has no frames");
            return QPixmap();
        }
        
        const auto& frame = frm->directions()[0].frames()[0];
        
        // Get frame dimensions
        uint16_t frameWidth = frame.width();
        uint16_t frameHeight = frame.height();
        
        if (frameWidth == 0 || frameHeight == 0) {
            spdlog::error("Frame has zero dimensions");
            return QPixmap();
        }
        
        // Use Frame's built-in RGBA conversion (like ObjectPalettePanel)
        uint8_t* rgbaData = const_cast<Frame&>(frame).rgba(const_cast<Pal*>(pal));
        if (!rgbaData) {
            spdlog::error("Failed to get RGBA data from frame");
            return QPixmap();
        }
        
        QImage frameImage(rgbaData, frameWidth, frameHeight, QImage::Format_RGBA8888);
        frameImage = frameImage.copy(); // Make a copy since rgbaData might be temporary
        
        // Return unscaled frame (like animation frames) - scaling will be done by caller
        return QPixmap::fromImage(frameImage);
        
    } catch (const std::exception& e) {
        spdlog::error("Error creating FRM thumbnail: {}", e.what());
        return QPixmap();
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
        topRight.setY(topRight.y() + 4); // 4px margin from top
        _rotateButton->move(topRight);
        _rotateButton->raise(); // Ensure it's on top
    }
    
    // Position play button next to rotate button (top-right area)
    if (_playPauseButton) {
        QPoint topRight = previewRect.topRight();
        topRight.setX(topRight.x() - _playPauseButton->width() - 4); // 4px margin from edge
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
        _playPauseButton->setVisible(show && _totalFrames > 1);
    }
}

void ObjectPreviewWidget::setShowFidField(bool show) {
    if (_fidWidget) {
        _fidWidget->setVisible(show);
    }
}

} // namespace geck