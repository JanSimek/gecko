#include "SFMLWidget.h"
#include "../core/EditorWidget.h"
#include "../dragdrop/MimeTypes.h"

#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QCoreApplication>
#include <algorithm>
#include <type_traits>
#include <spdlog/spdlog.h>
#include <SFML/Graphics/Image.hpp>

#ifdef Q_WS_X11
#include <Qt/qx11info_x11.h>
#include <X11/Xlib.h>
#endif

namespace geck {

SFMLWidget::SFMLWidget(QWidget* parent)
    : QWidget(parent)
    , _renderTexture(std::make_unique<sf::RenderTexture>())
    , _editorWidget(nullptr) {

    this->setAttribute(Qt::WA_NoSystemBackground);
    this->setAttribute(Qt::WA_DontCreateNativeAncestors);

    // Enable mouse tracking
    this->setMouseTracking(true);
    this->setFocusPolicy(Qt::StrongFocus);

    // Enable drag and drop
    this->setAcceptDrops(true);

    // Set size policy to expand and fill available space
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

SFMLWidget::~SFMLWidget() = default;

void SFMLWidget::setEditorWidget(EditorWidget* editorWidget) {
    _editorWidget = editorWidget;
}

sf::RenderTexture* SFMLWidget::getRenderTarget() {
    if (!ensureRenderTexture(this->size())) {
        return nullptr;
    }
    return _renderTexture.get();
}

void SFMLWidget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (!_frameImage.isNull()) {
        painter.drawImage(rect(), _frameImage);
    }
}

void SFMLWidget::resizeEvent(QResizeEvent* event) {
    _needsResize = true;

    if (_editorWidget) {
        sf::Event sfmlEvent = sf::Event::Resized{
            { static_cast<unsigned int>(event->size().width()),
                static_cast<unsigned int>(event->size().height()) }
        };
        _editorWidget->handleEvent(sfmlEvent);
    }

    QWidget::resizeEvent(event);
}

void SFMLWidget::mousePressEvent(QMouseEvent* event) {
    if (event && _editorWidget) {
        handleSFMLEvent(createMouseEvent(event, true));
    }
    QWidget::mousePressEvent(event);
}

void SFMLWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event && _editorWidget) {
        handleSFMLEvent(createMouseEvent(event, false));
    }
    QWidget::mouseReleaseEvent(event);
}

void SFMLWidget::mouseMoveEvent(QMouseEvent* event) {
    if (event && _editorWidget) {
        handleSFMLEvent(sf::Event::MouseMoved{ { event->pos().x(), event->pos().y() } });
    }

    QWidget::mouseMoveEvent(event);
}

void SFMLWidget::wheelEvent(QWheelEvent* event) {
    if (event && _editorWidget) {
        handleSFMLEvent(createWheelEvent(event));
    }

    QWidget::wheelEvent(event);
}

void SFMLWidget::keyPressEvent(QKeyEvent* event) {
    if (event && _editorWidget && !event->isAutoRepeat()) {
        const auto key = convertQtKeyToSf(event->key());
        if (key != sf::Keyboard::Key::Unknown) {
            handleSFMLEvent(sf::Event::KeyPressed{ key });
        }
    }

    QWidget::keyPressEvent(event);
}

void SFMLWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event && _editorWidget && !event->isAutoRepeat()) {
        const auto key = convertQtKeyToSf(event->key());
        if (key != sf::Keyboard::Key::Unknown) {
            handleSFMLEvent(sf::Event::KeyReleased{ key });
        }
    }

    QWidget::keyReleaseEvent(event);
}

void SFMLWidget::updateAndRender() {
    if (!_editorWidget) {
        return;
    }

    const QSize currentSize = size();
    if (currentSize.width() <= 0 || currentSize.height() <= 0) {
        return;
    }

    if (!ensureRenderTexture(currentSize)) {
        return;
    }

    float deltaTime = _deltaClock.restart().asSeconds();

    _editorWidget->update(deltaTime);

    _renderTexture->clear(sf::Color::Black);
    _editorWidget->render(*_renderTexture, deltaTime);
    _renderTexture->display();

    sf::Image image = _renderTexture->getTexture().copyToImage();
    _frameImage = QImage(image.getPixelsPtr(),
        static_cast<int>(image.getSize().x),
        static_cast<int>(image.getSize().y),
        QImage::Format_RGBA8888)
                      .copy();

    update();
}

bool SFMLWidget::ensureRenderTexture(const QSize& size) {
    if (!_renderTexture) {
        _renderTexture = std::make_unique<sf::RenderTexture>();
        _needsResize = true;
    }

    const unsigned int targetWidth = static_cast<unsigned int>(std::max(1, size.width()));
    const unsigned int targetHeight = static_cast<unsigned int>(std::max(1, size.height()));

    if (_needsResize || _renderTexture->getSize().x != targetWidth || _renderTexture->getSize().y != targetHeight) {
        if (!_renderTexture->resize(sf::Vector2u{ targetWidth, targetHeight })) {
            spdlog::error("SFMLWidget: Failed to resize render texture {}x{}", targetWidth, targetHeight);
            return false;
        }

        _renderTexture->setSmooth(false);
        const sf::FloatRect viewRect(sf::Vector2f{ 0.f, 0.f },
            sf::Vector2f{ static_cast<float>(targetWidth), static_cast<float>(targetHeight) });
        _renderTexture->setView(sf::View(viewRect));
        _needsResize = false;
    }

    return true;
}

void SFMLWidget::handleSFMLEvent(const sf::Event& event) {
    if (_editorWidget) {
        _editorWidget->handleEvent(event);
    }
}

sf::Event SFMLWidget::createMouseEvent(QMouseEvent* qtEvent, bool isPressed) const {
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
        return sf::Event::MouseButtonPressed{ button, position };
    }
    return sf::Event::MouseButtonReleased{ button, position };
}

sf::Event SFMLWidget::createWheelEvent(QWheelEvent* qtEvent) const {
    return sf::Event::MouseWheelScrolled{
        sf::Mouse::Wheel::Vertical,
        qtEvent->angleDelta().y() / 120.0f,
        { static_cast<int>(qtEvent->position().x()), static_cast<int>(qtEvent->position().y()) }
    };
}

sf::Vector2f SFMLWidget::mapToWorld(const QPointF& pos) const {
    if (_needsResize) {
        const_cast<SFMLWidget*>(this)->ensureRenderTexture(size());
    }
    if (_renderTexture && _renderTexture->getSize().x > 0 && _renderTexture->getSize().y > 0) {
        return _renderTexture->mapPixelToCoords(sf::Vector2i(static_cast<int>(pos.x()), static_cast<int>(pos.y())));
    }
    return { static_cast<float>(pos.x()), static_cast<float>(pos.y()) };
}

sf::Keyboard::Key SFMLWidget::convertQtKeyToSf(int qtKey) const {
    using sf::Keyboard::Key;

    const auto toKey = [](Key base, int offset) {
        using Underlying = std::underlying_type_t<Key>;
        return static_cast<Key>(static_cast<Underlying>(base) + offset);
    };

    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        return toKey(Key::A, qtKey - Qt::Key_A);
    }

    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        return toKey(Key::Num0, qtKey - Qt::Key_0);
    }

    switch (qtKey) {
        case Qt::Key_Escape:
            return Key::Escape;
        case Qt::Key_Tab:
            return Key::Tab;
        case Qt::Key_Backspace:
            return Key::Backspace;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return Key::Enter;
        case Qt::Key_Space:
            return Key::Space;
        case Qt::Key_Left:
            return Key::Left;
        case Qt::Key_Right:
            return Key::Right;
        case Qt::Key_Up:
            return Key::Up;
        case Qt::Key_Down:
            return Key::Down;
        case Qt::Key_PageUp:
            return Key::PageUp;
        case Qt::Key_PageDown:
            return Key::PageDown;
        case Qt::Key_Home:
            return Key::Home;
        case Qt::Key_End:
            return Key::End;
        case Qt::Key_Delete:
            return Key::Delete;
        case Qt::Key_Insert:
            return Key::Insert;
        case Qt::Key_F1:
            return Key::F1;
        case Qt::Key_F2:
            return Key::F2;
        case Qt::Key_F3:
            return Key::F3;
        case Qt::Key_F4:
            return Key::F4;
        case Qt::Key_F5:
            return Key::F5;
        case Qt::Key_F6:
            return Key::F6;
        case Qt::Key_F7:
            return Key::F7;
        case Qt::Key_F8:
            return Key::F8;
        case Qt::Key_F9:
            return Key::F9;
        case Qt::Key_F10:
            return Key::F10;
        case Qt::Key_F11:
            return Key::F11;
        case Qt::Key_F12:
            return Key::F12;
        case Qt::Key_Shift:
            return Key::LShift;
        case Qt::Key_Control:
            return Key::LControl;
        case Qt::Key_Alt:
            return Key::LAlt;
        case Qt::Key_Meta:
        case Qt::Key_Super_L:
        case Qt::Key_Super_R:
        case Qt::Key_Menu:
            return Key::LSystem;
        default:
            return Key::Unknown;
    }
}

void SFMLWidget::dragEnterEvent(QDragEnterEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (!mimeData) {
        event->ignore();
        return;
    }

    if (mimeData->hasFormat(ui::mime::GECK_OBJECT)) {
        // Extract object data and start drag preview
        QByteArray objectData = mimeData->data(ui::mime::GECK_OBJECT);
        QStringList parts = QString::fromUtf8(objectData).split(',');

        if (parts.size() == 2 && _editorWidget) {
            int objectIndex = parts[0].toInt();
            int categoryInt = parts[1].toInt();

            // Convert Qt coordinates to SFML world coordinates
            sf::Vector2f worldPos = mapToWorld(event->position());

            // Start drag preview in editor
            _editorWidget->startDragPreview(objectIndex, categoryInt, worldPos);
        }

        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void SFMLWidget::dragMoveEvent(QDragMoveEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (!mimeData) {
        event->ignore();
        return;
    }

    if (mimeData->hasFormat(ui::mime::GECK_OBJECT)) {
        // Update drag preview position
        if (_editorWidget) {
            sf::Vector2f worldPos = mapToWorld(event->position());
            _editorWidget->updateDragPreview(worldPos);
        }

        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void SFMLWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    // Clean up drag preview when drag leaves the widget
    if (_editorWidget) {
        _editorWidget->cancelDragPreview();
    }
    event->accept();
}

void SFMLWidget::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (!mimeData) {
        event->ignore();
        return;
    }

    if (mimeData->hasFormat(ui::mime::GECK_OBJECT)) {
        // Finish the drag preview and place the object
        if (_editorWidget) {
            sf::Vector2f worldPos = mapToWorld(event->position());
            _editorWidget->finishDragPreview(worldPos);
        }

        event->acceptProposedAction();
    } else {
        // Propagate to parent if not handled
        event->ignore();
    }
}

} // namespace geck
