#include "SFMLWidget.h"
#include "../state/StateMachine.h"
#include "../state/State.h"

#include <QShowEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <spdlog/spdlog.h>

#ifdef Q_WS_X11
    #include <Qt/qx11info_x11.h>
    #include <X11/Xlib.h>
#endif

namespace geck {

SFMLWidget::SFMLWidget(QWidget* parent)
    : QWidget(parent)
    , _renderWindow(nullptr)
    , _stateMachine(nullptr)
    , _initialized(false) {
    
    // Set widget attributes for SFML rendering
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    
    // Enable mouse tracking
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

SFMLWidget::~SFMLWidget() {
    if (_renderWindow) {
        _renderWindow.reset();
    }
}

void SFMLWidget::setStateMachine(std::shared_ptr<StateMachine> stateMachine) {
    _stateMachine = stateMachine;
}

void SFMLWidget::showEvent(QShowEvent* event) {
    if (!_initialized) {
        // Create SFML render window using the widget's window handle
        sf::WindowHandle handle = reinterpret_cast<sf::WindowHandle>(winId());
        _renderWindow = std::make_unique<sf::RenderWindow>(handle);
        
        if (_renderWindow) {
            _renderWindow->setVerticalSyncEnabled(true);
            _initialized = true;
            spdlog::info("SFML render window created successfully");
        } else {
            spdlog::error("Failed to create SFML render window");
        }
    }
    
    QWidget::showEvent(event);
}

void SFMLWidget::paintEvent(QPaintEvent* event) {
    // Do nothing - SFML handles the rendering
    Q_UNUSED(event)
}

void SFMLWidget::resizeEvent(QResizeEvent* event) {
    if (_renderWindow) {
        // Convert resize event to SFML event and forward to state machine
        sf::Event sfmlEvent;
        sfmlEvent.type = sf::Event::Resized;
        sfmlEvent.size.width = event->size().width();
        sfmlEvent.size.height = event->size().height();
        
        if (_stateMachine && !_stateMachine->empty()) {
            _stateMachine->top().handleEvent(sfmlEvent);
        }
    }
    
    QWidget::resizeEvent(event);
}

void SFMLWidget::mousePressEvent(QMouseEvent* event) {
    sf::Event sfmlEvent;
    convertQtMouseEventToSFML(event, sfmlEvent, sf::Event::MouseButtonPressed);
    handleSFMLEvent(sfmlEvent);
    
    QWidget::mousePressEvent(event);
}

void SFMLWidget::mouseReleaseEvent(QMouseEvent* event) {
    sf::Event sfmlEvent;
    convertQtMouseEventToSFML(event, sfmlEvent, sf::Event::MouseButtonReleased);
    handleSFMLEvent(sfmlEvent);
    
    QWidget::mouseReleaseEvent(event);
}

void SFMLWidget::mouseMoveEvent(QMouseEvent* event) {
    sf::Event sfmlEvent;
    sfmlEvent.type = sf::Event::MouseMoved;
    sfmlEvent.mouseMove.x = event->position().x();
    sfmlEvent.mouseMove.y = event->position().y();
    handleSFMLEvent(sfmlEvent);
    
    QWidget::mouseMoveEvent(event);
}

void SFMLWidget::wheelEvent(QWheelEvent* event) {
    sf::Event sfmlEvent;
    convertQtWheelEventToSFML(event, sfmlEvent);
    handleSFMLEvent(sfmlEvent);
    
    QWidget::wheelEvent(event);
}

QPaintEngine* SFMLWidget::paintEngine() const {
    // Disable Qt's painting system
    return nullptr;
}

void SFMLWidget::updateAndRender() {
    if (!_renderWindow || !_initialized) {
        return;
    }
    
    // Calculate delta time
    float deltaTime = _deltaClock.restart().asSeconds();
    
    // Process SFML events
    sf::Event event;
    while (_renderWindow->pollEvent(event)) {
        handleSFMLEvent(event);
    }
    
    // Update and render current state
    if (_stateMachine && !_stateMachine->empty()) {
        _stateMachine->top().update(deltaTime);
        
        _renderWindow->clear(sf::Color::Black);
        _stateMachine->top().render(deltaTime);
        _renderWindow->display();
    }
}

void SFMLWidget::handleSFMLEvent(const sf::Event& event) {
    if (_stateMachine && !_stateMachine->empty()) {
        _stateMachine->top().handleEvent(event);
    }
}

void SFMLWidget::convertQtMouseEventToSFML(QMouseEvent* qtEvent, sf::Event& sfmlEvent, sf::Event::EventType type) {
    sfmlEvent.type = type;
    
    // Convert mouse button
    switch (qtEvent->button()) {
        case Qt::LeftButton:
            sfmlEvent.mouseButton.button = sf::Mouse::Left;
            break;
        case Qt::RightButton:
            sfmlEvent.mouseButton.button = sf::Mouse::Right;
            break;
        case Qt::MiddleButton:
            sfmlEvent.mouseButton.button = sf::Mouse::Middle;
            break;
        default:
            sfmlEvent.mouseButton.button = sf::Mouse::Left;
            break;
    }
    
    sfmlEvent.mouseButton.x = qtEvent->position().x();
    sfmlEvent.mouseButton.y = qtEvent->position().y();
}

void SFMLWidget::convertQtWheelEventToSFML(QWheelEvent* qtEvent, sf::Event& sfmlEvent) {
    sfmlEvent.type = sf::Event::MouseWheelScrolled;
    sfmlEvent.mouseWheelScroll.wheel = sf::Mouse::VerticalWheel;
    sfmlEvent.mouseWheelScroll.delta = qtEvent->angleDelta().y() / 120.0f; // Convert to wheel steps
    sfmlEvent.mouseWheelScroll.x = qtEvent->position().x();
    sfmlEvent.mouseWheelScroll.y = qtEvent->position().y();
}

} // namespace geck