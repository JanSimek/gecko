#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QTimer>
#include <QPixmap>
#include <memory>
#include <vector>

class QGroupBox;

namespace geck {

class Frm;

/**
 * @brief Widget for displaying and animating FRM previews
 * 
 * This widget provides a preview of FRM files with optional animation controls,
 * direction selection, and FID management. It can be configured to show different
 * UI elements making it suitable for both animated object previews and static
 * item previews (inventory/ground).
 */
class ObjectPreviewWidget : public QWidget {
    Q_OBJECT

public:
    enum PreviewOption {
        ShowAnimationControls = 0x01,
        ShowFidField = 0x02,
        ShowGroupBox = 0x04,
        Default = ShowAnimationControls | ShowFidField | ShowGroupBox
    };
    Q_DECLARE_FLAGS(PreviewOptions, PreviewOption)
    
    explicit ObjectPreviewWidget(QWidget* parent = nullptr, 
                                PreviewOptions options = Default,
                                const QSize& previewSize = QSize(150, 150));
    ~ObjectPreviewWidget() = default;
    
    // Configuration
    void setFrmPath(const QString& frmPath);
    void setFid(int32_t fid);
    void clear();
    
    // Runtime configuration
    void setGroupBoxTitle(const QString& title);
    void setPreviewSize(const QSize& size);
    void setShowAnimationControls(bool show);
    void setShowFidField(bool show);
    
    // Preview control
    void updatePreview();
    void stopAnimation();
    
    // Getters
    int32_t getFid() const { return _currentFid; }
    QString getFrmPath() const { return _currentFrmPath; }
    
signals:
    void fidChangeRequested(); // User clicked FID selector button
    void fidChanged(int32_t newFid); // FID was changed
    
public slots:
    void onPlayPauseClicked();
    void onFrameChanged(int frame);
    void onDirectionChanged(int direction);
    void onAnimationTick();
    void onFidSelectorClicked();
    
private:
    void setupUI();
    void loadAnimationFrames();
    QPixmap createFrmThumbnail(const std::string& frmPath, const QSize& targetSize = QSize(250, 250));
    
    // UI Components
    QGroupBox* _previewGroup;
    QLabel* _previewLabel;
    QWidget* _fidWidget;
    QLabel* _fidLabel;
    QPushButton* _fidSelectorButton;
    
    // Animation controls
    QWidget* _animationControls;
    QPushButton* _playPauseButton;
    QSlider* _frameSlider;
    QLabel* _frameLabel;
    QComboBox* _directionCombo;
    QTimer* _animationTimer;
    
    // Animation state
    int _currentFrame;
    int _currentDirection;
    int _totalFrames;
    int _totalDirections;
    bool _isAnimating;
    std::vector<QPixmap> _frameCache;
    
    // Data
    int32_t _currentFid;
    QString _currentFrmPath;
    
    // Configuration
    PreviewOptions _options;
    QSize _customPreviewSize;
    
    // Constants
    static constexpr int PREVIEW_MIN_HEIGHT = 150;
    static constexpr int PREVIEW_MAX_HEIGHT = 175;
    static constexpr int PREVIEW_MIN_WIDTH = 150;
    static constexpr int ANIMATION_TIMER_INTERVAL = 200; // 5 FPS
    static constexpr int DIRECTIONS_COUNT = 6;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ObjectPreviewWidget::PreviewOptions)

} // namespace geck