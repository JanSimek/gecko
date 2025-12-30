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

#include "../UIConstants.h"

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
        Default = ShowAnimationControls | ShowGroupBox
    };
    Q_DECLARE_FLAGS(PreviewOptions, PreviewOption)

    explicit ObjectPreviewWidget(QWidget* parent = nullptr,
        PreviewOptions options = Default,
        const QSize& previewSize = QSize(150, 150),
        double scaleFactor = 1.25);
    ~ObjectPreviewWidget() = default;

    // Configuration
    void setFrmPath(const QString& frmPath);
    void setFid(int32_t fid);
    void clear();

    // Runtime configuration
    void setTitle(const QString& title);
    QString getTitle() const;
    void setPreviewSize(const QSize& size);
    void setShowAnimationControls(bool show);
    void setShowFidField(bool show);
    void setScaleFactor(double scaleFactor);

    // Preview control
    void updatePreview();
    void stopAnimation();

    // Getters
    int32_t getFid() const { return _currentFid; }
    QString getFrmPath() const { return _currentFrmPath; }

signals:
    void fidChangeRequested();       // User clicked FID selector button
    void fidChanged(int32_t newFid); // FID was changed

public slots:
    void onPlayPauseClicked();
    void onFrameChanged(int frame);
    void onRotateClicked();
    void onAnimationTick();
    void onFidSelectorClicked();

private:
    void setupUI();
    void loadAnimationFrames();
    void positionOverlayButtons();

    // UI Components
    QLabel* _previewLabel;
    QLabel* _titleLabel;
    QWidget* _fidWidget;
    QPushButton* _fidButton; // Combined label + selector button

    // Animation controls (overlays)
    QPushButton* _playPauseButton;
    QPushButton* _rotateButton;
    QPushButton* _editButton;
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
    QString _title;
    double _scaleFactor;

    // Constants
    static constexpr int PREVIEW_MIN_HEIGHT = 150;
    static constexpr int PREVIEW_MAX_HEIGHT = 175;
    static constexpr int PREVIEW_MIN_WIDTH = 150;
    static constexpr int DIRECTIONS_COUNT = 6;

protected:
    void resizeEvent(QResizeEvent* event) override;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ObjectPreviewWidget::PreviewOptions)

} // namespace geck