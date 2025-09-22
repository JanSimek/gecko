#pragma once

#include <QWidget>
#include <QImage>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeyEvent>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/System/Clock.hpp>
#include <memory>

namespace geck {

class EditorWidget;

class SFMLWidget : public QWidget {
    Q_OBJECT

public:
    explicit SFMLWidget(QWidget* parent = nullptr);
    ~SFMLWidget();

    void setEditorWidget(EditorWidget* editorWidget);
    sf::RenderTexture* getRenderTarget();

    void handleSFMLEvent(const sf::Event& event);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

    // Drag and drop support
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

public slots:
    void updateAndRender();

private:
    bool ensureRenderTexture(const QSize& size);
    sf::Vector2f mapToWorld(const QPointF& pos) const;
    sf::Event createMouseEvent(QMouseEvent* qtEvent, bool isPressed) const;
    sf::Event createWheelEvent(QWheelEvent* qtEvent) const;
    sf::Keyboard::Key convertQtKeyToSf(int qtKey) const;

    std::unique_ptr<sf::RenderTexture> _renderTexture;
    EditorWidget* _editorWidget;
    sf::Clock _deltaClock;
    QImage _frameImage;
    bool _needsResize = true;
};

} // namespace geck
