#include "ObjectPalettePanel.h"
#include "../../format/map/Map.h"
#include "../../format/lst/Lst.h"
#include "../../format/pro/Pro.h"
#include "../../format/frm/Frm.h"
#include "../../format/pal/Pal.h"
#include "../../util/ResourceManager.h"
#include "../../util/Constants.h"
#include "../../util/ColorUtils.h"
#include "../../util/FrmThumbnailGenerator.h"
#include "../common/BaseWidget.h"
#include "../dragdrop/MimeTypes.h"
#include "../theme/ThemeManager.h"
#include "../UIConstants.h"

#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QStyle>
#include <QFileInfo>
#include <QDir>
#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

namespace geck {

ObjectWidget::ObjectWidget(int objectIndex, const ObjectInfo* objectInfo, const QPixmap& pixmap, ObjectCategory category, QWidget* parent)
    : BasePaletteWidget(objectIndex, parent)
    , _objectInfo(objectInfo)
    , _category(category) {

    QPixmap scaledPixmap = BaseWidget::scalePixmapToSize(pixmap, OBJECT_SIZE);
    QPixmap centeredPixmap = BaseWidget::createCenteredPixmap(scaledPixmap, OBJECT_SIZE);
    setPixmap(centeredPixmap);

    setupCommonProperties(OBJECT_SIZE);
    setStyleSheet(ui::theme::styles::normalWidget());

    // Add tooltip with object information
    if (objectInfo) {
        setToolTip(QString("Object %1: %2\nFile: %3")
                .arg(objectIndex)
                .arg(objectInfo->displayName)
                .arg(objectInfo->proFileName));
    } else {
        setToolTip(QString("Object %1").arg(objectIndex));
    }

    // Connect base class signal to our specific signal
    connect(this, &BasePaletteWidget::clicked, this, [this](int index) {
        emit objectClicked(index);
    });
}

void ObjectWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        BasePaletteWidget::mouseMoveEvent(event);
        return;
    }

    // Get drag start position from base class event handling
    QPoint startPos = mapFromGlobal(QCursor::pos()) - event->pos();
    if ((event->pos() + startPos).manhattanLength() < QApplication::startDragDistance()) {
        BasePaletteWidget::mouseMoveEvent(event);
        return;
    }

    // Start drag operation
    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;

    // Set MIME data with object information
    mimeData->setText(QString("geck/object"));
    mimeData->setData(ui::mime::GECK_OBJECT,
        QByteArray::number(getIndex()) + "," + QByteArray::number(static_cast<int>(_category)));

    // Use the object's pixmap as drag pixmap
    drag->setPixmap(pixmap().scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    drag->setMimeData(mimeData);

    // Execute the drag
    Qt::DropAction dropAction = drag->exec(Qt::CopyAction);
    Q_UNUSED(dropAction);
}

ObjectPalettePanel::ObjectPalettePanel(QWidget* parent)
    : GridPalettePanel("Objects", parent) {
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
    _mainLayout->setSpacing(ui::constants::SPACING_TIGHT);
    _mainLayout->setContentsMargins(ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN);

    setupCategoryTabs();
    setupSearchControls();
    setupPaginationControls();
    setupObjectGrid();

    // Status label
    _statusLabel = new QLabel("No objects loaded", this);
    _statusLabel->setStyleSheet(ui::theme::styles::italicSecondaryText());
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
    _searchGroup = new QGroupBox("Filter", this);
    auto* searchLayout = new QHBoxLayout(_searchGroup);

    searchLayout->addWidget(new QLabel("Search:", this));
    _searchLineEdit = new QLineEdit(this);
    _searchLineEdit->setPlaceholderText("Enter object name...");
    _searchLineEdit->setClearButtonEnabled(true);
    searchLayout->addWidget(_searchLineEdit, 1);

    // Connect search signal
    connect(_searchLineEdit, &QLineEdit::textChanged, this, &ObjectPalettePanel::onSearchTextChanged);

    _mainLayout->addWidget(_searchGroup);
}

void ObjectPalettePanel::setupObjectGrid() {
    // Use base class grid setup
    setupGridArea();
    _mainLayout->addWidget(scrollArea(), 1); // Take remaining space
}

void ObjectPalettePanel::setupPaginationControls() {
    _paginationGroup = new QGroupBox("Page Navigation", this);
    auto* paginationLayout = new QHBoxLayout(_paginationGroup);

    // Shared pagination widget with all controls
    _paginationWidget = new PaginationWidget(this);
    _paginationWidget->setShowFirstLastButtons(true);
    connect(_paginationWidget, &PaginationWidget::pageChanged,
            this, &ObjectPalettePanel::onGridPaginationPageChanged);
    paginationLayout->addWidget(_paginationWidget);

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
    // Recalculate optimal columns based on current panel width using base class method
    int newColumnsPerRow = calculateOptimalColumnsPerRow(ObjectWidget::OBJECT_SIZE);
    if (newColumnsPerRow != _objectsPerRow) {
        _objectsPerRow = newColumnsPerRow;
        _previousColumnsPerRow = newColumnsPerRow;
    }

    // Clear existing widgets using base class method
    _objectWidgets.clear();
    clearGridWidgets();

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
    int targetStartIndex = getPageStartIndex();
    int targetEndIndex = getPageEndIndex();

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
            auto objectWidget = std::make_unique<ObjectWidget>(i, objectInfo.get(), objectThumbnail, _currentCategory, this);
            connect(objectWidget.get(), &ObjectWidget::objectClicked, this, &ObjectPalettePanel::onObjectClicked);

            // Add to grid
            gridLayout()->addWidget(objectWidget.get(), row, col);
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
                         .arg(currentPage() + 1)
                         .arg(totalPages())
                         .arg(totalFilteredItems())
                         .arg(_searchText)
                         .arg(getCategoryDisplayName(_currentCategory))
                         .arg(objectsLoaded);
    } else {
        statusText = QString("Page %1/%2: %3 total %4 objects (showing %5)")
                         .arg(currentPage() + 1)
                         .arg(totalPages())
                         .arg(totalFilteredItems())
                         .arg(getCategoryDisplayName(_currentCategory))
                         .arg(objectsLoaded);
    }
    _statusLabel->setText(statusText);

    GridPalettePanel::updatePaginationControls();

    spdlog::info("ObjectPalettePanel: Loaded {} object widgets for category {}",
        objectsLoaded, getCategoryDisplayName(_currentCategory).toStdString());
}

QPixmap ObjectPalettePanel::createObjectThumbnail(const ObjectInfo* objectInfo, ObjectCategory category) {
    QPixmap thumbnail(ObjectWidget::OBJECT_SIZE, ObjectWidget::OBJECT_SIZE);

    // Try to load actual FRM sprite first
    if (objectInfo && !objectInfo->frmPath.isEmpty()) {
        try {
            auto& resourceManager = ResourceManager::getInstance();

            // Load the FRM object directly (not the texture)
            const auto* frm = resourceManager.loadResource<Frm>(objectInfo->frmPath.toStdString());
            if (!frm) {
                spdlog::debug("ObjectPalettePanel: Failed to load FRM object for {}", objectInfo->frmPath.toStdString());
                // Fall through to placeholder generation
            } else {
                // Extract first frame of first direction (like F2 Dims)
                const auto& directions = frm->directions();
                if (!directions.empty()) {
                    const auto& firstDirection = directions[0];
                    const auto& frames = firstDirection.frames();
                    if (!frames.empty()) {
                        const auto& firstFrame = frames[0];

                        // Load palette for color conversion (required)
                        const Pal* palette = nullptr;
                        try {
                            palette = resourceManager.loadResource<Pal>("color.pal");
                        } catch (...) {
                            // Palette loading failed - this is required for proper color display
                            spdlog::warn("ObjectPalettePanel: Could not load color.pal for {}, falling back to placeholder", objectInfo->frmPath.toStdString());
                            // Fall through to placeholder generation since we need the palette
                        }

                        if (!palette) {
                            // Without palette, we cannot generate proper colors, use placeholder
                            spdlog::debug("ObjectPalettePanel: No palette available for {}", objectInfo->frmPath.toStdString());
                            // Fall through to placeholder generation
                        } else {
                            // Convert single frame to thumbnail
                            thumbnail = FrmThumbnailGenerator::fromFrame(
                                firstFrame, palette,
                                QSize(ObjectWidget::OBJECT_SIZE, ObjectWidget::OBJECT_SIZE));

                            spdlog::debug("ObjectPalettePanel: Created single-frame thumbnail for {} ({}x{})",
                                objectInfo->frmPath.toStdString(), firstFrame.width(), firstFrame.height());

                            return thumbnail;
                        }
                    } else {
                        spdlog::debug("ObjectPalettePanel: No frames found in FRM {}", objectInfo->frmPath.toStdString());
                    }
                } else {
                    spdlog::debug("ObjectPalettePanel: No directions found in FRM {}", objectInfo->frmPath.toStdString());
                }
            }

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
            categoryColor = ui::theme::colors::categoryItems();
            categoryText = "ITEM";
            break;
        case ObjectCategory::SCENERY:
            categoryColor = ui::theme::colors::categoryScenery();
            categoryText = "SCEN";
            break;
        case ObjectCategory::CRITTERS:
            categoryColor = ui::theme::colors::categoryCritters();
            categoryText = "CRIT";
            break;
        case ObjectCategory::WALLS:
            categoryColor = ui::theme::colors::categoryWalls();
            categoryText = "WALL";
            break;
        case ObjectCategory::MISC:
            categoryColor = ui::theme::colors::categoryMisc();
            categoryText = "MISC";
            break;
    }

    thumbnail.fill(categoryColor);

    // Draw object information
    QPainter painter(&thumbnail);
    painter.setPen(ui::theme::colors::textDark());
    painter.setFont(ui::theme::fonts::compactBold());

    // Draw category type at top
    painter.drawText(QRect(0, 2, ObjectWidget::OBJECT_SIZE, 12),
        Qt::AlignCenter, categoryText);

    // Draw object filename or display name
    painter.setFont(ui::theme::fonts::tiny());
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
    std::ranges::for_each(_objectWidgets, [](auto& widget) {
        widget->setSelected(false);
    });
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
        updatePaginationState(0);
        GridPalettePanel::updatePaginationControls();
        return;
    }

    // Count objects that match current filters
    int filteredCount = 0;
    for (const auto& objectInfo : *objectList) {
        // Apply search filter if set
        if (!_searchText.isEmpty()) {
            if (!objectInfo->displayName.contains(_searchText, Qt::CaseInsensitive) && !objectInfo->proFileName.contains(_searchText, Qt::CaseInsensitive)) {
                continue; // Skip objects that don't match search
            }
        }
        filteredCount++;
    }

    // Use base class pagination update
    updatePaginationState(filteredCount);
}

const ObjectInfo* ObjectPalettePanel::getObjectInfo(int objectIndex, ObjectCategory category) const {
    const std::vector<std::unique_ptr<ObjectInfo>>* categoryList = nullptr;

    switch (category) {
        case ObjectCategory::ITEMS:
            categoryList = &_itemsList;
            break;
        case ObjectCategory::SCENERY:
            categoryList = &_sceneryList;
            break;
        case ObjectCategory::CRITTERS:
            categoryList = &_crittersList;
            break;
        case ObjectCategory::WALLS:
            categoryList = &_wallsList;
            break;
        case ObjectCategory::MISC:
            categoryList = &_miscList;
            break;
    }

    if (!categoryList || objectIndex < 0 || objectIndex >= static_cast<int>(categoryList->size())) {
        return nullptr;
    }

    return (*categoryList)[objectIndex].get();
}

void ObjectPalettePanel::resizeEvent(QResizeEvent* event) {
    GridPalettePanel::resizeEvent(event);

    // Calculate optimal columns for the new size using base class method
    int newColumnsPerRow = calculateOptimalColumnsPerRow(ObjectWidget::OBJECT_SIZE);

    // Only update if the column count actually changed to avoid unnecessary rebuilds
    if (newColumnsPerRow != _previousColumnsPerRow) {
        _objectsPerRow = newColumnsPerRow;
        _previousColumnsPerRow = newColumnsPerRow;

        // Trigger grid update only if we have objects loaded
        if (!_objectWidgets.empty()) {
            updateObjectGrid();
        }
    }
}

} // namespace geck