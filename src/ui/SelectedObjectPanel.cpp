#include "SelectedObjectPanel.h"

#include <QFormLayout>
#include <QPixmap>
#include <QApplication>
#include <spdlog/spdlog.h>

#include "../format/map/MapObject.h"
#include "../reader/pro/ProReader.h"
#include "../util/ResourceManager.h"
#include "../util/ProHelper.h"

namespace geck {

SelectedObjectPanel::SelectedObjectPanel(QWidget* parent)
    : QWidget(parent)
    , _mainLayout(nullptr)
    , _scrollArea(nullptr)
    , _contentWidget(nullptr)
    , _contentLayout(nullptr)
    , _objectInfoGroup(nullptr)
    , _spriteLabel(nullptr)
    , _nameEdit(nullptr)
    , _typeEdit(nullptr)
    , _messageIdSpin(nullptr)
    , _positionSpin(nullptr)
    , _protoPidSpin(nullptr) {
    
    setupUI();
}

void SelectedObjectPanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(5, 5, 5, 5);
    
    // Create scroll area for content
    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    _contentWidget = new QWidget();
    _contentLayout = new QVBoxLayout(_contentWidget);
    _contentLayout->setContentsMargins(5, 5, 5, 5);
    
    // Object information group
    _objectInfoGroup = new QGroupBox("Object Information");
    QFormLayout* formLayout = new QFormLayout(_objectInfoGroup);
    
    // Sprite display (placeholder for now)
    _spriteLabel = new QLabel("No object selected");
    _spriteLabel->setAlignment(Qt::AlignCenter);
    _spriteLabel->setMinimumHeight(128);
    _spriteLabel->setMinimumWidth(128);
    _spriteLabel->setMaximumHeight(128);
    _spriteLabel->setMaximumWidth(128);
    _spriteLabel->setScaledContents(false);  // Don't stretch - we'll handle scaling manually
    _spriteLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _spriteLabel->setStyleSheet("border: 1px solid gray; background-color: #f0f0f0;");
    formLayout->addRow("Sprite:", _spriteLabel);
    
    // Object properties
    _nameEdit = new QLineEdit();
    _nameEdit->setReadOnly(true);
    _nameEdit->setPlaceholderText("No object selected");
    formLayout->addRow("Name:", _nameEdit);
    
    _typeEdit = new QLineEdit();
    _typeEdit->setReadOnly(true);
    _typeEdit->setPlaceholderText("No object selected");
    formLayout->addRow("Type:", _typeEdit);
    
    _messageIdSpin = new QSpinBox();
    _messageIdSpin->setRange(0, INT_MAX);
    _messageIdSpin->setReadOnly(true);
    _messageIdSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    formLayout->addRow("Message ID:", _messageIdSpin);
    
    _positionSpin = new QSpinBox();
    _positionSpin->setRange(0, INT_MAX);
    _positionSpin->setReadOnly(true);
    _positionSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    formLayout->addRow("Position:", _positionSpin);
    
    _protoPidSpin = new QSpinBox();
    _protoPidSpin->setRange(0, INT_MAX);
    _protoPidSpin->setReadOnly(true);
    _protoPidSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    formLayout->addRow("Proto PID:", _protoPidSpin);
    
    _contentLayout->addWidget(_objectInfoGroup);
    _contentLayout->addStretch(); // Add stretch to push content to top
    
    _scrollArea->setWidget(_contentWidget);
    _mainLayout->addWidget(_scrollArea);
    
    // Initially clear the display
    clearObjectInfo();
}

void SelectedObjectPanel::selectObject(std::shared_ptr<Object> selectedObject) {
    if (selectedObject == nullptr) {
        _selectedObject.reset();
        clearObjectInfo();
        spdlog::debug("SelectedObjectPanel: Object deselected");
    } else {
        _selectedObject = selectedObject;
        updateObjectInfo();
        spdlog::debug("SelectedObjectPanel: Object selected with PID {}", 
                     selectedObject->getMapObject().pro_pid);
    }
}

void SelectedObjectPanel::updateObjectInfo() {
    if (!_selectedObject || !_selectedObject.value()) {
        clearObjectInfo();
        return;
    }
    
    try {
        auto& selectedMapObject = _selectedObject.value()->getMapObject();
        int32_t PID = selectedMapObject.pro_pid;
        
        // Load Proto file to get object information
        ProReader proReader{};
        auto pro = ResourceManager::getInstance().loadResource(ProHelper::basePath(PID), proReader);
        
        if (pro) {
            // Get object name from message file
            auto msg = ProHelper::msgFile(pro->type());
            std::string objectName = "Unknown";
            if (msg) {
                try {
                    objectName = msg->message(pro->header.message_id).text;
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to get message for ID {}: {}", pro->header.message_id, e.what());
                }
            }
            
            // Update UI with object information
            _nameEdit->setText(QString::fromStdString(objectName));
            _typeEdit->setText(QString::fromStdString(pro->typeToString()));
            _messageIdSpin->setValue(static_cast<int>(pro->header.message_id));
            _positionSpin->setValue(selectedMapObject.position);
            _protoPidSpin->setValue(static_cast<int>(selectedMapObject.pro_pid));
            
            // Convert SFML sprite to QPixmap for display
            const auto& sprite = _selectedObject.value()->getSprite();
            const auto* texture = sprite.getTexture();
            if (texture) {
                // Get the image from SFML texture
                auto image = texture->copyToImage();
                auto textureRect = sprite.getTextureRect();
                
                // Create QImage from SFML image data
                const sf::Uint8* pixels = image.getPixelsPtr();
                QImage qImage(pixels, image.getSize().x, image.getSize().y, QImage::Format_RGBA8888);
                
                // Extract the sprite's texture rectangle if needed
                if (textureRect.width > 0 && textureRect.height > 0) {
                    qImage = qImage.copy(textureRect.left, textureRect.top, textureRect.width, textureRect.height);
                }
                
                // Create pixmap and scale to fit label while maintaining aspect ratio
                QPixmap pixmap = QPixmap::fromImage(qImage);
                if (!pixmap.isNull()) {
                    // Scale the pixmap to fit within 128x128 while keeping aspect ratio
                    QSize maxSize(128, 128);
                    
                    if (pixmap.width() > maxSize.width() || pixmap.height() > maxSize.height()) {
                        pixmap = pixmap.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    }
                    
                    _spriteLabel->setPixmap(pixmap);
                    
                    // Clear the text when we have a pixmap
                    _spriteLabel->setText("");
                } else {
                    _spriteLabel->setText("Failed to convert sprite");
                }
            } else {
                _spriteLabel->setText("No texture available");
            }
            
            _objectInfoGroup->setTitle("Object Information");
        } else {
            spdlog::warn("Failed to load proto file for PID: {}", PID);
            clearObjectInfo();
        }
    } catch (const std::exception& e) {
        spdlog::error("Error updating object info: {}", e.what());
        clearObjectInfo();
    }
}

void SelectedObjectPanel::clearObjectInfo() {
    _nameEdit->clear();
    _nameEdit->setPlaceholderText("No object selected");
    
    _typeEdit->clear();
    _typeEdit->setPlaceholderText("No object selected");
    
    _messageIdSpin->setValue(0);
    _positionSpin->setValue(0);
    _protoPidSpin->setValue(0);
    
    _spriteLabel->clear();
    _spriteLabel->setText("No object selected");
    _objectInfoGroup->setTitle("Object Information");
}

} // namespace geck