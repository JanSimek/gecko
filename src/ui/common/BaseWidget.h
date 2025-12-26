#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <memory>

#include "../theme/ThemeManager.h"

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
    explicit BaseWidget(QWidget* parent = nullptr)
        : QWidget(parent) { }
    virtual ~BaseWidget() = default;

protected:
    /**
     * @brief Creates and sets up a standard vertical layout with tight spacing
     * @return Pointer to the created layout
     *
     * For looser layouts, use setupLooseVBoxLayout() or set spacing manually.
     */
    [[nodiscard]] QVBoxLayout* setupStandardVBoxLayout() noexcept {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(ui::theme::spacing::TIGHT);
        layout->setContentsMargins(
            ui::theme::spacing::MARGIN_TIGHT,
            ui::theme::spacing::MARGIN_TIGHT,
            ui::theme::spacing::MARGIN_TIGHT,
            ui::theme::spacing::MARGIN_TIGHT);
        return layout;
    }

    /**
     * @brief Creates and sets up a standard horizontal layout with tight spacing
     * @return Pointer to the created layout
     */
    [[nodiscard]] QHBoxLayout* setupStandardHBoxLayout() noexcept {
        auto* layout = new QHBoxLayout(this);
        layout->setSpacing(ui::theme::spacing::TIGHT);
        layout->setContentsMargins(
            ui::theme::spacing::MARGIN_TIGHT,
            ui::theme::spacing::MARGIN_TIGHT,
            ui::theme::spacing::MARGIN_TIGHT,
            ui::theme::spacing::MARGIN_TIGHT);
        return layout;
    }

    /**
     * @brief Creates a vertical layout with normal spacing for dialogs/panels
     * @return Pointer to the created layout
     */
    [[nodiscard]] QVBoxLayout* setupLooseVBoxLayout() noexcept {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(ui::theme::spacing::LOOSE);
        layout->setContentsMargins(
            ui::theme::spacing::MARGIN_LOOSE,
            ui::theme::spacing::MARGIN_LOOSE,
            ui::theme::spacing::MARGIN_LOOSE,
            ui::theme::spacing::MARGIN_LOOSE);
        return layout;
    }

    /**
     * @brief Applies consistent selection styling using theme colors
     * @param selected Whether the widget is selected
     */
    void applySelectionStyle(bool selected) noexcept {
        if (selected) {
            setStyleSheet(ui::theme::styles::selectedWidget());
        } else {
            setStyleSheet(ui::theme::styles::normalWidget());
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