#include "ui/worldmap/WorldMapView.h"

#include "editor/worldmap/WorldMapScene.h"
#include "ui/theme/ThemeManager.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace geck {

namespace {

    // Low enough that the whole 1400x1500 worldmap still fits a small window.
    constexpr double MIN_ZOOM = 0.1;
    constexpr double MAX_ZOOM = 8.0;
    constexpr double WHEEL_STEP = 1.15;

    // Fitting never magnifies: the art is 8-bit and 1:1 is as large as it is meant to be seen.
    constexpr double MAX_FIT_ZOOM = 1.0;

    // The engine puts the city name three pixels under its circle (wmInterfaceDrawCircleOverlay).
    constexpr int LABEL_GAP = 3;
    constexpr int LABEL_POINT_SIZE = 9;

    // Below this the labels crowd into each other and stop being readable, so they are dropped.
    constexpr double LABEL_MIN_ZOOM = 0.5;

} // namespace

WorldMapView::WorldMapView(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
    setAutoFillBackground(true);

    QPalette background = palette();
    background.setColor(QPalette::Window, QColor(ui::theme::colors::SURFACE_DARK));
    setPalette(background);
}

WorldMapView::~WorldMapView() = default;

void WorldMapView::setScene(std::shared_ptr<worldmap::WorldMapScene> scene) {
    _scene = std::move(scene);
    _selected = nullptr;
    _hovered = nullptr;

    if (_scene) {
        // QImage over the scene's buffer: no copy, but the scene has to outlive the image, which
        // the shared_ptr guarantees. Re-wrapped whenever the scene recomposes (see setMarkersVisible).
        _image = QImage(_scene->pixels().data(), _scene->width(), _scene->height(),
            _scene->width() * 4, QImage::Format_RGBA8888);
    } else {
        _image = QImage();
    }

    _fitPending = true;
    fitIfPending();
    update();
}

void WorldMapView::fitIfPending() {
    if (_fitPending && _scene && width() > 1 && height() > 1) {
        zoomToFit();
    }
}

void WorldMapView::setMarkersVisible(bool visible) {
    if (!_scene || _scene->markersVisible() == visible) {
        return;
    }
    _scene->setMarkersVisible(visible);
    // The recompose rewrote the buffer in place; the QImage still points at it, but tell Qt the
    // pixels changed by rebuilding the wrapper (QImage caches nothing else here).
    _image = QImage(_scene->pixels().data(), _scene->width(), _scene->height(),
        _scene->width() * 4, QImage::Format_RGBA8888);
    update();
}

void WorldMapView::setLabelsVisible(bool visible) {
    if (_labelsVisible == visible) {
        return;
    }
    _labelsVisible = visible;
    update();
}

QPointF WorldMapView::toWorld(const QPointF& widgetPos) const {
    return _topLeft + widgetPos / _zoom;
}

QPointF WorldMapView::toWidget(const QPointF& worldPos) const {
    return (worldPos - _topLeft) * _zoom;
}

void WorldMapView::zoomToFit() {
    if (!_scene || _scene->width() <= 0 || _scene->height() <= 0) {
        return;
    }
    // Stays armed: while the view is fitted, resizing the window refits it. Panning or zooming
    // disarms it, because from then on the user has chosen what to look at.
    _fitPending = true;
    const double fit = std::min(static_cast<double>(width()) / _scene->width(),
        static_cast<double>(height()) / _scene->height());
    _zoom = std::clamp(fit, MIN_ZOOM, MAX_FIT_ZOOM);
    _topLeft = QPointF(0, 0);
    clampPan();
    update();
}

void WorldMapView::zoomToActualSize() {
    if (!_scene) {
        return;
    }
    _fitPending = false;
    setZoom(1.0, QPointF(width() / 2.0, height() / 2.0));
}

void WorldMapView::setZoom(double zoom, const QPointF& anchorWidgetPos) {
    const double clamped = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
    if (std::abs(clamped - _zoom) < 1e-9) {
        return;
    }
    _fitPending = false; // the user picked a zoom; stop refitting on resize
    // Keep whatever is under the anchor pinned there.
    const QPointF anchorWorld = toWorld(anchorWidgetPos);
    _zoom = clamped;
    _topLeft = anchorWorld - anchorWidgetPos / _zoom;
    clampPan();
    update();
}

void WorldMapView::clampPan() {
    if (!_scene) {
        return;
    }
    const double visibleWidth = width() / _zoom;
    const double visibleHeight = height() / _zoom;

    // When the map is smaller than the view in an axis, centre it rather than pinning it left/top.
    if (_scene->width() <= visibleWidth) {
        _topLeft.setX((_scene->width() - visibleWidth) / 2.0);
    } else {
        _topLeft.setX(std::clamp(_topLeft.x(), 0.0, _scene->width() - visibleWidth));
    }
    if (_scene->height() <= visibleHeight) {
        _topLeft.setY((_scene->height() - visibleHeight) / 2.0);
    } else {
        _topLeft.setY(std::clamp(_topLeft.y(), 0.0, _scene->height() - visibleHeight));
    }
}

void WorldMapView::revealArea(int areaIndex) {
    if (!_scene) {
        return;
    }
    for (const worldmap::AreaMarker& area : _scene->areas()) {
        if (area.index != areaIndex) {
            continue;
        }
        _fitPending = false; // scrolling to an area is a deliberate view choice
        _selected = &area;
        _topLeft = QPointF(area.x + area.width / 2.0 - width() / (2.0 * _zoom),
            area.y + area.height / 2.0 - height() / (2.0 * _zoom));
        clampPan();
        update();
        Q_EMIT areaSelected(_selected);
        return;
    }
}

void WorldMapView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    if (_image.isNull()) {
        return;
    }

    // Nearest-neighbour keeps the 8-bit art crisp when magnified, which is how the game shows it;
    // smoothing only helps when shrinking below 1:1.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, _zoom < 1.0);

    const QRectF target(toWidget(QPointF(0, 0)), QSizeF(_image.width() * _zoom, _image.height() * _zoom));
    painter.drawImage(target, _image);

    if (_labelsVisible && _zoom >= LABEL_MIN_ZOOM) {
        drawLabels(painter);
    }

    if (_selected != nullptr) {
        const QRectF marker(toWidget(QPointF(_selected->x, _selected->y)),
            QSizeF(_selected->width * _zoom, _selected->height * _zoom));
        painter.setPen(QPen(QColor(ui::theme::colors::PRIMARY), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(marker.adjusted(-2, -2, 2, 2));
    }
}

void WorldMapView::drawLabels(QPainter& painter) const {
    const QColor green(QRgb(0xFF000000 | _scene->labelColor()));

    QFont font = painter.font();
    font.setPointSize(LABEL_POINT_SIZE);
    painter.setFont(font);
    const QFontMetrics metrics(font);

    const QRectF visible = rect();
    for (const worldmap::AreaMarker& area : _scene->areas()) {
        const QString label = QString::fromStdString(area.label());
        if (label.isEmpty()) {
            continue;
        }
        // Centred under the circle, as the engine places it.
        const QPointF anchor = toWidget(QPointF(area.x + area.width / 2.0, area.y + area.height + LABEL_GAP));
        const int textWidth = metrics.horizontalAdvance(label);
        const QRectF box(anchor.x() - textWidth / 2.0, anchor.y(), textWidth, metrics.height());
        if (!visible.intersects(box)) {
            continue;
        }

        // The engine draws the name shadowed so it stays legible over bright terrain.
        painter.setPen(Qt::black);
        painter.drawText(box.translated(1, 1), Qt::AlignCenter, label);
        painter.setPen(green);
        painter.drawText(box, Qt::AlignCenter, label);
    }
}

void WorldMapView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (_fitPending) {
        fitIfPending();
        return; // zoomToFit() clamps already
    }
    clampPan();
}

void WorldMapView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    fitIfPending();
}

void WorldMapView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && _scene) {
        _panning = true;
        _dragged = false;
        _panAnchor = event->pos();
        _panOrigin = _topLeft;
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void WorldMapView::mouseMoveEvent(QMouseEvent* event) {
    if (!_scene) {
        return;
    }

    if (_panning) {
        const QPointF delta = event->position() - QPointF(_panAnchor);
        if (delta.manhattanLength() > 2) {
            _dragged = true;
            _fitPending = false; // the user chose where to look
        }
        _topLeft = _panOrigin - delta / _zoom;
        clampPan();
        update();
        return;
    }

    const QPointF world = toWorld(event->position());
    const auto worldX = static_cast<int>(std::floor(world.x()));
    const auto worldY = static_cast<int>(std::floor(world.y()));
    const worldmap::AreaMarker* area = _scene->areaAt(worldX, worldY);
    if (area != _hovered) {
        _hovered = area;
        setCursor(area != nullptr ? Qt::PointingHandCursor : Qt::OpenHandCursor);
    }
    Q_EMIT hovered(worldX, worldY, area);
}

void WorldMapView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && _panning) {
        _panning = false;
        setCursor(_hovered != nullptr ? Qt::PointingHandCursor : Qt::OpenHandCursor);

        // A drag pans; only a click selects.
        if (!_dragged && _scene) {
            const QPointF world = toWorld(event->position());
            _selected = _scene->areaAt(static_cast<int>(std::floor(world.x())),
                static_cast<int>(std::floor(world.y())));
            update();
            Q_EMIT areaSelected(_selected);
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void WorldMapView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && _scene) {
        const QPointF world = toWorld(event->position());
        const worldmap::AreaMarker* area = _scene->areaAt(static_cast<int>(std::floor(world.x())),
            static_cast<int>(std::floor(world.y())));
        if (area != nullptr) {
            _selected = area;
            update();
            Q_EMIT areaActivated(area);
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void WorldMapView::wheelEvent(QWheelEvent* event) {
    if (!_scene) {
        return;
    }
    const int steps = event->angleDelta().y();
    if (steps == 0) {
        return;
    }
    setZoom(_zoom * std::pow(WHEEL_STEP, steps / 120.0), event->position());
    event->accept();
}

void WorldMapView::leaveEvent(QEvent* event) {
    _hovered = nullptr;
    QWidget::leaveEvent(event);
}

bool WorldMapView::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip && _scene != nullptr) {
        const auto* help = static_cast<const QHelpEvent*>(event);
        const QPointF world = toWorld(QPointF(help->pos()));
        if (const worldmap::AreaMarker* area = _scene->areaAt(static_cast<int>(std::floor(world.x())),
                static_cast<int>(std::floor(world.y())));
            area != nullptr) {
            QToolTip::showText(help->globalPos(), tooltipFor(*area), this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

QString WorldMapView::tooltipFor(const worldmap::AreaMarker& area) const {
    // The tooltip is rich text and every string in it comes out of game data files, so escape them
    // rather than trust that no name ever contains '<' or '&'.
    const auto escaped = [](const std::string& value) { return QString::fromStdString(value).toHtmlEscaped(); };

    QString text = QStringLiteral("<b>%1</b>").arg(escaped(area.label()));
    if (area.label() != area.name) {
        text += QStringLiteral(" <i>(%1)</i>").arg(escaped(area.name));
    }
    text += QStringLiteral("<br>Area %1 &middot; %2")
                .arg(area.index)
                .arg(QString::fromLatin1(cityAreaSizeName(area.size)));
    if (!area.terrain.empty()) {
        text += QStringLiteral(" &middot; %1").arg(escaped(area.terrain));
    }
    text += QStringLiteral("<br>%1").arg(area.knownAtStart ? QStringLiteral("Known at start")
                                                           : QStringLiteral("Discovered through play"));
    if (!area.mapFiles.empty()) {
        QStringList maps;
        for (const std::string& mapFile : area.mapFiles) {
            maps << escaped(mapFile);
        }
        text += QStringLiteral("<br>%1").arg(maps.join(QStringLiteral(", ")));
    }
    return text;
}

} // namespace geck
