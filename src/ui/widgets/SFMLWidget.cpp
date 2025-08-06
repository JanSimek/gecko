#include "SFMLWidget.h"
#include "../core/EditorWidget.h"

#include <QShowEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <spdlog/spdlog.h>
#include <cstdint>

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

    // Enable drag and drop
    setAcceptDrops(true);

    // Set size policy to expand and fill available space
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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
        sf::WindowHandle handle = reinterpret_cast<sf::WindowHandle>(static_cast<uintptr_t>(winId()));
        _renderWindow = std::make_unique<sf::RenderWindow>(handle);

        if (_renderWindow) {
            // Configure for pixel-perfect rendering (matching Fallout 2 style)
            _renderWindow->setVerticalSyncEnabled(true);
            
            // Disable smoothing for pixel-perfect rendering
            sf::View defaultView = _renderWindow->getDefaultView();
            _renderWindow->setView(defaultView);
            
            _initialized = true;
            spdlog::info("SFML render window created successfully with pixel-perfect settings");
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
        // For embedded SFML windows, we MUST explicitly resize the render window
        // SFML only handles automatic resizing for standalone windows
        sf::Vector2u newSize(event->size().width(), event->size().height());
        _renderWindow->setSize(newSize);

        spdlog::debug("SFMLWidget resize: {}x{}, widget geometry: ({}, {}, {}x{})",
            newSize.x, newSize.y,
            geometry().x(), geometry().y(),
            geometry().width(), geometry().height());

        // Don't set view size here - let EditorWidget handle view management
        // This prevents conflicting view configurations

        // Convert resize event to SFML event and forward to state machine
        sf::Event sfmlEvent = sf::Event::Resized{
            { static_cast<unsigned int>(event->size().width()),
                static_cast<unsigned int>(event->size().height()) }
        };

        if (_editorWidget) {
            _editorWidget->handleEvent(sfmlEvent);
        }
    }

    QWidget::resizeEvent(event);
}

void SFMLWidget::mousePressEvent(QMouseEvent* event) {
    // Note: SFML handles mouse events through its own polling loop via handleSFMLEvent()
    // Forwarding Qt mouse events here causes duplicate event processing
    // Let SFML handle all mouse events to avoid duplicates
    
    QWidget::mousePressEvent(event);
}

void SFMLWidget::mouseReleaseEvent(QMouseEvent* event) {
    // Note: SFML handles mouse events through its own polling loop via handleSFMLEvent()
    // Forwarding Qt mouse events here causes duplicate event processing
    // Let SFML handle all mouse events to avoid duplicates
    
    QWidget::mouseReleaseEvent(event);
}

void SFMLWidget::mouseMoveEvent(QMouseEvent* event) {
    // Forward Qt mouse events to SFML since Qt correctly handles the full widget area
    if (_renderWindow && _editorWidget) {
        sf::Event sfmlEvent = sf::Event::MouseMoved{
            { event->pos().x(), event->pos().y() }
        };
        _editorWidget->handleEvent(sfmlEvent);
    }

    QWidget::mouseMoveEvent(event);
}

void SFMLWidget::wheelEvent(QWheelEvent* event) {
    sf::Event sfmlEvent{ sf::Event::MouseWheelScrolled{ sf::Mouse::Wheel::Vertical, 0.0f, sf::Vector2i{ 0, 0 } } };
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
        // Skip mouse movement events - we handle those through Qt for proper coordinate handling
        if (event.is<sf::Event::MouseMoved>()) {
            return;
        }
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
    sf::Vector2i position{ static_cast<int>(qtEvent->position().x()), static_cast<int>(qtEvent->position().y()) };
    if (isPressed) {
        sfmlEvent = sf::Event::MouseButtonPressed{ button, position };
    } else {
        sfmlEvent = sf::Event::MouseButtonReleased{ button, position };
    }
}

void SFMLWidget::convertQtWheelEventToSFML(QWheelEvent* qtEvent, sf::Event& sfmlEvent) {
    sfmlEvent = sf::Event::MouseWheelScrolled{
        sf::Mouse::Wheel::Vertical,
        qtEvent->angleDelta().y() / 120.0f, // Convert to wheel steps
        sf::Vector2i{ static_cast<int>(qtEvent->position().x()), static_cast<int>(qtEvent->position().y()) }
    };
}

void SFMLWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-geck-object")) {
        // Extract object data and start drag preview
        QByteArray objectData = event->mimeData()->data("application/x-geck-object");
        QStringList parts = QString::fromUtf8(objectData).split(',');
        
        if (parts.size() == 2 && _editorWidget) {
            int objectIndex = parts[0].toInt();
            int categoryInt = parts[1].toInt();
            
            // Convert Qt coordinates to SFML world coordinates
            sf::Vector2f worldPos;
            if (_renderWindow) {
                sf::Vector2i windowPos(event->position().x(), event->position().y());
                worldPos = _renderWindow->mapPixelToCoords(windowPos);
            }
            
            // Start drag preview in editor
            _editorWidget->startDragPreview(objectIndex, categoryInt, worldPos);
        }
        
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void SFMLWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat("application/x-geck-object")) {
        // Update drag preview position
        if (_editorWidget && _renderWindow) {
            sf::Vector2i windowPos(event->position().x(), event->position().y());
            sf::Vector2f worldPos = _renderWindow->mapPixelToCoords(windowPos);
            _editorWidget->updateDragPreview(worldPos);
        }
        
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void SFMLWidget::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasFormat("application/x-geck-object")) {
        // Finish the drag preview and place the object
        if (_editorWidget && _renderWindow) {
            sf::Vector2i windowPos(event->position().x(), event->position().y());
            sf::Vector2f worldPos = _renderWindow->mapPixelToCoords(windowPos);
            _editorWidget->finishDragPreview(worldPos);
        }
        
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

} // namespace geck