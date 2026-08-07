#include "ObjectPalettePanel.h"
#include "format/msg/Msg.h"
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

#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QStyle>
#include <QFileInfo>
#include <QDir>
#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>

namespace geck {

ObjectPalettePanel::ObjectPalettePanel(resource::GameResources& resources, QWidget* parent)
    : BasePanel("Objects", parent)
    , _resources(resources) {
    setupUI();
    spdlog::debug("ObjectPalettePanel: Created object palette panel");
}

ObjectPalettePanel::~ObjectPalettePanel() {
    // The resource repository manages Pro object lifetime, not us
    spdlog::debug("ObjectPalettePanel: Clearing object lists before destruction");

    _objectsByCategory.clear();

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
    setupObjectView();

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

void ObjectPalettePanel::setupObjectView() {
    _model = new ui::PaletteModel(this);
    _model->setIconProvider([this](const ui::PaletteItem& item) {
        const ObjectInfo* info = getObjectInfo(item.engineIndex, _currentCategory);
        return info ? createObjectThumbnail(info, _currentCategory) : QPixmap();
    });
    // The view starts the drag; the model only supplies the payload the map drop handler expects.
    _model->setMimeProvider(ui::mime::GECK_OBJECT, [this](const ui::PaletteItem& item) {
        // No setText(): with no drag pixmap the platform falls back to showing the plain text, which
        // put "geck/object" next to the cursor. The custom type is what the map's drop handler reads.
        auto* payload = new QMimeData; // not `data`: QWidget has a member of that name (MSVC C4458)
        payload->setData(ui::mime::GECK_OBJECT,
            QByteArray::number(item.engineIndex) + "," + QByteArray::number(static_cast<int>(_currentCategory)));
        return payload;
    });

    _filter = new QSortFilterProxyModel(this);
    _filter->setSourceModel(_model);
    _filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    _filter->setFilterRole(ui::PaletteModel::LabelRole);

    // Caption each icon with the proto's engine name (protoDisplayName), falling back to its .pro
    // filename. Both halves are needed: the model supplies the text, the view the room for it.
    _model->setShowLabels(true);

    _objectView = new ui::PaletteView(OBJECT_SIZE, this);
    _objectView->setShowLabels(true);
    _objectView->setModel(_filter);
    _objectView->setDragEnabled(true);
    _objectView->setDragDropMode(QAbstractItemView::DragOnly);

    connect(_objectView->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& current, const QModelIndex&) {
            if (current.isValid()) {
                onObjectClicked(current.data(ui::PaletteModel::EngineIndexRole).toInt());
            }
        });

    _mainLayout->addWidget(_objectView, 1);
}

void ObjectPalettePanel::loadObjects() {
    spdlog::debug("ObjectPalettePanel: Loading objects from LST files");

    loadCategoryObjects(_currentCategory);
    rebuildItems();
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
                    objectInfo->displayName = protoDisplayName(*objectInfo->pro, qProFileName);

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

QString ObjectPalettePanel::protoDisplayName(const Pro& pro, const QString& proFileName) const {
    // getMessages().find(), not Msg::message(): that one is a map operator[] and inserts a blank
    // entry for a missing id, mutating the shared cached Msg.
    try {
        if (const Msg* msg = ProHelper::msgFile(_resources, pro.type()); msg != nullptr) {
            const auto& messages = msg->getMessages();
            const auto found = messages.find(static_cast<int>(pro.header.message_id));
            if (found != messages.end() && !found->second.text.empty()) {
                return QString::fromStdString(found->second.text);
            }
        }
    } catch (const std::exception& e) {
        spdlog::debug("ObjectPalettePanel: no engine name for {}: {}", proFileName.toStdString(), e.what());
    }
    return proFileName;
}

void ObjectPalettePanel::rebuildItems() {
    if (_model == nullptr) {
        return;
    }

    const auto& objects = getObjectList(_currentCategory);
    std::vector<ui::PaletteItem> items;
    items.reserve(objects.size());
    // The index is the position in this list, which is what getObjectInfo() and revealProto() use.
    // ObjectInfo::listIndex is the .lst line instead, and the two diverge as soon as a proto fails
    // to load and its entry is skipped - placing then looked up the wrong object, or none.
    for (int position = 0; position < static_cast<int>(objects.size()); ++position) {
        const auto& info = objects[static_cast<size_t>(position)];
        if (!info) {
            continue;
        }
        const QString label = info->displayName.isEmpty() ? info->proFileName : info->displayName;
        const QString tooltip = label == info->proFileName ? label : label + "\n" + info->proFileName;
        items.push_back({ position, label, tooltip });
    }

    _model->setItems(std::move(items));
    updateStatusLabel();
}

void ObjectPalettePanel::updateStatusLabel() {
    if (_statusLabel == nullptr || _filter == nullptr) {
        return;
    }
    const int shown = _filter->rowCount();
    const int total = _model ? _model->rowCount() : 0;
    _statusLabel->setText(shown == total ? QString("%1 objects").arg(total)
                                         : QString("%1 of %2 objects").arg(shown).arg(total));
}

void ObjectPalettePanel::selectRowForObject(int objectIndex) {
    if (_objectView == nullptr || _model == nullptr || objectIndex < 0) {
        return;
    }
    const int row = _model->rowForEngineIndex(objectIndex);
    if (row < 0) {
        return;
    }
    const QModelIndex mapped = _filter->mapFromSource(_model->index(row, 0));
    if (!mapped.isValid()) {
        return;
    }
    QSignalBlocker blocker(_objectView->selectionModel());
    _objectView->setCurrentIndex(mapped);
    _objectView->scrollTo(mapped, QAbstractItemView::PositionAtCenter);
}

QPixmap ObjectPalettePanel::createObjectThumbnail(const ObjectInfo* objectInfo, ObjectCategory category) {
    QPixmap thumbnail(OBJECT_SIZE, OBJECT_SIZE);

    if (objectInfo && !objectInfo->frmPath.isEmpty()) {
        try {
            thumbnail = FrmThumbnailGenerator::fromFrmPath(_resources,
                objectInfo->frmPath.toStdString(),
                QSize(OBJECT_SIZE, OBJECT_SIZE));
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

    painter.drawText(QRect(0, 2, OBJECT_SIZE, 12),
        Qt::AlignCenter, categoryText);

    painter.setFont(ui::theme::fonts::tiny());
    QString displayText;
    if (objectInfo) {
        displayText = objectInfo->displayName.isEmpty() ? objectInfo->proFileName.left(10) : objectInfo->displayName.left(10);
    } else {
        displayText = "Unknown";
    }

    painter.drawText(QRect(2, OBJECT_SIZE / 2, OBJECT_SIZE - 4, OBJECT_SIZE / 2 - 2),
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
    // No selectRowForObject() here: this runs from the view's own currentChanged, so the row is
    // already current, and re-applying it would scrollTo(PositionAtCenter) and recentre the grid
    // under the still-pressed cursor - the click then landed on whatever item the scroll brought
    // under the mouse. Programmatic callers (revealProto, the search filter) sync the view
    // themselves.
    _selectedObjectIndex = objectIndex;

    Q_EMIT objectSelected(objectIndex, _currentCategory);

    spdlog::debug("ObjectPalettePanel: Selected object {} in category {}",
        objectIndex, static_cast<int>(_currentCategory));
}

void ObjectPalettePanel::onCategoryChanged(int tabIndex) {
    _currentCategory = static_cast<ObjectCategory>(tabIndex);

    spdlog::debug("ObjectPalettePanel: Changed to category {}", static_cast<int>(_currentCategory));

    loadCategoryObjects(_currentCategory);
    rebuildItems();
}

void ObjectPalettePanel::onSearchTextChanged(const QString& text) {
    _filter->setFilterFixedString(text.trimmed());
    selectRowForObject(_selectedObjectIndex);
    updateStatusLabel();
}

void ObjectPalettePanel::clearObjectSelection() {
    if (_objectView != nullptr) {
        QSignalBlocker blocker(_objectView->selectionModel());
        _objectView->clearSelection();
        _objectView->setCurrentIndex({});
    }
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
    // basePath() throws on an out-of-range PID (corrupt map / mismatched resources) — treat that as
    // "not in this palette" rather than letting it terminate the app.
    QString fileName;
    try {
        fileName = QFileInfo(QString::fromStdString(ProHelper::basePath(_resources, pid))).fileName();
    } catch (const std::exception& e) {
        spdlog::warn("ObjectPalettePanel::revealProto: cannot resolve PID {}: {}", pid, e.what());
        return std::nullopt;
    }
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
    _selectedObjectIndex = index;
    selectRowForObject(index);
    Q_EMIT objectSelected(index, _currentCategory);
    return std::make_pair(index, category);
}

const ObjectInfo* ObjectPalettePanel::getObjectInfo(int objectIndex, ObjectCategory category) const {
    const auto& categoryList = getObjectList(category);

    if (objectIndex < 0 || objectIndex >= static_cast<int>(categoryList.size())) {
        return nullptr;
    }

    return categoryList[objectIndex].get();
}

} // namespace geck
