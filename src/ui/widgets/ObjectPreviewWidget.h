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

namespace geck {

class Frm;

/**
 * @brief Widget for displaying and animating FRM previews for non-item objects
 * 
 * This widget provides a preview of FRM files with animation controls,
 * direction selection, and FID management. It handles all non-item PRO objects:
 * CRITTER, SCENERY, WALL, TILE, and MISC. It was extracted from ProEditorDialog
 * to reduce complexity and improve reusability.
 */
class ObjectPreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit ObjectPreviewWidget(QWidget* parent = nullptr);
    ~ObjectPreviewWidget() = default;
    
    // Configuration
    void setFrmPath(const QString& frmPath);
    void setFid(int32_t fid);
    void clear();
    
    // Preview control
    void updatePreview();
    void stopAnimation();
    
    // Getters
    int32_t getFid() const { return _currentFid; }
    QString getFrmPath() const { return _currentFrmPath; }
    
signals:
    void fidChangeRequested(); // User clicked FID selector button
    void fidChanged(int32_t newFid); // FID was changed
    
private slots:
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
    QLabel* _previewLabel;
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
    
    // Constants
    static constexpr int PREVIEW_MIN_HEIGHT = 100;
    static constexpr int PREVIEW_MAX_HEIGHT = 125;
    static constexpr int PREVIEW_MIN_WIDTH = 125;
    static constexpr int ANIMATION_TIMER_INTERVAL = 200; // 5 FPS
    static constexpr int DIRECTIONS_COUNT = 6;
};

} // namespace geck