#include "ObjectPalettePanel.h"
#include "format/map/Map.h"
#include "format/lst/Lst.h"
#include "format/pro/Pro.h"
#include "resource/GameResources.h"
#include "util/Constants.h"
#include "util/ColorUtils.h"
#include "util/ProHelper.h"
#include "ui/FrmThumbnailGenerator.h"
#include "ui/common/BaseWidget.h"
#include "ui/dragdrop/MimeTypes.h"
#include "ui/theme/ThemeManager.h"
#include "ui/UIConstants.h"

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

    if (objectInfo) {
        setToolTip(QString("Object %1: %2\nFile: %3")
                .arg(objectIndex)
                .arg(objectInfo->displayName)
                .arg(objectInfo->proFileName));
    } else {
        setToolTip(QString("Object %1").arg(objectIndex));
    }

    connect(this, &BasePaletteWidget::clicked, this, [this](int index) {
        Q_EMIT objectClicked(index);
    });
}

void ObjectWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        BasePaletteWidget::mouseMoveEvent(event);
        return;
    }

    QPoint startPos = mapFromGlobal(QCursor::pos()) - event->pos();
    if ((event->pos() + startPos).manhattanLength() < QApplication::startDragDistance()) {
        BasePaletteWidget::mouseMoveEvent(event);
        return;
    }

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;

    // Payload: "<objectIndex>,<category>".
    mimeData->setText(QString("geck/object"));
    mimeData->setData(ui::mime::GECK_OBJECT,
        QByteArray::number(getIndex()) + "," + QByteArray::number(static_cast<int>(_category)));

    drag->setPixmap(pixmap().scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    drag->setMimeData(mimeData);

    Qt::DropAction dropAction = drag->exec(Qt::CopyAction);
    Q_UNUSED(dropAction);
}

ObjectPalettePanel::ObjectPalettePanel(resource::GameResources& resources, QWidget* parent)
    : GridPalettePanel("Objects", parent)
    , _resources(resources) {
    setupUI();
    spdlog::debug("ObjectPalettePanel: Created object palette panel");
}

ObjectPalettePanel::~ObjectPalettePanel() {
    // The resource repository manages Pro object lifetime, not us
    spdlog::debug("ObjectPalettePanel: Clearing object lists before destruction");

    _objectsByCategory.clear();
    _objectWidgets.clear();

    spdlog::debug("ObjectPalettePanel: Destructor completed");
}

std::vector<std::unique_ptr<ObjectInfo>>& ObjectPalettePanel::getObjectList(ObjectCategory category) {
    return _objectsByCategory[category];
}

const std::vector<std::unique_ptr<ObjectInfo>>& ObjectPalettePanel::getObjectList(ObjectCategory category) const {
    static const std::vector<std::unique_ptr<ObjectInfo>> empty;
    auto it = _objectsByCategory.find(category);
    if (it != _objectsByCategory.end()) {
        return it->second;
    }
    return empty;
}

void ObjectPalettePanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setSpacing(ui::constants::SPACING_TIGHT);
    _mainLayout->setContentsMargins(ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN, ui::constants::COMPACT_MARGIN);

    setupCategoryTabs();
    setupSearchControls();
    setupPaginationControls();
    setupObjectGrid();

    _statusLabel = new QLabel("No objects loaded", this);
    _statusLabel->setStyleSheet(ui::theme::styles::italicSecondaryText());
    _mainLayout->addWidget(_statusLabel);

    _mainLayout->addStretch();
}

void ObjectPalettePanel::setupCategoryTabs() {
    _categoryTabs = new QTabWidget(this);

    _categoryTabs->addTab(new QWidget(), getCategoryDisplayName(ObjectCategory::ITEMS));
    _categoryTabs->addTab(new QWidget(), getCategoryDisplayName(ObjectCategory::SCENERY));
    _categoryTabs->addTab(new QWidget(), getCategoryDisplayName(ObjectCategory::CRITTERS));
    _categoryTabs->addTab(new QWidget(), getCategoryDisplayName(ObjectCategory::WALLS));
    _categoryTabs->addTab(new QWidget(), getCategoryDisplayName(ObjectCategory::MISC));

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

    connect(_searchLineEdit, &QLineEdit::textChanged, this, &ObjectPalettePanel::onSearchTextChanged);

    _mainLayout->addWidget(_searchGroup);
}

void ObjectPalettePanel::setupObjectGrid() {
    setupGridArea();
    _mainLayout->addWidget(scrollArea(), 1);
}

void ObjectPalettePanel::setupPaginationControls() {
    _paginationGroup = new QGroupBox("Page Navigation", this);
    auto* paginationLayout = new QHBoxLayout(_paginationGroup);

    _paginationWidget = new PaginationWidget(this);
    _paginationWidget->setShowFirstLastButtons(true);
    connect(_paginationWidget, &PaginationWidget::pageChanged,
        this, &ObjectPalettePanel::onGridPaginationPageChanged);
    paginationLayout->addWidget(_paginationWidget);

    _mainLayout->addWidget(_paginationGroup);
    _paginationGroup->hide();
}

void ObjectPalettePanel::loadObjects() {
    spdlog::debug("ObjectPalettePanel: Loading objects from LST files");

    loadCategoryObjects(_currentCategory);
    updateObjectGrid();
}

void ObjectPalettePanel::loadCategoryObjects(ObjectCategory category) {
    QString categoryPath = getCategoryPath(category);
    QString lstPath = QString("%1/%2.lst").arg(categoryPath).arg(getCategoryDisplayName(category).toLower());

    spdlog::debug("ObjectPalettePanel: Loading category {} from {}",
        static_cast<int>(category), lstPath.toStdString());

    auto& targetList = getObjectList(category);
    targetList.clear();

    try {
        const auto* lst = _resources.repository().find<Lst>(lstPath.toStdString());
        if (!lst) {
            lst = _resources.repository().load<Lst>(lstPath.toStdString());
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

                auto objectInfo = std::make_unique<ObjectInfo>(qProFileName, i);

                std::string proFilePath = categoryPath.toStdString() + "/" + proFileName;

                objectInfo->pro = _resources.repository().load<Pro>(proFilePath);

                if (objectInfo->pro) {
                    objectInfo->displayName = QString("%1 (%2)")
                                                  .arg(QString::fromStdString(objectInfo->pro->typeToString()))
                                                  .arg(qProFileName);

                    // Resolve the FRM path from the PRO's FID.
                    try {
                        std::string frmPath = _resources.frmResolver().resolve(objectInfo->pro->header.FID);
                        objectInfo->frmPath = QString::fromStdString(frmPath);
                    } catch (const std::exception& e) {
                        spdlog::debug("ObjectPalettePanel: Could not resolve FID for {}: {}",
                            proFileName, e.what());
                        objectInfo->frmPath = "";
                    }
                } else {
                    objectInfo->displayName = qProFileName;
                    objectInfo->frmPath = "";
                }

                targetList.push_back(std::move(objectInfo));
                loadedCount++;

            } catch (const std::exception& e) {
                spdlog::debug("ObjectPalettePanel: Failed to load PRO {}: {}",
                    proFiles[i], e.what());
            }
        }

        spdlog::debug("ObjectPalettePanel: Loaded {} objects for category {} from {}",
            loadedCount, getCategoryDisplayName(category).toStdString(), lstPath.toStdString());

    } catch (const std::exception& e) {
        spdlog::error("ObjectPalettePanel: Failed to load category {}: {}",
            getCategoryDisplayName(category).toStdString(), e.what());
    }
}

void ObjectPalettePanel::updateObjectGrid() {
    int newColumnsPerRow = calculateOptimalColumnsPerRow(ObjectWidget::OBJECT_SIZE);
    if (newColumnsPerRow != _objectsPerRow) {
        _objectsPerRow = newColumnsPerRow;
        _previousColumnsPerRow = newColumnsPerRow;
    }

    _objectWidgets.clear();
    clearGridWidgets();

    const auto& objectList = getObjectList(_currentCategory);

    if (objectList.empty()) {
        _statusLabel->setText("No objects available for this category");
        return;
    }

    calculatePagination();

    int row = 0;
    int col = 0;
    int objectsLoaded = 0;
    int filteredIndex = 0;
    int targetStartIndex = getPageStartIndex();
    int targetEndIndex = getPageEndIndex();

    for (int i = 0; i < static_cast<int>(objectList.size()); ++i) {
        const auto& objectInfo = objectList[i];

        if (!_searchText.isEmpty()) {
            if (!objectInfo->displayName.contains(_searchText, Qt::CaseInsensitive) && !objectInfo->proFileName.contains(_searchText, Qt::CaseInsensitive)) {
                continue;
            }
        }

        // Only build widgets for objects within the current page range.
        if (filteredIndex < targetStartIndex) {
            filteredIndex++;
            continue;
        }
        if (filteredIndex > targetEndIndex) {
            break;
        }

        try {
            QPixmap objectThumbnail = createObjectThumbnail(objectInfo.get(), _currentCategory);

            auto objectWidget = std::make_unique<ObjectWidget>(i, objectInfo.get(), objectThumbnail, _currentCategory, this);
            connect(objectWidget.get(), &ObjectWidget::objectClicked, this, &ObjectPalettePanel::onObjectClicked);

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

    spdlog::debug("ObjectPalettePanel: Loaded {} object widgets for category {}",
        objectsLoaded, getCategoryDisplayName(_currentCategory).toStdString());
}

QPixmap ObjectPalettePanel::createObjectThumbnail(const ObjectInfo* objectInfo, ObjectCategory category) {
    QPixmap thumbnail(ObjectWidget::OBJECT_SIZE, ObjectWidget::OBJECT_SIZE);

    if (objectInfo && !objectInfo->frmPath.isEmpty()) {
        try {
            thumbnail = FrmThumbnailGenerator::fromFrmPath(_resources,
                objectInfo->frmPath.toStdString(),
                QSize(ObjectWidget::OBJECT_SIZE, ObjectWidget::OBJECT_SIZE));
            if (!thumbnail.isNull()) {
                return thumbnail;
            }
        } catch (const std::exception& e) {
            spdlog::debug("ObjectPalettePanel: Failed to load FRM for {}: {}",
                objectInfo->frmPath.toStdString(), e.what());
            // Fall through to placeholder generation
        }
    }

    // No FRM available: render a category-colored placeholder.
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

    QPainter painter(&thumbnail);
    painter.setPen(ui::theme::colors::textDark());
    painter.setFont(ui::theme::fonts::compactBold());

    painter.drawText(QRect(0, 2, ObjectWidget::OBJECT_SIZE, 12),
        Qt::AlignCenter, categoryText);

    painter.setFont(ui::theme::fonts::tiny());
    QString displayText;
    if (objectInfo) {
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
    clearObjectSelection();

    _selectedObjectIndex = objectIndex;

    if (objectIndex >= 0 && objectIndex < static_cast<int>(_objectWidgets.size())) {
        _objectWidgets[objectIndex]->setSelected(true);
    }

    Q_EMIT objectSelected(objectIndex, _currentCategory);

    spdlog::debug("ObjectPalettePanel: Selected object {} in category {}",
        objectIndex, static_cast<int>(_currentCategory));
}

void ObjectPalettePanel::onCategoryChanged(int tabIndex) {
    _currentCategory = static_cast<ObjectCategory>(tabIndex);

    spdlog::debug("ObjectPalettePanel: Changed to category {}", static_cast<int>(_currentCategory));

    _currentPage = 0; // Reset to first page on category change

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

std::optional<std::pair<int, ObjectCategory>> ObjectPalettePanel::revealProto(uint32_t pid) {
    ObjectCategory category;
    switch (Pro::typeOfPid(pid)) {
        case Pro::OBJECT_TYPE::ITEM:
            category = ObjectCategory::ITEMS;
            break;
        case Pro::OBJECT_TYPE::CRITTER:
            category = ObjectCategory::CRITTERS;
            break;
        case Pro::OBJECT_TYPE::SCENERY:
            category = ObjectCategory::SCENERY;
            break;
        case Pro::OBJECT_TYPE::WALL:
            category = ObjectCategory::WALLS;
            break;
        case Pro::OBJECT_TYPE::MISC:
            category = ObjectCategory::MISC;
            break;
        default:
            return std::nullopt; // TILE / unknown types are not shown in this palette
    }

    // Resolve the proto's .pro filename so we match the exact palette entry without re-deriving the
    // PID<->filename off-by-one; the LST-sourced ObjectInfo::proFileName is the same basename.
    const QString fileName = QFileInfo(QString::fromStdString(ProHelper::basePath(_resources, pid))).fileName();
    if (fileName.isEmpty()) {
        return std::nullopt;
    }

    // Categories are loaded lazily when their tab is first opened, so the eyedropper may target one
    // the user has never viewed (e.g. a critter while the Items tab is showing). Populate it first.
    if (getObjectList(category).empty()) {
        loadCategoryObjects(category);
    }

    // Locate the entry in the category's full list (robust: independent of which page is materialised).
    // This index is what getObjectInfo()/the ghost builder expect.
    const auto& objectList = getObjectList(category);
    int index = -1;
    for (int i = 0; i < static_cast<int>(objectList.size()); ++i) {
        if (objectList[i] && objectList[i]->proFileName.compare(fileName, Qt::CaseInsensitive) == 0) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        return std::nullopt;
    }

    // Switch to the proto's category and filter to it so it is revealed and selected in the palette.
    _categoryTabs->setCurrentIndex(static_cast<int>(category));
    if (_searchLineEdit) {
        _searchLineEdit->setText(fileName);
    }
    clearObjectSelection();
    for (const auto& widget : _objectWidgets) {
        const ObjectInfo* info = widget->getObjectInfo();
        if (info && info->proFileName.compare(fileName, Qt::CaseInsensitive) == 0) {
            widget->setSelected(true);
            _selectedObjectIndex = widget->getObjectIndex();
            Q_EMIT objectSelected(_selectedObjectIndex, _currentCategory);
            break;
        }
    }
    return std::make_pair(index, category);
}

void ObjectPalettePanel::calculatePagination() {
    const auto& objectList = getObjectList(_currentCategory);

    if (objectList.empty()) {
        updatePaginationState(0);
        GridPalettePanel::updatePaginationControls();
        return;
    }

    int filteredCount = 0;
    for (const auto& objectInfo : objectList) {
        if (!_searchText.isEmpty()) {
            if (!objectInfo->displayName.contains(_searchText, Qt::CaseInsensitive) && !objectInfo->proFileName.contains(_searchText, Qt::CaseInsensitive)) {
                continue;
            }
        }
        filteredCount++;
    }

    updatePaginationState(filteredCount);
}

const ObjectInfo* ObjectPalettePanel::getObjectInfo(int objectIndex, ObjectCategory category) const {
    const auto& categoryList = getObjectList(category);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(categoryList.size())) {
        return nullptr;
    }

    return categoryList[objectIndex].get();
}

void ObjectPalettePanel::resizeEvent(QResizeEvent* event) {
    GridPalettePanel::resizeEvent(event);

    int newColumnsPerRow = calculateOptimalColumnsPerRow(ObjectWidget::OBJECT_SIZE);

    // Only rebuild when the column count actually changed.
    if (newColumnsPerRow != _previousColumnsPerRow) {
        _objectsPerRow = newColumnsPerRow;
        _previousColumnsPerRow = newColumnsPerRow;

        if (!_objectWidgets.empty()) {
            updateObjectGrid();
        }
    }
}

} // namespace geck
