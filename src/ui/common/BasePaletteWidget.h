#pragma once

#include "../theme/ThemeManager.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QApplication>
#include <QPainter>
#include <QPen>
#include <functional>

namespace geck {

/**
 * @brief Base class for palette widgets (tiles and objects)
 *
 * Provides common functionality for selectable palette items,
 * eliminating duplication between TileWidget and ObjectWidget.
 */
class BasePaletteWidget : public QLabel {
    Q_OBJECT

public:
    explicit BasePaletteWidget(int index, QWidget* parent = nullptr)
        : QLabel(parent)
        , _index(index)
        , _selected(false) {
        setCursor(Qt::PointingHandCursor);
        setAlignment(Qt::AlignCenter);
    }

    virtual ~BasePaletteWidget() = default;

    [[nodiscard]] int getIndex() const noexcept { return _index; }
    [[nodiscard]] bool isSelected() const noexcept { return _selected; }

    virtual void setSelected(bool selected) {
        if (_selected != selected) {
            _selected = selected;
            update(); // Trigger repaint
        }
    }

signals:
    void clicked(int index);
    void rightClicked(int index);
    void dragStarted(int index);

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            _dragStartPosition = event->pos();
            Q_EMIT clicked(_index);
        } else if (event->button() == Qt::RightButton) {
            Q_EMIT rightClicked(_index);
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton)) {
            QLabel::mouseMoveEvent(event);
            return;
        }

        if ((event->pos() - _dragStartPosition).manhattanLength()
            < QApplication::startDragDistance()) {
            QLabel::mouseMoveEvent(event);
            return;
        }

        Q_EMIT dragStarted(_index);
    }

    void paintEvent(QPaintEvent* event) override {
        QLabel::paintEvent(event);

        if (_selected) {
            QPainter painter(this);
            painter.setPen(QPen(ui::theme::colors::primary(), 3));
            painter.drawRect(rect().adjusted(1, 1, -1, -1));
        }
    }

    /**
     * @brief Sets up common widget properties
     * @param size Widget size
     * @param borderSpace Additional border space
     */
    void setupCommonProperties(int size, int borderSpace = 4) {
        setFixedSize(size + borderSpace, size + borderSpace);
        setFrameStyle(QFrame::Box);
    }

private:
    int _index;
    bool _selected;
    QPoint _dragStartPosition;
};

} // namespace geck