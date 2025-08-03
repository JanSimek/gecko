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
#include <spdlog/spdlog.h>

#include "../../format/frm/Frm.h"
#include "../../format/frm/Frame.h"
#include "../../format/pal/Pal.h"
#include "../../util/ResourceManager.h"
#include "../../reader/ReaderFactory.h"

namespace geck {

ObjectPreviewWidget::ObjectPreviewWidget(QWidget* parent, PreviewOptions options, const QSize& previewSize)
    : QWidget(parent)
    , _previewGroup(nullptr)
    , _previewLabel(nullptr)
    , _fidWidget(nullptr)
    , _fidLabel(nullptr)
    , _fidSelectorButton(nullptr)
    , _animationControls(nullptr)
    , _playPauseButton(nullptr)
    , _frameSlider(nullptr)
    , _frameLabel(nullptr)
    , _directionCombo(nullptr)
    , _animationTimer(nullptr)
    , _currentFrame(0)
    , _currentDirection(0)
    , _totalFrames(0)
    , _totalDirections(0)
    , _isAnimating(false)
    , _currentFid(0)
    , _options(options)
    , _customPreviewSize(previewSize)
{
    setupUI();
}

void ObjectPreviewWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    QVBoxLayout* contentLayout = nullptr;
    
    // Create preview group box if requested
    if (_options & ShowGroupBox) {
        _previewGroup = new QGroupBox("FRM Preview");
        contentLayout = new QVBoxLayout(_previewGroup);
        mainLayout->addWidget(_previewGroup);
    } else {
        // No group box, add content directly to main layout
        contentLayout = mainLayout;
    }
    
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
    contentLayout->addWidget(_previewLabel);
    
    // FID field (optional)
    if (_options & ShowFidField) {
        _fidWidget = new QWidget();
        QHBoxLayout* fidLayout = new QHBoxLayout(_fidWidget);
        fidLayout->setContentsMargins(0, 0, 0, 0);
        
        QLabel* fidTextLabel = new QLabel("FID:");
        fidTextLabel->setMinimumWidth(30);
        
        _fidLabel = new QLabel("No FRM");
        _fidLabel->setToolTip("FRM filename for ground/world view");
        _fidLabel->setStyleSheet("QLabel { border: 1px solid gray; padding: 2px; background-color: white; }");
        
        _fidSelectorButton = new QPushButton("...");
        _fidSelectorButton->setMaximumWidth(30);
        _fidSelectorButton->setToolTip("Browse FRM files");
        connect(_fidSelectorButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onFidSelectorClicked);
        
        fidLayout->addWidget(fidTextLabel);
        fidLayout->addWidget(_fidLabel);
        fidLayout->addWidget(_fidSelectorButton);
        contentLayout->addWidget(_fidWidget);
    }
    
    // Animation controls (optional)
    if (_options & ShowAnimationControls) {
        _animationControls = new QWidget();
        QHBoxLayout* animationLayout = new QHBoxLayout(_animationControls);
        animationLayout->setContentsMargins(0, 0, 0, 0);
        
        // Direction selection
        _directionCombo = new QComboBox();
        _directionCombo->addItems({"NE", "E", "SE", "SW", "W", "NW"});
        _directionCombo->setToolTip("Select animation direction");
        _directionCombo->setMaximumWidth(50);
        animationLayout->addWidget(new QLabel("Direction:"));
        animationLayout->addWidget(_directionCombo);
        
        animationLayout->addSpacing(10);
        
        // Play/pause button
        _playPauseButton = new QPushButton("▶");
        _playPauseButton->setMaximumWidth(30);
        _playPauseButton->setToolTip("Play/Pause animation");
        animationLayout->addWidget(_playPauseButton);
        
        // Frame slider
        _frameSlider = new QSlider(Qt::Horizontal);
        _frameSlider->setMinimum(0);
        _frameSlider->setMaximum(0);
        _frameSlider->setToolTip("Select frame");
        animationLayout->addWidget(_frameSlider);
        
        // Frame label
        _frameLabel = new QLabel("0/0");
        _frameLabel->setMinimumWidth(40);
        animationLayout->addWidget(_frameLabel);
        
        contentLayout->addWidget(_animationControls);
        
        // Setup animation timer
        _animationTimer = new QTimer(this);
        _animationTimer->setSingleShot(false);
        _animationTimer->setInterval(ANIMATION_TIMER_INTERVAL);
        
        // Connect signals
        if (_playPauseButton) {
            connect(_playPauseButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onPlayPauseClicked);
        }
        if (_frameSlider) {
            connect(_frameSlider, &QSlider::valueChanged, this, &ObjectPreviewWidget::onFrameChanged);
        }
        if (_directionCombo) {
            connect(_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ObjectPreviewWidget::onDirectionChanged);
        }
        if (_animationTimer) {
            connect(_animationTimer, &QTimer::timeout, this, &ObjectPreviewWidget::onAnimationTick);
        }
        
        // Initially disable controls
        _animationControls->setEnabled(false);
    }
    
    mainLayout->addStretch();
}

void ObjectPreviewWidget::setFrmPath(const QString& frmPath) {
    if (frmPath == _currentFrmPath) {
        return;
    }
    
    _currentFrmPath = frmPath;
    
    // Update FID label if it exists
    if (_fidLabel) {
        if (!frmPath.isEmpty()) {
            _fidLabel->setText(frmPath.split('/').last());
        } else {
            _fidLabel->setText("No FRM");
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
    
    if (_fidLabel) {
        _fidLabel->setText("No FRM");
    }
    
    if (_frameLabel) {
        _frameLabel->setText("0/0");
    }
    
    if (_frameSlider) {
        _frameSlider->setMaximum(0);
    }
    
    if (_animationControls) {
        _animationControls->setEnabled(false);
    }
}

void ObjectPreviewWidget::updatePreview() {
    stopAnimation();
    
    if (_currentFrmPath.isEmpty()) {
        _previewLabel->setText("No FRM loaded");
        if (_animationControls) {
            _animationControls->setEnabled(false);
        }
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

        // Scale image to 2x size, but constrain to widget bounds if too large
        QSize targetSize = QSize(thumbnail.width() * SCALE_FACTOR, thumbnail.height() * SCALE_FACTOR);
        
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
        if (_animationControls) {
            loadAnimationFrames();
            // Enable animation controls if we have frames
            _animationControls->setEnabled(_totalFrames > 0);
        }
    } else {
        _previewLabel->setText("Failed to load FRM");
        if (_animationControls) {
            _animationControls->setEnabled(false);
        }
    }
}

void ObjectPreviewWidget::stopAnimation() {
    if (_animationTimer && _animationTimer->isActive()) {
        _animationTimer->stop();
        _isAnimating = false;
        if (_playPauseButton) {
            _playPauseButton->setText("▶");
        }
    }
}

void ObjectPreviewWidget::onPlayPauseClicked() {
    if (_isAnimating) {
        _animationTimer->stop();
        _isAnimating = false;
        _playPauseButton->setText("▶");
    } else {
        if (_totalFrames > 1) {
            _animationTimer->start();
            _isAnimating = true;
            _playPauseButton->setText("⏸");
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
        
        // Scale image to 2x size, but constrain to widget bounds if too large
        QSize targetSize = QSize(_frameCache[frame].width() * SCALE_FACTOR, _frameCache[frame].height() * SCALE_FACTOR);
        
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
    
    if (_frameLabel) {
        _frameLabel->setText(QString("%1/%2").arg(_currentFrame + 1).arg(_totalFrames));
    }
}

void ObjectPreviewWidget::onDirectionChanged(int direction) {
    _currentDirection = direction;
    
    // Stop animation when changing direction
    if (_isAnimating) {
        _animationTimer->stop();
        _isAnimating = false;
        _playPauseButton->setText("▶");
    }
    
    // Reload frames for new direction
    loadAnimationFrames();
    
    // Reset to first frame
    _currentFrame = 0;
    if (_frameSlider) {
        _frameSlider->setValue(0);
    }
    onFrameChanged(0);
}

void ObjectPreviewWidget::onAnimationTick() {
    if (_totalFrames <= 1) {
        return;
    }
    
    _currentFrame = (_currentFrame + 1) % _totalFrames;
    if (_frameSlider) {
        _frameSlider->setValue(_currentFrame);
        // onFrameChanged will be called automatically via signal
    } else {
        // No slider, call onFrameChanged directly
        onFrameChanged(_currentFrame);
    }
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
        
        // Update UI
        if (_frameSlider) {
            _frameSlider->setMaximum(_totalFrames - 1);
        }
        if (_frameLabel) {
            _frameLabel->setText(QString("%1/%2").arg(_currentFrame + 1).arg(_totalFrames));
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

void ObjectPreviewWidget::setGroupBoxTitle(const QString& title) {
    if (_previewGroup) {
        _previewGroup->setTitle(title);
    }
}

void ObjectPreviewWidget::setPreviewSize(const QSize& size) {
    _customPreviewSize = size;
    if (_previewLabel) {
        _previewLabel->setMinimumSize(size);
        _previewLabel->setMaximumSize(size);
    }
}

void ObjectPreviewWidget::setShowAnimationControls(bool show) {
    if (_animationControls) {
        _animationControls->setVisible(show);
    }
}

void ObjectPreviewWidget::setShowFidField(bool show) {
    if (_fidWidget) {
        _fidWidget->setVisible(show);
    }
}

} // namespace geck