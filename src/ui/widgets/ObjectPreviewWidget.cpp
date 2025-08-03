#include "ObjectPreviewWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <spdlog/spdlog.h>

#include "../../format/frm/Frm.h"
#include "../../format/frm/Frame.h"
#include "../../format/pal/Pal.h"
#include "../../util/ResourceManager.h"
#include "../../reader/ReaderFactory.h"

namespace geck {

ObjectPreviewWidget::ObjectPreviewWidget(QWidget* parent)
    : QWidget(parent)
    , _previewLabel(nullptr)
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
{
    setupUI();
}

void ObjectPreviewWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    // Create preview group box
    QGroupBox* previewGroup = new QGroupBox("FRM Preview");
    QVBoxLayout* previewGroupLayout = new QVBoxLayout(previewGroup);
    
    // Preview image label
    _previewLabel = new QLabel();
    _previewLabel->setAlignment(Qt::AlignCenter);
    _previewLabel->setMinimumHeight(PREVIEW_MIN_HEIGHT);
    _previewLabel->setMaximumHeight(PREVIEW_MAX_HEIGHT);
    _previewLabel->setMinimumWidth(PREVIEW_MIN_WIDTH);
    _previewLabel->setScaledContents(false);
    _previewLabel->setStyleSheet("QLabel { border: 1px solid gray; background-color: #f0f0f0; padding: 10px; }");
    _previewLabel->setText("No FRM loaded");
    previewGroupLayout->addWidget(_previewLabel);
    
    // FID field
    QWidget* fidWidget = new QWidget();
    QHBoxLayout* fidLayout = new QHBoxLayout(fidWidget);
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
    previewGroupLayout->addWidget(fidWidget);
    
    // Animation controls
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
    
    previewGroupLayout->addWidget(_animationControls);
    
    // Setup animation timer
    _animationTimer = new QTimer(this);
    _animationTimer->setSingleShot(false);
    _animationTimer->setInterval(ANIMATION_TIMER_INTERVAL);
    
    // Connect signals
    connect(_playPauseButton, &QPushButton::clicked, this, &ObjectPreviewWidget::onPlayPauseClicked);
    connect(_frameSlider, &QSlider::valueChanged, this, &ObjectPreviewWidget::onFrameChanged);
    connect(_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ObjectPreviewWidget::onDirectionChanged);
    connect(_animationTimer, &QTimer::timeout, this, &ObjectPreviewWidget::onAnimationTick);
    
    // Initially disable controls
    _animationControls->setEnabled(false);
    
    // Add to main layout
    mainLayout->addWidget(previewGroup);
    mainLayout->addStretch();
}

void ObjectPreviewWidget::setFrmPath(const QString& frmPath) {
    if (frmPath == _currentFrmPath) {
        return;
    }
    
    _currentFrmPath = frmPath;
    
    // Update FID label
    if (!frmPath.isEmpty()) {
        _fidLabel->setText(frmPath.split('/').last());
    } else {
        _fidLabel->setText("No FRM");
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
    _fidLabel->setText("No FRM");
    _frameLabel->setText("0/0");
    _frameSlider->setMaximum(0);
    _animationControls->setEnabled(false);
}

void ObjectPreviewWidget::updatePreview() {
    stopAnimation();
    
    if (_currentFrmPath.isEmpty()) {
        _previewLabel->setText("No FRM loaded");
        _animationControls->setEnabled(false);
        return;
    }
    
    // Create thumbnail for preview
    QPixmap thumbnail = createFrmThumbnail(_currentFrmPath.toStdString());
    
    if (!thumbnail.isNull()) {
        // Scale proportionally to fit within widget bounds while preserving aspect ratio
        QSize labelSize = _previewLabel->size();
        if (labelSize.isEmpty() || labelSize.width() <= 0 || labelSize.height() <= 0) {
            // Fallback to widget constraints if size not available yet
            labelSize = QSize(PREVIEW_MIN_WIDTH, PREVIEW_MIN_HEIGHT);
        }
        
        // Scale down only if image is larger than widget, otherwise keep original size
        if (thumbnail.width() > labelSize.width() || thumbnail.height() > labelSize.height()) {
            QPixmap scaled = thumbnail.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            _previewLabel->setPixmap(scaled);
        } else {
            // Keep original size for small images
            _previewLabel->setPixmap(thumbnail);
        }
        
        // Load animation frames
        loadAnimationFrames();
        
        // Enable animation controls if we have frames
        _animationControls->setEnabled(_totalFrames > 0);
    } else {
        _previewLabel->setText("Failed to load FRM");
        _animationControls->setEnabled(false);
    }
}

void ObjectPreviewWidget::stopAnimation() {
    if (_animationTimer->isActive()) {
        _animationTimer->stop();
        _isAnimating = false;
        _playPauseButton->setText("▶");
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
            // Fallback to widget constraints if size not available yet
            labelSize = QSize(PREVIEW_MIN_WIDTH, PREVIEW_MIN_HEIGHT);
        }
        
        // Scale down only if image is larger than widget, otherwise keep original size
        if (_frameCache[frame].width() > labelSize.width() || _frameCache[frame].height() > labelSize.height()) {
            QPixmap scaled = _frameCache[frame].scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            _previewLabel->setPixmap(scaled);
        } else {
            // Keep original size for small images
            _previewLabel->setPixmap(_frameCache[frame]);
        }
    }
    
    _frameLabel->setText(QString("%1/%2").arg(_currentFrame + 1).arg(_totalFrames));
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
    _frameSlider->setValue(0);
    onFrameChanged(0);
}

void ObjectPreviewWidget::onAnimationTick() {
    if (_totalFrames <= 1) {
        return;
    }
    
    _currentFrame = (_currentFrame + 1) % _totalFrames;
    _frameSlider->setValue(_currentFrame);
    // onFrameChanged will be called automatically via signal
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
        _frameSlider->setMaximum(_totalFrames - 1);
        _frameLabel->setText(QString("%1/%2").arg(_currentFrame + 1).arg(_totalFrames));
        
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

} // namespace geck