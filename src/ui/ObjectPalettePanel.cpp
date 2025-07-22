#include "ObjectPalettePanel.h"
#include "../format/map/Map.h"
#include "../format/lst/Lst.h"
#include "../format/pro/Pro.h"
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
    setupPaginationControls();

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

void ObjectPalettePanel::setupPaginationControls() {
    _paginationGroup = new QGroupBox("Page Navigation", this);
    auto* paginationLayout = new QHBoxLayout(_paginationGroup);

    // First page button
    _firstPageButton = new QPushButton("|<", this);
    _firstPageButton->setToolTip("Go to first page");
    _firstPageButton->setMaximumWidth(40);
    connect(_firstPageButton, &QPushButton::clicked, this, &ObjectPalettePanel::goToFirstPage);
    paginationLayout->addWidget(_firstPageButton);

    // Previous page button
    _prevPageButton = new QPushButton("<", this);
    _prevPageButton->setToolTip("Go to previous page");
    _prevPageButton->setMaximumWidth(30);
    connect(_prevPageButton, &QPushButton::clicked, this, &ObjectPalettePanel::goToPrevPage);
    paginationLayout->addWidget(_prevPageButton);

    // Page selector
    paginationLayout->addWidget(new QLabel("Page:", this));
    _pageSpinBox = new QSpinBox(this);
    _pageSpinBox->setMinimum(1);
    _pageSpinBox->setMaximumWidth(60);
    connect(_pageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ObjectPalettePanel::onPageSpinBoxChanged);
    paginationLayout->addWidget(_pageSpinBox);

    _pageInfoLabel = new QLabel("of 1", this);
    paginationLayout->addWidget(_pageInfoLabel);

    // Next page button
    _nextPageButton = new QPushButton(">", this);
    _nextPageButton->setToolTip("Go to next page");
    _nextPageButton->setMaximumWidth(30);
    connect(_nextPageButton, &QPushButton::clicked, this, &ObjectPalettePanel::goToNextPage);
    paginationLayout->addWidget(_nextPageButton);

    // Last page button
    _lastPageButton = new QPushButton(">|", this);
    _lastPageButton->setToolTip("Go to last page");
    _lastPageButton->setMaximumWidth(40);
    connect(_lastPageButton, &QPushButton::clicked, this, &ObjectPalettePanel::goToLastPage);
    paginationLayout->addWidget(_lastPageButton);

    paginationLayout->addStretch(); // Push buttons to left

    _mainLayout->addWidget(_paginationGroup);
    _paginationGroup->hide(); // Initially hidden
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
            lst = resourceManager.loadResource<Lst>(lstPath.toStdString());
        }

        if (!lst || lst->list().empty()) {
            spdlog::warn("ObjectPalettePanel: No objects found in {}", lstPath.toStdString());
            return;
        }

        const auto& proFiles = lst->list();
        int loadedCount = 0;
        // Load ALL objects - no artificial limit
        int totalObjects = static_cast<int>(proFiles.size());

        for (int i = 0; i < totalObjects; ++i) {
            try {
                const std::string& proFileName = proFiles[i];
                QString qProFileName = QString::fromStdString(proFileName);

                // Create ObjectInfo
                auto objectInfo = std::make_unique<ObjectInfo>(qProFileName, i);

                // Try to load the PRO file
                std::string proFilePath = categoryPath.toStdString() + "/" + proFileName;

                objectInfo->pro = resourceManager.loadResource<Pro>(proFilePath);

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

    // Calculate pagination for filtered objects
    calculatePagination();

    int row = 0;
    int col = 0;
    int objectsLoaded = 0;
    int filteredIndex = 0;
    int targetStartIndex = _currentPage * OBJECTS_PER_PAGE;
    int targetEndIndex = targetStartIndex + OBJECTS_PER_PAGE - 1;

    for (int i = 0; i < static_cast<int>(objectList->size()); ++i) {
        const auto& objectInfo = (*objectList)[i];

        // Apply search filter if set
        if (!_searchText.isEmpty()) {
            if (!objectInfo->displayName.contains(_searchText, Qt::CaseInsensitive) && !objectInfo->proFileName.contains(_searchText, Qt::CaseInsensitive)) {
                continue; // Skip objects that don't match search
            }
        }

        // Check if this filtered object is in the current page range
        if (filteredIndex < targetStartIndex) {
            filteredIndex++;
            continue; // Skip objects before current page
        }
        if (filteredIndex > targetEndIndex) {
            break; // Stop loading objects beyond current page
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
            filteredIndex++;
        } catch (const std::exception& e) {
            spdlog::warn("ObjectPalettePanel: Failed to load object {}: {}",
                objectInfo->proFileName.toStdString(), e.what());
        }
    }

    // Update status with pagination info
    QString statusText;
    if (!_searchText.isEmpty()) {
        statusText = QString("Page %1/%2: Found %3 objects matching '%4' in %5 (showing %6)")
                         .arg(_currentPage + 1)
                         .arg(_totalPages)
                         .arg(_totalFilteredObjects)
                         .arg(_searchText)
                         .arg(getCategoryDisplayName(_currentCategory))
                         .arg(objectsLoaded);
    } else {
        statusText = QString("Page %1/%2: %3 total %4 objects (showing %5)")
                         .arg(_currentPage + 1)
                         .arg(_totalPages)
                         .arg(_totalFilteredObjects)
                         .arg(getCategoryDisplayName(_currentCategory))
                         .arg(objectsLoaded);
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

            resourceManager.loadResource<Frm>(objectInfo->frmPath.toStdString());
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

    // Reset pagination when changing categories
    _currentPage = 0;

    // Load objects for new category and update grid
    loadCategoryObjects(_currentCategory);
    updateObjectGrid();
}

void ObjectPalettePanel::onSearchTextChanged(const QString& text) {
    _searchText = text.trimmed();
    _currentPage = 0; // Reset to first page when search changes
    updateObjectGrid();
}

void ObjectPalettePanel::clearObjectSelection() {
    for (auto& objectWidget : _objectWidgets) {
        objectWidget->setSelected(false);
    }
    _selectedObjectIndex = -1;
}

void ObjectPalettePanel::calculatePagination() {
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
        _totalFilteredObjects = 0;
        _totalPages = 0;
        _currentPage = 0;
        updatePaginationControls();
        return;
    }

    // Count objects that match current filters
    int filteredCount = 0;
    for (const auto& objectInfo : *objectList) {
        // Apply search filter if set
        if (!_searchText.isEmpty()) {
            if (!objectInfo->displayName.contains(_searchText, Qt::CaseInsensitive) && 
                !objectInfo->proFileName.contains(_searchText, Qt::CaseInsensitive)) {
                continue; // Skip objects that don't match search
            }
        }
        filteredCount++;
    }

    _totalFilteredObjects = filteredCount;
    _totalPages = (filteredCount + OBJECTS_PER_PAGE - 1) / OBJECTS_PER_PAGE; // Ceiling division

    // Ensure current page is valid
    if (_currentPage >= _totalPages) {
        _currentPage = std::max(0, _totalPages - 1);
    }

    updatePaginationControls();
}

void ObjectPalettePanel::updatePaginationControls() {
    if (_totalPages <= 1) {
        _paginationGroup->hide(); // Hide pagination if not needed
        return;
    }

    _paginationGroup->show();

    // Update button states
    _firstPageButton->setEnabled(_currentPage > 0);
    _prevPageButton->setEnabled(_currentPage > 0);
    _nextPageButton->setEnabled(_currentPage < _totalPages - 1);
    _lastPageButton->setEnabled(_currentPage < _totalPages - 1);

    // Update page spinbox
    _pageSpinBox->setEnabled(_totalPages > 1);
    _pageSpinBox->setMaximum(_totalPages);
    _pageSpinBox->setValue(_currentPage + 1); // Convert to 1-based

    // Update page info label
    _pageInfoLabel->setText(QString("of %1").arg(_totalPages));
}

void ObjectPalettePanel::goToFirstPage() {
    if (_currentPage != 0) {
        _currentPage = 0;
        updateObjectGrid();
    }
}

void ObjectPalettePanel::goToLastPage() {
    int lastPage = std::max(0, _totalPages - 1);
    if (_currentPage != lastPage) {
        _currentPage = lastPage;
        updateObjectGrid();
    }
}

void ObjectPalettePanel::goToPrevPage() {
    if (_currentPage > 0) {
        _currentPage--;
        updateObjectGrid();
    }
}

void ObjectPalettePanel::goToNextPage() {
    if (_currentPage < _totalPages - 1) {
        _currentPage++;
        updateObjectGrid();
    }
}

void ObjectPalettePanel::onPageSpinBoxChanged(int page) {
    int newPage = page - 1; // Convert from 1-based to 0-based
    if (newPage != _currentPage && newPage >= 0 && newPage < _totalPages) {
        _currentPage = newPage;
        updateObjectGrid();
    }
}

} // namespace geck