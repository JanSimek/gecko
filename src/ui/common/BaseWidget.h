#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <memory>

namespace geck {

/**
 * @brief Base class for common widget functionality
 * 
 * Provides shared utilities and standard implementations to reduce
 * code duplication across UI widgets following DRY principle.
 */
class BaseWidget : public QWidget {
    Q_OBJECT

public:
    explicit BaseWidget(QWidget* parent = nullptr) : QWidget(parent) {}
    virtual ~BaseWidget() = default;

protected:
    /**
     * @brief Creates and sets up a standard vertical layout
     * @return Pointer to the created layout
     */
    [[nodiscard]] QVBoxLayout* setupStandardVBoxLayout() noexcept {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(4);
        layout->setContentsMargins(4, 4, 4, 4);
        return layout;
    }

    /**
     * @brief Creates and sets up a standard horizontal layout
     * @return Pointer to the created layout
     */
    [[nodiscard]] QHBoxLayout* setupStandardHBoxLayout() noexcept {
        auto* layout = new QHBoxLayout(this);
        layout->setSpacing(4);
        layout->setContentsMargins(4, 4, 4, 4);
        return layout;
    }

    /**
     * @brief Applies consistent selection styling
     * @param selected Whether the widget is selected
     */
    void applySelectionStyle(bool selected) noexcept {
        if (selected) {
            setStyleSheet("border: 2px solid #4A90E2; background-color: #E6F2FF;");
        } else {
            setStyleSheet("border: 1px solid gray; background-color: white;");
        }
    }

public:
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

protected:
};

} // namespace geck