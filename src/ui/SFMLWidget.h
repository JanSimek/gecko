#pragma once

#include <QWidget>
#include <QTimer>
#include <SFML/Graphics.hpp>
#include <memory>

namespace geck {

class EditorWidget;

class SFMLWidget : public QWidget {
    Q_OBJECT

public:
    explicit SFMLWidget(QWidget* parent = nullptr);
    ~SFMLWidget();

    void setEditorWidget(EditorWidget* editorWidget);
    sf::RenderWindow* getRenderWindow() const { return _renderWindow.get(); }

    void handleSFMLEvent(const sf::Event& event);

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    QPaintEngine* paintEngine() const override;

public slots:
    void updateAndRender();

private:
    void convertQtMouseEventToSFML(QMouseEvent* qtEvent, sf::Event& sfmlEvent, sf::Event::EventType type);
    void convertQtWheelEventToSFML(QWheelEvent* qtEvent, sf::Event& sfmlEvent);

    std::unique_ptr<sf::RenderWindow> _renderWindow;
    EditorWidget* _editorWidget;
    sf::Clock _deltaClock;
    bool _initialized;
};

} // namespace geck