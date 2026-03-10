#pragma once

#include <QWidget>
#include <QPixmap>

namespace geck {

/**
 * @brief Base class for common widget functionality
 *
 * Provides shared utilities and standard implementations used by UI widgets.
 */
class BaseWidget : public QWidget {
    Q_OBJECT

public:
    explicit BaseWidget(QWidget* parent = nullptr)
        : QWidget(parent) { }
    virtual ~BaseWidget() = default;
    /**
     * @brief Scales a pixmap to specified size while maintaining aspect ratio
     * @param pixmap Source pixmap
     * @param size Target size
     * @return Scaled pixmap
     */
    [[nodiscard]] static QPixmap scalePixmapToSize(const QPixmap& pixmap, int size) {
        return pixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    /**
     * @brief Creates a centered pixmap on a square canvas
     * @param pixmap Source pixmap
     * @param canvasSize Size of the square canvas
     * @return Centered pixmap on transparent background
     */
    [[nodiscard]] static QPixmap createCenteredPixmap(const QPixmap& pixmap, int canvasSize);
};

} // namespace geck
