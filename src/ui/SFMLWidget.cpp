#include "SFMLWidget.h"
#include "EditorWidget.h"

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
    , _editorWidget(nullptr)
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

void SFMLWidget::setEditorWidget(EditorWidget* editorWidget) {
    _editorWidget = editorWidget;
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
        // Update SFML render window size to match the widget
        sf::Vector2u newSize(event->size().width(), event->size().height());
        
        spdlog::debug("SFMLWidget resize: {}x{}", newSize.x, newSize.y);
        
        _renderWindow->setSize(newSize);
        
        // Don't set view size here - let EditorState handle view management
        // This prevents conflicting view configurations
        
        // Convert resize event to SFML event and forward to state machine
        sf::Event sfmlEvent = sf::Event::Resized{
                {
                    static_cast<unsigned int>(event->size().width()),
                   static_cast<unsigned int>(event->size().height())
                }
        };
        
        if (_editorWidget) {
            _editorWidget->handleEvent(sfmlEvent);
        }
    }
    
    QWidget::resizeEvent(event);
}

void SFMLWidget::mousePressEvent(QMouseEvent* event) {
    // Don't forward Qt mouse events to SFML - SFML handles them natively
    // This prevents duplicate event processing
    QWidget::mousePressEvent(event);
}

void SFMLWidget::mouseReleaseEvent(QMouseEvent* event) {
    // Don't forward Qt mouse events to SFML - SFML handles them natively
    // This prevents duplicate event processing
    QWidget::mouseReleaseEvent(event);
}

void SFMLWidget::mouseMoveEvent(QMouseEvent* event) {
    // Don't forward Qt mouse events to SFML - SFML handles them natively
    // This prevents duplicate event processing
    QWidget::mouseMoveEvent(event);
}

void SFMLWidget::wheelEvent(QWheelEvent* event) {
    sf::Event sfmlEvent{sf::Event::MouseWheelScrolled{sf::Mouse::Wheel::Vertical, 0.0f, sf::Vector2i{0, 0}}};
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
    
    // Process SFML events (SFML 3 style)
    while (const std::optional event = _renderWindow->pollEvent()) {
        handleSFMLEvent(*event);
    }
    
    // Update and render current state
    if (_editorWidget) {
        _editorWidget->update(deltaTime);
        
        _renderWindow->clear(sf::Color::Black);
        _editorWidget->render(deltaTime);
        _renderWindow->display();
    }
}

void SFMLWidget::handleSFMLEvent(const sf::Event& event) {
    if (_editorWidget) {
        _editorWidget->handleEvent(event);
    }
}

void SFMLWidget::convertQtMouseEventToSFML(QMouseEvent* qtEvent, sf::Event& sfmlEvent, bool isPressed) {
    // Convert mouse button
    sf::Mouse::Button button = sf::Mouse::Button::Left;
    switch (qtEvent->button()) {
        case Qt::LeftButton:
            button = sf::Mouse::Button::Left;
            break;
        case Qt::RightButton:
            button = sf::Mouse::Button::Right;
            break;
        case Qt::MiddleButton:
            button = sf::Mouse::Button::Middle;
            break;
        default:
            button = sf::Mouse::Button::Left;
            break;
    }
    
    // Create the appropriate SFML 3 event
    sf::Vector2i position{static_cast<int>(qtEvent->position().x()), static_cast<int>(qtEvent->position().y())};
    if (isPressed) {
        sfmlEvent = sf::Event::MouseButtonPressed{button, position};
    } else {
        sfmlEvent = sf::Event::MouseButtonReleased{button, position};
    }
}

void SFMLWidget::convertQtWheelEventToSFML(QWheelEvent* qtEvent, sf::Event& sfmlEvent) {
    sfmlEvent = sf::Event::MouseWheelScrolled{
        sf::Mouse::Wheel::Vertical,
        qtEvent->angleDelta().y() / 120.0f, // Convert to wheel steps
        sf::Vector2i{static_cast<int>(qtEvent->position().x()), static_cast<int>(qtEvent->position().y())}
    };
}

} // namespace geck