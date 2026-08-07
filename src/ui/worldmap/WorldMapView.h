#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QWidget>

#include <memory>

namespace geck::worldmap {
class WorldMapScene;
struct AreaMarker;
}

namespace geck {

/// The worldmap canvas: draws the scene's image, pans, zooms, and reports what the pointer is over.
///
/// The image itself is produced by WorldMapScene in the engine's own palette maths, so this widget
/// only scales and blits it. The one thing it draws itself is the area labels — the engine renders
/// those with its bitmap font, which does not survive being scaled, so they are Qt text in the
/// engine's green instead.
class WorldMapView : public QWidget {
    Q_OBJECT

public:
    explicit WorldMapView(QWidget* parent = nullptr);
    ~WorldMapView() override;

    /// Takes over the scene to display; pass nullptr to clear. Resets the view to fit.
    void setScene(std::shared_ptr<worldmap::WorldMapScene> scene);
    [[nodiscard]] const worldmap::WorldMapScene* scene() const { return _scene.get(); }

    void setMarkersVisible(bool visible);
    void setLabelsVisible(bool visible);
    [[nodiscard]] bool labelsVisible() const { return _labelsVisible; }

    /// Scales the whole worldmap to fit the widget, and re-centres.
    void zoomToFit();
    /// Back to one screen pixel per worldmap pixel, centred on the view's middle.
    void zoomToActualSize();

    /// The area the user last clicked, or nullptr.
    [[nodiscard]] const worldmap::AreaMarker* selectedArea() const { return _selected; }
    /// Scrolls an area into the middle of the view and selects it.
    void revealArea(int areaIndex);

Q_SIGNALS:
    /// The user clicked an area (or empty terrain, in which case the pointer is null).
    void areaSelected(const geck::worldmap::AreaMarker* area);
    /// The user double-clicked an area — the editor opens its first map.
    void areaActivated(const geck::worldmap::AreaMarker* area);
    /// The pointer moved to a new worldmap pixel, over @p area (may be null). For a status bar.
    void hovered(int worldX, int worldY, const geck::worldmap::AreaMarker* area);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override; // tooltips

private:
    /// Widget coordinates -> worldmap pixel, and back.
    [[nodiscard]] QPointF toWorld(const QPointF& widgetPos) const;
    [[nodiscard]] QPointF toWidget(const QPointF& worldPos) const;

    void drawLabels(QPainter& painter) const;
    void setZoom(double zoom, const QPointF& anchorWidgetPos);
    void clampPan();
    /// Runs the pending fit once the widget has a real size to fit to.
    void fitIfPending();
    [[nodiscard]] QString tooltipFor(const worldmap::AreaMarker& area) const;

    std::shared_ptr<worldmap::WorldMapScene> _scene;
    QImage _image; ///< a view onto the scene's pixels, not a copy

    double _zoom = 1.0;
    QPointF _topLeft; ///< the worldmap pixel drawn at the widget's top-left corner
    /// A scene can arrive before the widget has been laid out, when width()/height() are still
    /// meaningless; the fit then waits for the first real size.
    bool _fitPending = false;

    bool _panning = false;
    QPoint _panAnchor;
    QPointF _panOrigin;
    bool _dragged = false;

    const worldmap::AreaMarker* _selected = nullptr;
    const worldmap::AreaMarker* _hovered = nullptr;
    bool _labelsVisible = true;
};

} // namespace geck
