#include "BaseWidget.h"
#include <QPainter>

namespace geck {

QPixmap BaseWidget::createCenteredPixmap(const QPixmap& pixmap, int canvasSize) {
    QPixmap canvas(canvasSize, canvasSize);
    canvas.fill(Qt::transparent);
    
    QPainter painter(&canvas);
    const int x = (canvasSize - pixmap.width()) / 2;
    const int y = (canvasSize - pixmap.height()) / 2;
    painter.drawPixmap(x, y, pixmap);
    
    return canvas;
}

} // namespace geck