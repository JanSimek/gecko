#include "ObjectPalettePanel.h"
#include "../format/map/Map.h"
#include "../format/lst/Lst.h"
#include "../format/pro/Pro.h"
#include "../reader/pro/ProReader.h"
#include "../reader/lst/LstReader.h"
#include "../util/ResourceManager.h"
#include "../util/Constants.h"
#include "../util/ColorUtils.h"

#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QStyle>
#include <QFileInfo>
#include <QDir>
#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

namespace geck {

ObjectWidget::ObjectWidget(int objectIndex, const ObjectInfo* objectInfo, const QPixmap& pixmap, QWidget* parent)
    : QLabel(parent)
    , _objectIndex(objectIndex)
    , _objectInfo(objectInfo) {

    QPixmap scaledPixmap = pixmap.scaled(OBJECT_SIZE, OBJECT_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Create a square canvas with the object centered
    QPixmap canvas(OBJECT_SIZE, OBJECT_SIZE);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    int x = (OBJECT_SIZE - scaledPixmap.width()) / 2;
    int y = (OBJECT_SIZE - scaledPixmap.height()) / 2;
    painter.drawPixmap(x, y, scaledPixmap);

    setPixmap(canvas);
    setFixedSize(OBJECT_SIZE + 4, OBJECT_SIZE + 4); // Small margin
    setAlignment(Qt::AlignCenter);
    setStyleSheet("border: 1px solid gray; background-color: white;");

    // Add tooltip with object information
    if (objectInfo) {
        setToolTip(QString("Object %1: %2\nFile: %3")
                .arg(objectIndex)
                .arg(objectInfo->displayName)
                .arg(objectInfo->proFileName));
    } else {
        setToolTip(QString("Object %1").arg(objectIndex));
    }
}

void ObjectWidget::setSelected(bool selected) {
    if (_selected != selected) {
        _selected = selected;
        update(); // Trigger repaint
    }
}

void ObjectWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit objectClicked(_objectIndex);
    }
    QLabel::mousePressEvent(event);
}

void ObjectWidget::paintEvent(QPaintEvent* event) {
    QLabel::paintEvent(event);

    if (_selected) {
        QPainter painter(this);
        painter.setPen(QPen(QColor(255, 165, 0), 3)); // Orange selection border
        painter.drawRect(rect().adjusted(1, 1, -2, -2));
    }
}

ObjectPalettePanel::ObjectPalettePanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    spdlog::info("ObjectPalettePanel: Created object palette panel");
}

ObjectPalettePanel::~ObjectPalettePanel() {
    // Clear all object lists - now safe since we use raw pointers to Pro objects
    // The ResourceManager manages Pro object lifetime, not us
    spdlog::debug("ObjectPalettePanel: Clearing object lists before destruction");

    _itemsList.clear();
    _sceneryList.clear();
    _crittersList.clear();
    _wallsList.clear();
    _miscList.clear();

    // Clear widget references
    _objectWidgets.clear();

    spdlog::debug("ObjectPalettePanel: Destructor completed");
}

void ObjectPalettePanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setSpacing(4);
    _mainLayout->setContentsMargins(4, 4, 4, 4);

    setupCategoryTabs();
    setupSearchControls();
    setupObjectGrid();

    // Status label
    _statusLabel = new QLabel("No objects loaded", this);
    _statusLabel->setStyleSheet("color: gray; font-style: italic;");
    _mainLayout->addWidget(_statusLabel);

    _mainLayout->addStretch(); // Push everything to top
}

void ObjectPalettePanel::setupCategoryTabs() {
    _categoryTabs = new QTabWidget(this);

    // Add tabs for each object category
    _categoryTabs->addTab(new QWidget(), getCategoryDisplayName(ObjectCategory::ITEMS));
    _categoryTabs->addTab(new QWidget(), getCategoryDisplayName(ObjectCategory::SCENERY));
    _categoryTabs->addTab(new QWidget(), getCategoryDisplayName(ObjectCategory::CRITTERS));
    _categoryTabs->addTab(new QWidget(), getCategoryDisplayName(ObjectCategory::WALLS));
    _categoryTabs->addTab(new QWidget(), getCategoryDisplayName(ObjectCategory::MISC));

    // Connect tab change signal
    connect(_categoryTabs, &QTabWidget::currentChanged, this, &ObjectPalettePanel::onCategoryChanged);

    _mainLayout->addWidget(_categoryTabs);
}

void ObjectPalettePanel::setupSearchControls() {
    _searchGroup = new QGroupBox("Search", this);
    auto* searchLayout = new QHBoxLayout(_searchGroup);

    searchLayout->addWidget(new QLabel("Find:", this));
    _searchLineEdit = new QLineEdit(this);
    _searchLineEdit->setPlaceholderText("Enter object name...");
    _searchLineEdit->setClearButtonEnabled(true);
    searchLayout->addWidget(_searchLineEdit, 1);

    // Connect search signal
    connect(_searchLineEdit, &QLineEdit::textChanged, this, &ObjectPalettePanel::onSearchTextChanged);

    _mainLayout->addWidget(_searchGroup);
}

void ObjectPalettePanel::setupObjectGrid() {
    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    _objectGridWidget = new QWidget();
    _objectGridLayout = new QGridLayout(_objectGridWidget);
    _objectGridLayout->setSpacing(2);
    _objectGridLayout->setContentsMargins(4, 4, 4, 4);

    _scrollArea->setWidget(_objectGridWidget);
    _mainLayout->addWidget(_scrollArea, 1); // Take remaining space
}

void ObjectPalettePanel::loadObjects() {
    spdlog::info("ObjectPalettePanel: Loading objects from LST files");

    // Load objects for the current category
    loadCategoryObjects(_currentCategory);
    updateObjectGrid();
}

void ObjectPalettePanel::loadCategoryObjects(ObjectCategory category) {
    QString categoryPath = getCategoryPath(category);
    QString lstPath = QString("%1/%2.lst").arg(categoryPath).arg(getCategoryDisplayName(category).toLower());

    spdlog::debug("ObjectPalettePanel: Loading category {} from {}",
        static_cast<int>(category), lstPath.toStdString());

    // Get target list for this category
    std::vector<std::unique_ptr<ObjectInfo>>* targetList = nullptr;

    switch (category) {
        case ObjectCategory::ITEMS:
            targetList = &_itemsList;
            break;
        case ObjectCategory::SCENERY:
            targetList = &_sceneryList;
            break;
        case ObjectCategory::CRITTERS:
            targetList = &_crittersList;
            break;
        case ObjectCategory::WALLS:
            targetList = &_wallsList;
            break;
        case ObjectCategory::MISC:
            targetList = &_miscList;
            break;
    }

    if (!targetList) {
        return;
    }

    targetList->clear();

    try {
        // Load LST file for this category
        auto& resourceManager = ResourceManager::getInstance();

        // Try to get the LST file from ResourceManager first
        const auto* lst = resourceManager.getResource<Lst, std::string>(lstPath.toStdString());

        // If not found, try to load it manually
        if (!lst) {
            LstReader lstReader;
            lst = resourceManager.loadResource(lstPath.toStdString(), lstReader);
        }

        if (!lst || lst->list().empty()) {
            spdlog::warn("ObjectPalettePanel: No objects found in {}", lstPath.toStdString());
            return;
        }

        const auto& proFiles = lst->list();
        int loadedCount = 0;
        int maxToLoad = std::min(static_cast<int>(proFiles.size()), MAX_OBJECTS_TO_LOAD);

        for (int i = 0; i < maxToLoad; ++i) {
            try {
                const std::string& proFileName = proFiles[i];
                QString qProFileName = QString::fromStdString(proFileName);

                // Create ObjectInfo
                auto objectInfo = std::make_unique<ObjectInfo>(qProFileName, i);

                // Try to load the PRO file
                std::string proFilePath = categoryPath.toStdString() + "/" + proFileName;

                ProReader proReader;
                objectInfo->pro = resourceManager.loadResource(proFilePath, proReader);

                if (objectInfo->pro) {
                    // Generate display name from PRO file information
                    objectInfo->displayName = QString("%1 (%2)")
                                                  .arg(QString::fromStdString(objectInfo->pro->typeToString()))
                                                  .arg(qProFileName);

                    // Get FRM path from FID
                    try {
                        std::string frmPath = resourceManager.FIDtoFrmName(objectInfo->pro->header.FID);
                        objectInfo->frmPath = QString::fromStdString(frmPath);
                    } catch (const std::exception& e) {
                        spdlog::debug("ObjectPalettePanel: Could not resolve FID for {}: {}",
                            proFileName, e.what());
                        objectInfo->frmPath = ""; // Use placeholder
                    }
                } else {
                    // Fallback if PRO loading fails
                    objectInfo->displayName = qProFileName;
                    objectInfo->frmPath = "";
                }

                targetList->push_back(std::move(objectInfo));
                loadedCount++;

            } catch (const std::exception& e) {
                spdlog::debug("ObjectPalettePanel: Failed to load PRO {}: {}",
                    proFiles[i], e.what());
                // Continue with next object
            }
        }

        spdlog::info("ObjectPalettePanel: Loaded {} objects for category {} from {}",
            loadedCount, getCategoryDisplayName(category).toStdString(), lstPath.toStdString());

    } catch (const std::exception& e) {
        spdlog::error("ObjectPalettePanel: Failed to load category {}: {}",
            getCategoryDisplayName(category).toStdString(), e.what());

        // Create fallback placeholder objects
        QString categoryName = getCategoryDisplayName(category);
        for (int i = 0; i < 10; ++i) {
            auto objectInfo = std::make_unique<ObjectInfo>(
                QString("%1_fallback_%2.pro").arg(categoryName.toLower()).arg(i), i);
            objectInfo->displayName = QString("Fallback %1 Object %2").arg(categoryName).arg(i);
            objectInfo->frmPath = "";
            targetList->push_back(std::move(objectInfo));
        }

        spdlog::info("ObjectPalettePanel: Created {} fallback objects for category {}",
            targetList->size(), categoryName.toStdString());
    }
}

void ObjectPalettePanel::updateObjectGrid() {
    // Clear existing widgets
    _objectWidgets.clear();
    QLayoutItem* item;
    while ((item = _objectGridLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    // Get objects for current category
    const std::vector<std::unique_ptr<ObjectInfo>>* objectList = nullptr;

    switch (_currentCategory) {
        case ObjectCategory::ITEMS:
            objectList = &_itemsList;
            break;
        case ObjectCategory::SCENERY:
            objectList = &_sceneryList;
            break;
        case ObjectCategory::CRITTERS:
            objectList = &_crittersList;
            break;
        case ObjectCategory::WALLS:
            objectList = &_wallsList;
            break;
        case ObjectCategory::MISC:
            objectList = &_miscList;
            break;
    }

    if (!objectList || objectList->empty()) {
        _statusLabel->setText("No objects available for this category");
        return;
    }

    int row = 0;
    int col = 0;
    int objectsLoaded = 0;
    int maxObjects = std::min(static_cast<int>(objectList->size()), MAX_OBJECTS_TO_LOAD);

    for (int i = 0; i < maxObjects; ++i) {
        const auto& objectInfo = (*objectList)[i];

        // Apply search filter if set
        if (!_searchText.isEmpty()) {
            if (!objectInfo->displayName.contains(_searchText, Qt::CaseInsensitive) && !objectInfo->proFileName.contains(_searchText, Qt::CaseInsensitive)) {
                continue; // Skip objects that don't match search
            }
        }

        try {
            // Create thumbnail for this object
            QPixmap objectThumbnail = createObjectThumbnail(objectInfo.get(), _currentCategory);

            // Create object widget
            auto objectWidget = std::make_unique<ObjectWidget>(i, objectInfo.get(), objectThumbnail, this);
            connect(objectWidget.get(), &ObjectWidget::objectClicked, this, &ObjectPalettePanel::onObjectClicked);

            // Add to grid
            _objectGridLayout->addWidget(objectWidget.get(), row, col);
            _objectWidgets.push_back(std::move(objectWidget));

            col++;
            if (col >= _objectsPerRow) {
                col = 0;
                row++;
            }

            objectsLoaded++;
        } catch (const std::exception& e) {
            spdlog::warn("ObjectPalettePanel: Failed to load object {}: {}",
                objectInfo->proFileName.toStdString(), e.what());
        }
    }

    // Update status
    QString statusText;
    if (!_searchText.isEmpty()) {
        statusText = QString("Found %1 objects matching '%2' in %3")
                         .arg(objectsLoaded)
                         .arg(_searchText)
                         .arg(getCategoryDisplayName(_currentCategory));
    } else {
        statusText = QString("Loaded %1 %2 objects")
                         .arg(objectsLoaded)
                         .arg(getCategoryDisplayName(_currentCategory));
    }
    _statusLabel->setText(statusText);

    spdlog::info("ObjectPalettePanel: Loaded {} object widgets for category {}",
        objectsLoaded, getCategoryDisplayName(_currentCategory).toStdString());
}

QPixmap ObjectPalettePanel::createObjectThumbnail(const ObjectInfo* objectInfo, ObjectCategory category) {
    QPixmap thumbnail(ObjectWidget::OBJECT_SIZE, ObjectWidget::OBJECT_SIZE);

    // Try to load actual FRM sprite first
    if (objectInfo && !objectInfo->frmPath.isEmpty()) {
        try {
            auto& resourceManager = ResourceManager::getInstance();
            const auto& texture = resourceManager.texture(objectInfo->frmPath.toStdString());

            // Convert SFML texture to QPixmap
            sf::Vector2u textureSize = texture.getSize();
            sf::Image image = texture.copyToImage();

            // Create QImage from SFML image data
            QImage qImage(image.getPixelsPtr(), textureSize.x, textureSize.y, QImage::Format_RGBA8888);
            thumbnail = QPixmap::fromImage(qImage);

            spdlog::debug("ObjectPalettePanel: Loaded FRM thumbnail for {} ({}x{})",
                objectInfo->frmPath.toStdString(), textureSize.x, textureSize.y);

            return thumbnail;

        } catch (const std::exception& e) {
            spdlog::debug("ObjectPalettePanel: Failed to load FRM for {}: {}",
                objectInfo->frmPath.toStdString(), e.what());
            // Fall through to placeholder generation
        }
    }

    // Create placeholder thumbnail with category-specific styling
    QColor categoryColor;
    QString categoryText;

    switch (category) {
        case ObjectCategory::ITEMS:
            categoryColor = QColor(100, 150, 255); // Blue
            categoryText = "ITEM";
            break;
        case ObjectCategory::SCENERY:
            categoryColor = QColor(100, 255, 100); // Green
            categoryText = "SCEN";
            break;
        case ObjectCategory::CRITTERS:
            categoryColor = QColor(255, 150, 100); // Orange
            categoryText = "CRIT";
            break;
        case ObjectCategory::WALLS:
            categoryColor = QColor(150, 150, 150); // Gray
            categoryText = "WALL";
            break;
        case ObjectCategory::MISC:
            categoryColor = QColor(255, 100, 255); // Magenta
            categoryText = "MISC";
            break;
    }

    thumbnail.fill(categoryColor);

    // Draw object information
    QPainter painter(&thumbnail);
    painter.setPen(Qt::black);
    painter.setFont(QFont("Arial", 8, QFont::Bold));

    // Draw category type at top
    painter.drawText(QRect(0, 2, ObjectWidget::OBJECT_SIZE, 12),
        Qt::AlignCenter, categoryText);

    // Draw object filename or display name
    painter.setFont(QFont("Arial", 6));
    QString displayText;
    if (objectInfo) {
        // Use the more readable display name if available
        displayText = objectInfo->displayName.isEmpty() ? objectInfo->proFileName.left(10) : objectInfo->displayName.left(10);
    } else {
        displayText = "Unknown";
    }

    painter.drawText(QRect(2, ObjectWidget::OBJECT_SIZE / 2, ObjectWidget::OBJECT_SIZE - 4, ObjectWidget::OBJECT_SIZE / 2 - 2),
        Qt::AlignCenter | Qt::TextWordWrap, displayText);

    return thumbnail;
}

QString ObjectPalettePanel::getCategoryPath(ObjectCategory category) const {
    switch (category) {
        case ObjectCategory::ITEMS:
            return "proto/items";
        case ObjectCategory::SCENERY:
            return "proto/scenery";
        case ObjectCategory::CRITTERS:
            return "proto/critters";
        case ObjectCategory::WALLS:
            return "proto/walls";
        case ObjectCategory::MISC:
            return "proto/misc";
    }
    return "";
}

QString ObjectPalettePanel::getCategoryDisplayName(ObjectCategory category) const {
    switch (category) {
        case ObjectCategory::ITEMS:
            return "Items";
        case ObjectCategory::SCENERY:
            return "Scenery";
        case ObjectCategory::CRITTERS:
            return "Critters";
        case ObjectCategory::WALLS:
            return "Walls";
        case ObjectCategory::MISC:
            return "Misc";
    }
    return "Unknown";
}

void ObjectPalettePanel::onObjectClicked(int objectIndex) {
    // Clear previous selection
    clearObjectSelection();

    // Set new selection
    _selectedObjectIndex = objectIndex;

    // Update visual selection
    if (objectIndex >= 0 && objectIndex < static_cast<int>(_objectWidgets.size())) {
        _objectWidgets[objectIndex]->setSelected(true);
    }

    emit objectSelected(objectIndex, _currentCategory);

    spdlog::debug("ObjectPalettePanel: Selected object {} in category {}",
        objectIndex, static_cast<int>(_currentCategory));
}

void ObjectPalettePanel::onCategoryChanged(int tabIndex) {
    _currentCategory = static_cast<ObjectCategory>(tabIndex);

    spdlog::debug("ObjectPalettePanel: Changed to category {}", static_cast<int>(_currentCategory));

    // Load objects for new category and update grid
    loadCategoryObjects(_currentCategory);
    updateObjectGrid();
}

void ObjectPalettePanel::onSearchTextChanged(const QString& text) {
    _searchText = text.trimmed();
    updateObjectGrid();
}

void ObjectPalettePanel::clearObjectSelection() {
    for (auto& objectWidget : _objectWidgets) {
        objectWidget->setSelected(false);
    }
    _selectedObjectIndex = -1;
}

} // namespace geck