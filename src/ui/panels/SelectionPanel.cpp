#include "SelectionPanel.h"
#include "ui/common/InventoryItemUiHelper.h"
#include "ui/dialogs/FrmSelectorDialog.h"
#include "ui/dialogs/ProEditorDialog.h"
#include "ui/dialogs/ObjectFlagsDialog.h"
#include "ui/dialogs/LightPropertiesDialog.h"
#include "ui/dialogs/SceneryDestinationDialog.h"
#include "ui/dialogs/InstancePropertiesDialog.h"
#include "ui/dialogs/CritterPropertiesDialog.h"
#include "ui/dialogs/ItemSelectorDialog.h"
#include "ui/theme/ThemeManager.h"
#include "ui/Settings.h"
#include "ui/panels/ObjectScriptSection.h"
#include "ui/widgets/CollapsibleSection.h"
#include "ui/widgets/ElidedLabel.h"
#include "resource/AiTxtLoader.h"
#include "resource/ResourcePaths.h"
#include "format/map/MapScript.h"

#include <algorithm>
#include <array>

#include <QFormLayout>
#include <QPixmap>
#include <QApplication>
#include <QTimer>
#include <QPainter>
#include <QHeaderView>
#include <QMessageBox>
#include <QSpinBox>
#include <QEnterEvent>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolButton>
#include <spdlog/spdlog.h>
#include <cmath>

#include "editor/HexagonGrid.h"
#include "format/map/Map.h"
#include "format/lst/Lst.h"
#include "resource/GameResources.h"
#include "util/ProHelper.h"
#include "reader/ReaderFactory.h"
#include "ui/IconHelper.h"
#include "format/map/MapObject.h"
#include "format/pro/Pro.h"
#include "format/msg/Msg.h"
#include "format/frm/Frm.h"

namespace geck {

using ui::inventory::COLUMN_AMOUNT;
using ui::inventory::COLUMN_ICON;
using ui::inventory::COLUMN_NAME;
using ui::inventory::COLUMN_TYPE;

/// Spinbox-based delegate for editing the inventory amount column.
class SelectionPanel::AmountDelegate : public QStyledItemDelegate {
public:
    AmountDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) { }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(option)
        Q_UNUSED(index)

        QSpinBox* editor = new QSpinBox(parent);
        editor->setRange(1, 999999);
        editor->setSingleStep(1);
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        int value = index.model()->data(index, Qt::EditRole).toInt();
        QSpinBox* spinBox = static_cast<QSpinBox*>(editor);
        spinBox->setValue(value);
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
        QSpinBox* spinBox = static_cast<QSpinBox*>(editor);
        spinBox->interpretText();
        int value = spinBox->value();
        model->setData(index, value, Qt::EditRole);
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index)
        editor->setGeometry(option.rect);
    }
};

HoverSpriteLabel::HoverSpriteLabel(QWidget* parent)
    : QLabel(parent) {
    setMouseTracking(true);
    setupEditButton();
}

void HoverSpriteLabel::setupEditButton() {
    _editButton = new QPushButton(this);
    _editButton->setIcon(createIcon(":/icons/actions/edit.svg"));
    _editButton->setToolTip("Change FRM file");
    _editButton->setFixedSize(ui::constants::sizes::ICON_BUTTON, ui::constants::sizes::ICON_BUTTON);
    _editButton->setStyleSheet(ui::theme::styles::overlayButton());
    _editButton->setIconSize(QSize(ui::constants::sizes::ICON_SIZE_SMALL, ui::constants::sizes::ICON_SIZE_SMALL));
    _editButton->setVisible(false); // Hidden by default
    _editButton->raise();
}

void HoverSpriteLabel::enterEvent(QEnterEvent* event) {
    Q_UNUSED(event)
    if (_editButton) {
        _editButton->setVisible(true);
        positionEditButton();
    }
}

void HoverSpriteLabel::leaveEvent(QEvent* event) {
    Q_UNUSED(event)
    if (_editButton) {
        _editButton->setVisible(false);
    }
}

void HoverSpriteLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    if (_editButton && _editButton->isVisible()) {
        positionEditButton();
    }
}

void HoverSpriteLabel::positionEditButton() {
    if (!_editButton)
        return;

    // Position in top-left corner with small margin
    QPoint topLeft = rect().topLeft();
    topLeft.setX(topLeft.x() + 4);
    topLeft.setY(topLeft.y() + 4);
    _editButton->move(topLeft);
    _editButton->raise();
}

QSize SelectionPanel::sizeHint() const {
    return QSize(ui::constants::sizes::PANEL_PREFERRED_WIDTH, ui::constants::sizes::PANEL_PREFERRED_HEIGHT);
}

QSize SelectionPanel::minimumSizeHint() const {
    return QSize(ui::constants::sizes::PANEL_MIN_SIZE_WIDTH, ui::constants::sizes::PANEL_MIN_SIZE_HEIGHT);
}

SelectionPanel::SelectionPanel(resource::GameResources& resources, std::shared_ptr<Settings> settings, QWidget* parent)
    : QWidget(parent)
    , _mainLayout(nullptr)
    , _scrollArea(nullptr)
    , _contentWidget(nullptr)
    , _contentLayout(nullptr)
    , _stackedWidget(nullptr)
    , _objectPanelWidget(nullptr)
    , _hoverSpriteLabel(nullptr)
    , _objectNameLabel(nullptr)
    , _objectSummaryLabel(nullptr)
    , _objectIdsLabel(nullptr)
    , _editProButton(nullptr)
    , _editMenuButton(nullptr)
    , _editExitGridAction(nullptr)
    , _editFlagsAction(nullptr)
    , _editLightAction(nullptr)
    , _editDestinationAction(nullptr)
    , _editInteractionAction(nullptr)
    , _editCritterAction(nullptr)
    , _propertiesSection(nullptr)
    , _messageIdLabel(nullptr)
    , _facingLabel(nullptr)
    , _lightLabel(nullptr)
    , _artPathLabel(nullptr)
    , _scriptSection(nullptr)
    , _scriptView(nullptr)
    , _inventorySection(nullptr)
    , _inventoryViewStack(nullptr)
    , _inventoryTree(nullptr)
    , _emptyInventoryLabel(nullptr)
    , _addInventoryButton(nullptr)
    , _removeInventoryButton(nullptr)
    , _tilePanelWidget(nullptr)
    , _tileInfoGroup(nullptr)
    , _tilePreviewLabel(nullptr)
    , _tileIndexSpin(nullptr)
    , _elevationSpin(nullptr)
    , _hexXSpin(nullptr)
    , _hexYSpin(nullptr)
    , _worldXSpin(nullptr)
    , _worldYSpin(nullptr)
    , _tileTypeEdit(nullptr)
    , _tileIdSpin(nullptr)
    , _tileNameEdit(nullptr)
    , _resources(resources)
    , _settings(std::move(settings))
    , _selectedTileIndex(-1)
    , _selectedElevation(-1)
    , _isRoofSelected(false)
    , _hasTileSelection(false)
    , _map(nullptr) {

    _amountDelegate = new AmountDelegate(this);

    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    setupUI();
}

CollapsibleSection* SelectionPanel::createSection(const QString& key, const QString& title) {
    auto* section = new CollapsibleSection(key, title);
    if (_settings) {
        section->setExpanded(!_settings->getCollapsedSections().contains(key));
    }
    connect(section, &CollapsibleSection::expandedChanged, this, [this, key](bool expanded) {
        if (!_settings) {
            return;
        }
        QStringList collapsed = _settings->getCollapsedSections();
        collapsed.removeAll(key);
        if (!expanded) {
            collapsed.append(key);
        }
        _settings->setCollapsedSections(collapsed);
    });
    return section;
}

QWidget* SelectionPanel::createObjectPage() {
    auto* page = new QWidget();
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(ui::theme::spacing::NORMAL);

    // Header: sprite beside who/where/ids, then the edit actions.
    constexpr int HEADER_SPRITE = 64;
    _hoverSpriteLabel = new HoverSpriteLabel();
    _hoverSpriteLabel->setText("—");
    _hoverSpriteLabel->setAlignment(Qt::AlignCenter);
    _hoverSpriteLabel->setFixedSize(HEADER_SPRITE, HEADER_SPRITE);
    _hoverSpriteLabel->setScaledContents(false);
    _hoverSpriteLabel->setStyleSheet(ui::theme::styles::previewArea());
    connect(_hoverSpriteLabel->editButton(), &QPushButton::clicked, this, &SelectionPanel::onChangeFrmClicked);

    _objectNameLabel = new ElidedLabel();
    QFont nameFont = _objectNameLabel->font();
    nameFont.setBold(true);
    _objectNameLabel->setFont(nameFont);
    _objectSummaryLabel = new ElidedLabel();
    _objectIdsLabel = new ElidedLabel();
    _objectIdsLabel->setStyleSheet(ui::theme::styles::smallLabel());

    auto* identity = new QVBoxLayout();
    identity->setSpacing(ui::theme::spacing::TIGHT / 2);
    identity->addWidget(_objectNameLabel);
    identity->addWidget(_objectSummaryLabel);
    identity->addWidget(_objectIdsLabel);

    auto* header = new QHBoxLayout();
    header->setSpacing(ui::theme::spacing::NORMAL);
    header->addWidget(_hoverSpriteLabel, 0, Qt::AlignTop);
    header->addLayout(identity, 1);
    pageLayout->addLayout(header);

    _editProButton = new QPushButton("Edit PRO...");
    _editProButton->setEnabled(false);
    connect(_editProButton, &QPushButton::clicked, this, &SelectionPanel::onEditProClicked);

    // The per-object editors; each is shown only for the objects it applies to (updateObjectInfo).
    _editMenuButton = new QPushButton("Edit");
    _editMenuButton->setEnabled(false);
    auto* editMenu = new QMenu(_editMenuButton);
    _editExitGridAction = editMenu->addAction("Exit Grid...", this, &SelectionPanel::onEditExitGridClicked);
    _editFlagsAction = editMenu->addAction("Flags...", this, &SelectionPanel::onEditFlagsClicked);
    _editLightAction = editMenu->addAction("Light...", this, &SelectionPanel::onEditLightClicked);
    _editDestinationAction = editMenu->addAction("Destination...", this, &SelectionPanel::onEditDestinationClicked);
    _editInteractionAction = editMenu->addAction("Interaction...", this, &SelectionPanel::onEditInteractionClicked);
    _editCritterAction = editMenu->addAction("Critter...", this, &SelectionPanel::onEditCritterClicked);
    _editMenuButton->setMenu(editMenu);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(ui::theme::spacing::TIGHT);
    actions->addWidget(_editProButton);
    actions->addWidget(_editMenuButton);
    actions->addStretch();
    pageLayout->addLayout(actions);

    // Properties: the instance values the header doesn't show.
    _propertiesSection = createSection("selection.properties", "Properties");
    auto* properties = new QFormLayout();
    properties->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    properties->setRowWrapPolicy(QFormLayout::DontWrapRows);
    _messageIdLabel = new ElidedLabel();
    _facingLabel = new ElidedLabel();
    _lightLabel = new ElidedLabel();
    _artPathLabel = new ElidedLabel(Qt::ElideMiddle);
    auto* changeArtButton = new QToolButton();
    changeArtButton->setIcon(createIcon(":/icons/actions/edit.svg"));
    changeArtButton->setToolTip("Change FRM file");
    changeArtButton->setAutoRaise(true);
    connect(changeArtButton, &QToolButton::clicked, this, &SelectionPanel::onChangeFrmClicked);
    auto* artRow = new QHBoxLayout();
    artRow->setSpacing(ui::theme::spacing::TIGHT);
    artRow->addWidget(_artPathLabel, 1);
    artRow->addWidget(changeArtButton);
    properties->addRow("Message ID:", _messageIdLabel);
    properties->addRow("Facing:", _facingLabel);
    properties->addRow("Light:", _lightLabel);
    properties->addRow("Art:", artRow);
    _propertiesSection->setContentLayout(properties);
    pageLayout->addWidget(_propertiesSection);

    // Script: only for the object types the engine lets carry one.
    _scriptSection = createSection("selection.script", "Script");
    _scriptView = new ObjectScriptSection(_resources);
    auto* scriptLayout = new QVBoxLayout();
    scriptLayout->addWidget(_scriptView);
    _scriptSection->setContentLayout(scriptLayout);
    _scriptSection->setVisible(false);
    connect(_scriptView, &ObjectScriptSection::attachRequested, this, &SelectionPanel::onAttachScript);
    connect(_scriptView, &ObjectScriptSection::detachRequested, this, &SelectionPanel::onDetachScript);
    connect(_scriptView, &ObjectScriptSection::editSourceRequested, this, &SelectionPanel::requestEditScriptSource);
    connect(_scriptView, &ObjectScriptSection::showInScriptsPanelRequested, this, &SelectionPanel::requestShowScriptInPanel);
    pageLayout->addWidget(_scriptSection);

    setupInventorySection();
    pageLayout->addWidget(_inventorySection);

    pageLayout->addStretch();
    return page;
}

void SelectionPanel::setupUI() {
    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setContentsMargins(ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN);

    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setMinimumSize(0, 0);
    _scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _contentWidget = new QWidget();
    _contentWidget->setMinimumSize(0, 0);
    _contentWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    _contentLayout = new QVBoxLayout(_contentWidget);
    _contentLayout->setContentsMargins(ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN, ui::constants::PANEL_CONTENT_MARGIN);

    // Stacked widget switches between the object panel and the tile panel.
    _stackedWidget = new QStackedWidget();

    _objectPanelWidget = createObjectPage();

    // === Tile Panel ===
    _tilePanelWidget = new QWidget();
    QVBoxLayout* tileLayout = new QVBoxLayout(_tilePanelWidget);

    _tileInfoGroup = new QGroupBox("Tile Information");
    QFormLayout* tileFormLayout = new QFormLayout(_tileInfoGroup);

    _tilePreviewLabel = new QLabel("No tile selected");
    _tilePreviewLabel->setAlignment(Qt::AlignCenter);
    _tilePreviewLabel->setMinimumHeight(ui::constants::sizes::PREVIEW_TILE_HEIGHT);
    _tilePreviewLabel->setMinimumWidth(ui::constants::sizes::PREVIEW_MEDIUM);
    _tilePreviewLabel->setMaximumHeight(ui::constants::sizes::PREVIEW_TILE_HEIGHT);
    _tilePreviewLabel->setMaximumWidth(ui::constants::sizes::PREVIEW_MEDIUM);
    _tilePreviewLabel->setScaledContents(false);
    _tilePreviewLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _tilePreviewLabel->setStyleSheet(ui::theme::styles::previewArea());
    tileFormLayout->addRow("Preview:", _tilePreviewLabel);

    _tileTypeEdit = new QLineEdit();
    _tileTypeEdit->setReadOnly(true);
    _tileTypeEdit->setPlaceholderText("No tile selected");
    tileFormLayout->addRow("Type:", _tileTypeEdit);

    _tileIndexSpin = new QSpinBox();
    _tileIndexSpin->setRange(0, Map::TILES_PER_ELEVATION - 1);
    _tileIndexSpin->setReadOnly(true);
    _tileIndexSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("Tile Index:", _tileIndexSpin);

    _elevationSpin = new QSpinBox();
    _elevationSpin->setRange(0, 1);
    _elevationSpin->setReadOnly(true);
    _elevationSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("Elevation:", _elevationSpin);

    _hexXSpin = new QSpinBox();
    _hexXSpin->setRange(0, Map::COLS - 1);
    _hexXSpin->setReadOnly(true);
    _hexXSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("Hex X:", _hexXSpin);

    _hexYSpin = new QSpinBox();
    _hexYSpin->setRange(0, Map::ROWS - 1);
    _hexYSpin->setReadOnly(true);
    _hexYSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("Hex Y:", _hexYSpin);

    _worldXSpin = new QSpinBox();
    _worldXSpin->setRange(0, INT_MAX);
    _worldXSpin->setReadOnly(true);
    _worldXSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("World X:", _worldXSpin);

    _worldYSpin = new QSpinBox();
    _worldYSpin->setRange(0, INT_MAX);
    _worldYSpin->setReadOnly(true);
    _worldYSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("World Y:", _worldYSpin);

    // Tile ID shows either the floor or roof value depending on selection.
    _tileIdSpin = new QSpinBox();
    _tileIdSpin->setRange(0, UINT16_MAX);
    _tileIdSpin->setReadOnly(true);
    _tileIdSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    tileFormLayout->addRow("Tile ID:", _tileIdSpin);

    _tileNameEdit = new QLineEdit();
    _tileNameEdit->setReadOnly(true);
    _tileNameEdit->setPlaceholderText("No tile selected");
    tileFormLayout->addRow("Tile Name:", _tileNameEdit);

    tileLayout->addWidget(_tileInfoGroup);
    tileLayout->addStretch();

    _stackedWidget->addWidget(_objectPanelWidget);
    _stackedWidget->addWidget(_tilePanelWidget);

    _contentLayout->addWidget(_stackedWidget);
    _scrollArea->setWidget(_contentWidget);
    _mainLayout->addWidget(_scrollArea);

    clearSelection();
}

void SelectionPanel::setMap(Map* map) {
    _map = map;
}

void SelectionPanel::selectObject(std::shared_ptr<Object> selectedObject) {
    if (selectedObject == nullptr) {
        clearSelection();
        spdlog::debug("SelectionPanel: Object deselected");
    } else {
        _selectedObject = selectedObject;
        _hasTileSelection = false;
        showObjectPanel();
        updateObjectInfo();
        spdlog::debug("SelectionPanel: Object selected with PID {}",
            selectedObject->getMapObject().pro_pid);
    }
}

void SelectionPanel::selectTile(int tileIndex, int elevation, bool isRoof) {
    _selectedTileIndex = tileIndex;
    _selectedElevation = elevation;
    _isRoofSelected = isRoof;
    _hasTileSelection = true;
    _selectedObject.reset();

    showTilePanel();
    updateTileInfo();

    spdlog::debug("SelectionPanel: Tile selected - index: {}, elevation: {}, isRoof: {}",
        tileIndex, elevation, isRoof);
}

void SelectionPanel::clearSelection() {
    _selectedObject.reset();
    _hasTileSelection = false;
    clearObjectInfo();
    clearTileInfo();

    // Show object panel by default when nothing is selected
    showObjectPanel();

    spdlog::debug("SelectionPanel: Selection cleared");
}

void SelectionPanel::showObjectPanel() {
    _stackedWidget->setCurrentWidget(_objectPanelWidget);
}

void SelectionPanel::showTilePanel() {
    _stackedWidget->setCurrentWidget(_tilePanelWidget);
}

namespace {

    QString hexId(uint32_t value) {
        return QString("0x%1").arg(value, 8, 16, QChar('0')).toUpper().replace("0X", "0x");
    }

    QString facingText(uint32_t direction) {
        static const std::array<const char*, 6> names{ "NE", "E", "SE", "SW", "W", "NW" };
        return direction < names.size() ? QString("%1 (%2)").arg(names[direction]).arg(direction)
                                        : QString("%1 (out of range)").arg(direction);
    }

    QString lightText(const MapObject& object) {
        if (object.light_radius == 0 && object.light_intensity == 0) {
            return "None";
        }
        // The engine's full intensity is 65536 (light.h LIGHT_INTENSITY_MAX).
        return QString("radius %1 · intensity %2 (%3%)")
            .arg(object.light_radius)
            .arg(object.light_intensity)
            .arg(object.light_intensity * 100 / 65536);
    }

    QPixmap spritePixmap(const Object& object, int maxSide) {
        const auto& sprite = object.getSprite();
        const auto image = sprite.getTexture().copyToImage();
        const auto rect = sprite.getTextureRect();
        QImage qImage(image.getPixelsPtr(), image.getSize().x, image.getSize().y, QImage::Format_RGBA8888);
        if (rect.size.x > 0 && rect.size.y > 0) {
            qImage = qImage.copy(rect.position.x, rect.position.y, rect.size.x, rect.size.y);
        } else {
            qImage = qImage.copy(); // detach from the SFML pixels, which die with `image`
        }
        QPixmap pixmap = QPixmap::fromImage(qImage);
        if (!pixmap.isNull() && (pixmap.width() > maxSide || pixmap.height() > maxSide)) {
            pixmap = pixmap.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return pixmap;
    }

} // namespace

void SelectionPanel::updateObjectInfo() {
    if (!_selectedObject || !_selectedObject.value() || !_selectedObject.value()->hasMapObject()) {
        clearObjectInfo();
        return;
    }
    const auto mapObjectPtr = _selectedObject.value()->getMapObjectPtr();
    const MapObject& mapObject = *mapObjectPtr;

    // The map's own data is shown even when the proto can't be loaded; only what needs the proto
    // (name, message id, art fallback, subtype editors) reports the gap.
    Pro* pro = nullptr;
    try {
        pro = _resources.loadPro(mapObject.pro_pid);
    } catch (const std::exception& e) {
        spdlog::warn("SelectionPanel: proto {} not loaded: {}", hexId(mapObject.pro_pid).toStdString(), e.what());
    }

    QString name;
    if (pro) {
        try {
            if (Msg* msg = ProHelper::msgFile(_resources, pro->type())) {
                name = QString::fromStdString(msg->message(pro->header.message_id).text);
            }
        } catch (const std::exception& e) {
            spdlog::warn("SelectionPanel: no name for message {}: {}", pro->header.message_id, e.what());
        }
    }
    _objectNameLabel->setFullText(!pro ? QString("Proto %1 not found").arg(hexId(mapObject.pro_pid))
            : name.isEmpty()           ? QString("Unnamed (message %1)").arg(pro->header.message_id)
                                       : name);

    const ObjectType objectType = mapObject.objectType();
    QString summary = QString::fromStdString(Pro::typeToString(objectType));
    if (mapObject.position >= 0) {
        const int width = HexagonGrid::GRID_WIDTH;
        summary += QString(" · hex %1 (%2, %3)").arg(mapObject.position).arg(mapObject.position % width).arg(mapObject.position / width);
    }
    summary += QString(" · elevation %1").arg(mapObject.elevation);
    _objectSummaryLabel->setFullText(summary);

    // frm_pid == 0 means the object uses its prototype's FID.
    const uint32_t activeFid = mapObject.frm_pid != 0 || !pro ? mapObject.frm_pid : static_cast<uint32_t>(pro->header.FID);
    _objectIdsLabel->setFullText(QString("PID %1 · FID %2").arg(hexId(mapObject.pro_pid), hexId(activeFid)));

    _messageIdLabel->setFullText(pro ? QString::number(pro->header.message_id) : QString("—"));
    _facingLabel->setFullText(facingText(mapObject.direction));
    _lightLabel->setFullText(lightText(mapObject));
    try {
        _artPathLabel->setFullText(QString::fromStdString(_resources.frmResolver().resolve(activeFid)));
    } catch (const std::exception& e) {
        _artPathLabel->setFullText(QString("Unresolved: %1").arg(e.what()));
    }

    // Edit actions, gated by object type as the engine's mapper gates its instance editors.
    const bool isScenery = objectType == ObjectType::Scenery;
    const SceneryType sceneryType = isScenery && pro ? static_cast<SceneryType>(pro->objectSubtypeId()) : SceneryType::Generic;
    const bool hasDestination = isScenery
        && (sceneryType == SceneryType::Stairs || sceneryType == SceneryType::LadderDown
            || sceneryType == SceneryType::LadderUp || sceneryType == SceneryType::Elevator);
    const bool isDoor = isScenery && sceneryType == SceneryType::Door;
    const bool isContainer = objectType == ObjectType::Item && pro && pro->itemType() == ItemType::Container;

    _hoverSpriteLabel->editButton()->setEnabled(true);
    _editProButton->setEnabled(pro != nullptr);
    _editMenuButton->setEnabled(true);
    _editExitGridAction->setVisible(mapObject.isExitGridMarker());
    _editFlagsAction->setVisible(true);
    _editLightAction->setVisible(true);
    _editDestinationAction->setVisible(hasDestination);
    _editInteractionAction->setVisible(isDoor || isContainer);
    _editCritterAction->setVisible(objectType == ObjectType::Critter);

    // Scripts can be attached to items, critters, scenery and walls (the engine mapper's instance
    // editors); tiles and misc markers cannot.
    const bool scriptable = objectType == ObjectType::Item || objectType == ObjectType::Critter
        || objectType == ObjectType::Scenery || objectType == ObjectType::Wall;
    _scriptSection->setVisible(scriptable);
    if (scriptable) {
        updateScriptSection();
    }

    const QPixmap pixmap = spritePixmap(*_selectedObject.value(), _hoverSpriteLabel->width());
    if (pixmap.isNull()) {
        _hoverSpriteLabel->setPixmap({});
        _hoverSpriteLabel->setText("?");
    } else {
        _hoverSpriteLabel->setPixmap(pixmap);
    }

    _propertiesSection->setVisible(true);
    updateInventorySection();
}

void geck::SelectionPanel::updateTileInfo() {
    if (!_hasTileSelection || !_map) {
        clearTileInfo();
        return;
    }

    try {
        auto& mapFile = _map->getMapFile();

        if (mapFile.tiles.find(_selectedElevation) == mapFile.tiles.end()) {
            spdlog::warn("SelectionPanel::updateTileInfo: Selected elevation {} does not exist in map data", _selectedElevation);
            clearTileInfo();
            return;
        }

        auto& tile = mapFile.tiles.at(_selectedElevation).at(_selectedTileIndex);

        // Calculate hex coordinates from tile index
        uint32_t hexX = static_cast<uint32_t>(std::ceil(static_cast<double>(_selectedTileIndex) / 100));
        uint32_t hexY = _selectedTileIndex % 100;

        // Calculate world coordinates
        uint32_t worldX = (100 - hexY - 1) * 48 + 32 * (hexX - 1);
        uint32_t worldY = hexX * 24 + (hexY - 1) * 12 + 1;

        _tileTypeEdit->setText(_isRoofSelected ? "Roof Tile" : "Floor Tile");
        _tileIndexSpin->setValue(_selectedTileIndex);
        _elevationSpin->setValue(_selectedElevation);
        _hexXSpin->setValue(static_cast<int>(hexX));
        _hexYSpin->setValue(static_cast<int>(hexY));
        _worldXSpin->setValue(static_cast<int>(worldX));
        _worldYSpin->setValue(static_cast<int>(worldY));

        uint16_t floorTileId = tile.getFloor();
        uint16_t roofTileId = tile.getRoof();

        // tiles.lst maps tile IDs to FRM filenames.
        try {
            auto tilesList = _resources.repository().find<Lst>("art/tiles/tiles.lst");

            if (tilesList) {
                auto tileNames = tilesList->list();

                uint16_t currentTileId = _isRoofSelected ? roofTileId : floorTileId;
                _tileIdSpin->setValue(currentTileId);

                if (currentTileId < tileNames.size()) {
                    _tileNameEdit->setText(QString::fromStdString(tileNames.at(currentTileId)));
                } else {
                    _tileNameEdit->setText("Invalid tile ID");
                }

                loadTilePreview(tilesList, currentTileId);

            } else {
                _tileIdSpin->setValue(0);
                _tileNameEdit->setText("tiles.lst not found");
                _tilePreviewLabel->setText("Preview unavailable");
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to load tile information: {}", e.what());
            _tileIdSpin->setValue(0);
            _tileNameEdit->setText("Error loading tile info");
            _tilePreviewLabel->setText("Preview error");
        }

        _tileInfoGroup->setTitle(QString("Tile Information - %1").arg(_isRoofSelected ? "Roof" : "Floor"));

    } catch (const std::exception& e) {
        spdlog::error("Error updating tile info: {}", e.what());
        clearTileInfo();
    }
}

void geck::SelectionPanel::clearObjectInfo() {
    _objectNameLabel->setFullText("No object selected");
    _objectSummaryLabel->setFullText({});
    _objectIdsLabel->setFullText({});

    _hoverSpriteLabel->editButton()->setEnabled(false);
    _hoverSpriteLabel->setPixmap({});
    _hoverSpriteLabel->setText("—");
    _editProButton->setEnabled(false);
    _editMenuButton->setEnabled(false);

    _propertiesSection->setVisible(false);
    _scriptSection->setVisible(false);
    _inventorySection->setVisible(false);
}

void geck::SelectionPanel::clearTileInfo() {
    _tileTypeEdit->clear();
    _tileTypeEdit->setPlaceholderText("No tile selected");

    _tileIndexSpin->setValue(0);
    _elevationSpin->setValue(0);
    _hexXSpin->setValue(0);
    _hexYSpin->setValue(0);
    _worldXSpin->setValue(0);
    _worldYSpin->setValue(0);

    _tileIdSpin->setValue(0);

    _tileNameEdit->clear();
    _tileNameEdit->setPlaceholderText("No tile selected");

    _tilePreviewLabel->clear();
    _tilePreviewLabel->setText("No tile selected");
    _tileInfoGroup->setTitle("Tile Information");
}

void geck::SelectionPanel::loadTilePreview(Lst* tilesList, uint16_t tileId) {
    try {
        auto tileNames = tilesList->list();
        if (tileId >= tileNames.size()) {
            _tilePreviewLabel->setText("Invalid tile ID");
            return;
        }

        std::string tilePath = "art/tiles/" + tileNames.at(tileId);

        auto& texture = _resources.textures().get(tilePath);

        auto image = texture.copyToImage();
        const std::uint8_t* pixels = image.getPixelsPtr();
        QImage qImage(pixels, image.getSize().x, image.getSize().y, QImage::Format_RGBA8888);

        QPixmap pixmap = QPixmap::fromImage(qImage);
        if (!pixmap.isNull()) {
            QSize maxSize(128, 96);

            if (pixmap.width() > maxSize.width() || pixmap.height() > maxSize.height()) {
                pixmap = pixmap.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }

            _tilePreviewLabel->setPixmap(pixmap);
            _tilePreviewLabel->setText("");
        } else {
            _tilePreviewLabel->setText("Failed to load tile");
        }

    } catch (const std::exception& e) {
        spdlog::warn("Failed to load tile preview: {}", e.what());
        _tilePreviewLabel->setText("Preview error");
    }
}

void geck::SelectionPanel::handleSelectionChanged(const selection::SelectionState& selection, int elevation) {
    if (selection.isEmpty()) {
        clearSelection();
        return;
    }

    // Multi-selection shows a summary rather than per-tile details.
    if (selection.items.size() > 1) {
        showTilePanel();

        _tileInfoGroup->setTitle(QString("Tile Selection (%1 tiles)").arg(selection.items.size()));

        _tileIndexSpin->setValue(0);
        _elevationSpin->setValue(elevation);
        _hexXSpin->setValue(0);
        _hexYSpin->setValue(0);
        _tileTypeEdit->clear();
        _tileIdSpin->setValue(0);
        _tileNameEdit->clear();
        _tilePreviewLabel->setText(QString("Multiple tiles selected (%1)").arg(selection.items.size()));

        _elevationSpin->setEnabled(true);
        _tileTypeEdit->setEnabled(false);
        _tileIdSpin->setEnabled(false);
        _tileNameEdit->setEnabled(false);

        spdlog::debug("SelectionPanel: Multiple selection - {} tiles", selection.items.size());
        return;
    }

    const auto& item = selection.items[0];
    switch (item.type) {
        case selection::SelectionType::OBJECT: {
            auto object = item.getObject();
            if (object) {
                selectObject(object);
            }
            break;
        }
        case selection::SelectionType::ROOF_TILE:
        case selection::SelectionType::FLOOR_TILE: {
            int tileIndex = item.getTileIndex();
            bool isRoof = (item.type == selection::SelectionType::ROOF_TILE);
            selectTile(tileIndex, elevation, isRoof);
            break;
        }
        case selection::SelectionType::HEX: {
            // No dedicated hex info panel yet; log only.
            int hexIndex = item.getHexIndex();
            spdlog::debug("SelectionPanel: Hex {} selected", hexIndex);
            break;
        }
    }
}

void SelectionPanel::onChangeFrmClicked() {
    if (!_selectedObject || !_selectedObject.value()) {
        return;
    }

    auto& mapObject = _selectedObject.value()->getMapObject();

    FrmSelectorDialog dialog(_resources, this);

    uint32_t currentFrmPid = mapObject.frm_pid != 0 ? mapObject.frm_pid : mapObject.pro_pid;
    dialog.setInitialFrmPid(currentFrmPid);

    // Filter the dialog by the object type encoded in the current FRM PID.
    const auto objectTypeFilter = FrmSelectorDialog::filterForFid(currentFrmPid);
    dialog.setObjectTypeFilter(objectTypeFilter);
    if (objectTypeFilter.has_value()) {
        spdlog::debug("SelectionPanel: Filtering FRM dialog by object type: {}", static_cast<int>(*objectTypeFilter));
    }

    if (dialog.exec() == QDialog::Accepted) {
        std::optional<uint32_t> newFrmPid = dialog.getSelectedFrmPid();
        std::string newFrmPath = dialog.getSelectedFrmPath();

        if (!newFrmPath.empty()) {
            // Validate that the FRM file actually exists before attempting the change
            try {
                auto testLoad = _resources.repository().load<Frm>(newFrmPath);
                if (!testLoad) {
                    spdlog::error("SelectionPanel: FRM file not found or invalid: {} - aborting change", newFrmPath);
                    return;
                }
            } catch (const std::exception& e) {
                spdlog::error("SelectionPanel: Failed to validate FRM file {}: {} - aborting change", newFrmPath, e.what());
                return;
            }

            // Try to update MapObject's frm_pid if we have a valid derived FID.
            // The visual representation is updated per-branch below, only once the
            // FID outcome (present vs. derivation failure) is known.
            if (newFrmPid.has_value()) {
                const uint32_t derivedFrmPid = *newFrmPid;

                Q_EMIT objectFrmPathChanged(_selectedObject.value(), newFrmPath);

                const bool isCustomFid = FrmSelectorDialog::isCustomFid(derivedFrmPid);

                if (derivedFrmPid != currentFrmPid) {
                    if (isCustomFid) {
                        spdlog::warn("SelectionPanel: Using custom FID 0x{:08X} for non-LST FRM - may not work in game", derivedFrmPid);

                        // Update the visual but keep the original frm_pid for game compatibility:
                        // the editor shows the new FRM while the saved map stays loadable in-game.
                        spdlog::debug("SelectionPanel: Keeping original frm_pid {} for game compatibility, visual uses custom FRM", currentFrmPid);

                        Q_EMIT statusMessage(QString("Warning: Custom FRM may not display correctly in game"));
                    } else {
                        // Valid LST-based FID, safe to persist.
                        mapObject.frm_pid = derivedFrmPid;
                        spdlog::debug("SelectionPanel: Updated MapObject frm_pid from {} to {} for persistent save",
                            currentFrmPid, derivedFrmPid);
                    }

                    updateObjectInfo();
                } else {
                    spdlog::debug("SelectionPanel: FRM PID unchanged ({}), no MapObject update needed", derivedFrmPid);
                    _artPathLabel->setFullText(QString::fromStdString(newFrmPath));
                }
            } else {
                // FID derivation failed (no reliable FID for this path).
                spdlog::warn("SelectionPanel: Could not derive reliable FID for path: {} - using alternative approach",
                    newFrmPath);

                // The FRM may be valid but simply absent from the LST: keep the visual
                // change and warn the user about potential game compatibility issues.

                size_t lastSlash = newFrmPath.find_last_of('/');
                std::string filename = (lastSlash != std::string::npos) ? newFrmPath.substr(lastSlash + 1) : newFrmPath;

                // TODO: verify in engine that the comment below is valid
                // Critters are a special case: many FRMs work in-game even if not in the LST.
                // Deliberate visual-only fallback - keep the original frm_pid for game
                // compatibility while still showing the new FRM in the editor.
                if (FrmId(currentFrmPid).objectType() == ObjectType::Critter) {
                    spdlog::warn("SelectionPanel: FRM '{}' not found in critters.lst - change may not persist in game", filename);
                    spdlog::debug("SelectionPanel: Keeping original FRM PID ({}) for game compatibility", currentFrmPid);

                    Q_EMIT statusMessage(QString("Warning: FRM '%1' may not display correctly in game - not found in critters.lst")
                            .arg(QString::fromStdString(filename)));
                }

                // Visual-only update for the derivation-failed path; frm_pid is left untouched.
                Q_EMIT objectFrmPathChanged(_selectedObject.value(), newFrmPath);

                _artPathLabel->setFullText(QString::fromStdString(newFrmPath));
                updateObjectInfo();
            }

            spdlog::debug("SelectionPanel: Changed object FRM visual to path: {}", newFrmPath);

            // Keep the object highlighted after the FRM change. Delay via a single-shot
            // timer so the texture update is fully processed before re-highlighting.
            if (_selectedObject.has_value() && _selectedObject.value()) {
                QTimer::singleShot(50, this, [this]() {
                    if (_selectedObject.has_value() && _selectedObject.value()) {
                        Q_EMIT requestObjectHighlight(_selectedObject.value());
                    }
                });
            }
        }
    }
}

void SelectionPanel::onEditProClicked() {
    if (!_selectedObject || !_selectedObject.value()) {
        return;
    }

    openProEditorForSelectedObject();
}

bool SelectionPanel::openProEditorForSelectedObject() {
    if (!_selectedObject.has_value()) {
        return false;
    }

    auto selectedObject = _selectedObject.value();
    if (!selectedObject || !selectedObject->hasMapObject()) {
        spdlog::debug("SelectionPanel::openProEditorForSelectedObject() - selected object has no MapObject");
        return false;
    }

    auto& mapObject = selectedObject->getMapObject();

    try {
        std::string proFileName = ProHelper::basePath(_resources, mapObject.pro_pid);
        spdlog::debug("SelectionPanel::openProEditorForSelectedObject() - opening PRO: {}", proFileName);

        auto fileData = _resources.files().readRawBytes(proFileName);
        if (!fileData) {
            spdlog::error("SelectionPanel::openProEditorForSelectedObject() - could not open PRO file: {}", proFileName);
            return false;
        }

        auto pro = ReaderFactory::readFileFromMemory<Pro>(*fileData, proFileName);
        if (!pro) {
            spdlog::error("SelectionPanel::openProEditorForSelectedObject() - could not parse PRO file");
            return false;
        }

        ProEditorDialog dialog(_resources, std::shared_ptr<Pro>(pro.release()), this);
        dialog.exec();

        return true;

    } catch (const std::exception& e) {
        spdlog::error("SelectionPanel::openProEditorForSelectedObject() - exception: {}", e.what());
        return false;
    }
}

void SelectionPanel::onEditExitGridClicked() {
    if (!_selectedObject || !_selectedObject.value()) {
        return;
    }

    Q_EMIT requestExitGridEditor(_selectedObject.value());
}

void SelectionPanel::onEditFlagsClicked() {
    if (!_selectedObject || !_selectedObject.value()) {
        return;
    }
    auto object = _selectedObject.value();
    auto mapObject = object->getMapObjectPtr();
    if (!mapObject) {
        return;
    }

    const ObjectType objectType = mapObject->objectType();
    ObjectFlagsDialog dialog(mapObject->flags, objectType, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const uint32_t newFlags = dialog.getFlags();
    if (newFlags == mapObject->flags) {
        return;
    }

    auto before = ObjectCommandController::captureInstanceState(*mapObject);
    auto after = before;
    after.flags = newFlags;
    Q_EMIT requestInstanceEdit(object, before, after, "Edit Object Flags");
}

void SelectionPanel::onEditLightClicked() {
    if (!_selectedObject || !_selectedObject.value()) {
        return;
    }
    auto object = _selectedObject.value();
    auto mapObject = object->getMapObjectPtr();
    if (!mapObject) {
        return;
    }

    LightPropertiesDialog dialog(mapObject->light_radius, mapObject->light_intensity, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const uint32_t newRadius = dialog.getLightRadius();
    const uint32_t newIntensity = dialog.getLightIntensity();
    if (newRadius == mapObject->light_radius && newIntensity == mapObject->light_intensity) {
        return;
    }

    auto before = ObjectCommandController::captureInstanceState(*mapObject);
    auto after = before;
    after.lightRadius = newRadius;
    after.lightIntensity = newIntensity;
    Q_EMIT requestInstanceEdit(object, before, after, "Edit Light Properties");
}

void SelectionPanel::onEditDestinationClicked() {
    if (!_selectedObject || !_selectedObject.value()) {
        return;
    }
    auto object = _selectedObject.value();
    auto mapObject = object->getMapObjectPtr();
    if (!mapObject) {
        return;
    }

    SceneryType sceneryType = SceneryType::Generic;
    try {
        auto pro = _resources.loadPro(mapObject->pro_pid);
        if (!pro || pro->type() != ObjectType::Scenery) {
            return;
        }
        sceneryType = static_cast<SceneryType>(pro->objectSubtypeId());
    } catch (const std::exception& e) {
        spdlog::warn("onEditDestinationClicked: failed to load pro: {}", e.what());
        return;
    }

    SceneryDestinationDialog dialog(sceneryType, mapObject->elevhex, mapObject->map,
        mapObject->elevtype, mapObject->elevlevel, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    auto before = ObjectCommandController::captureInstanceState(*mapObject);
    auto after = before;
    after.elevhex = dialog.getElevhex();
    after.map = dialog.getMap();
    after.elevtype = dialog.getElevtype();
    after.elevlevel = dialog.getElevlevel();
    if (after.elevhex == before.elevhex && after.map == before.map
        && after.elevtype == before.elevtype && after.elevlevel == before.elevlevel) {
        return;
    }
    Q_EMIT requestInstanceEdit(object, before, after, "Edit Scenery Destination");
}

void SelectionPanel::onEditInteractionClicked() {
    if (!_selectedObject || !_selectedObject.value()) {
        return;
    }
    auto object = _selectedObject.value();
    auto mapObject = object->getMapObjectPtr();
    if (!mapObject) {
        return;
    }

    bool isDoor = false;
    try {
        auto pro = _resources.loadPro(mapObject->pro_pid);
        if (!pro) {
            return;
        }
        isDoor = pro->type() == ObjectType::Scenery
            && static_cast<SceneryType>(pro->objectSubtypeId()) == SceneryType::Door;
    } catch (const std::exception& e) {
        spdlog::warn("onEditInteractionClicked: failed to load pro: {}", e.what());
        return;
    }

    // Doors keep lock/jam in their openFlags (our `walkthrough`); containers keep
    // them in the object data flags (our `unknown11`). The dialog edits whichever
    // applies and leaves the other untouched.
    InstancePropertiesDialog dialog(isDoor, mapObject->walkthrough, mapObject->unknown11, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const uint32_t newWalkthrough = dialog.getDoorOpenFlags();
    const uint32_t newDataFlags = dialog.getContainerDataFlags();
    if (newWalkthrough == mapObject->walkthrough && newDataFlags == mapObject->unknown11) {
        return;
    }

    auto before = ObjectCommandController::captureInstanceState(*mapObject);
    auto after = before;
    after.walkthrough = newWalkthrough;
    after.dataFlags = newDataFlags;
    Q_EMIT requestInstanceEdit(object, before, after, "Edit Interaction State");
}

void SelectionPanel::onEditCritterClicked() {
    if (!_selectedObject || !_selectedObject.value()) {
        return;
    }
    auto object = _selectedObject.value();
    auto mapObject = object->getMapObjectPtr();
    if (!mapObject) {
        return;
    }

    const AiTxt aiTxt = resource::loadAiTxt(_resources); // names the AI packet combo; empty -> raw number
    CritterPropertiesDialog dialog(mapObject->ai_packet, mapObject->group_id,
        mapObject->current_hp, mapObject->current_rad, mapObject->current_poison, aiTxt, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    auto before = ObjectCommandController::captureInstanceState(*mapObject);
    auto after = before;
    after.aiPacket = dialog.getAiPacket();
    after.groupId = dialog.getTeam();
    after.currentHp = dialog.getHp();
    after.currentRad = dialog.getRadiation();
    after.currentPoison = dialog.getPoison();
    if (after.aiPacket == before.aiPacket && after.groupId == before.groupId
        && after.currentHp == before.currentHp && after.currentRad == before.currentRad
        && after.currentPoison == before.currentPoison) {
        return;
    }
    Q_EMIT requestInstanceEdit(object, before, after, "Edit Critter Properties");
}

// Script attach/detach open the picker here, then route the change through the
// editor's ObjectCommandController (via MainWindow) so it is undoable.

void SelectionPanel::updateScriptSection() {
    std::shared_ptr<MapObject> object;
    if (_selectedObject && _selectedObject.value() && _selectedObject.value()->hasMapObject()) {
        object = _selectedObject.value()->getMapObjectPtr();
    }
    _scriptView->showObject(_map, object);
}

void SelectionPanel::setScriptStatusProvider(std::function<ScriptSourceService::ScriptStatus(int)> provider) {
    _scriptView->setStatusProvider(std::move(provider));
    if (_scriptSection->isVisible()) {
        updateScriptSection();
    }
}

void SelectionPanel::onAttachScript(int programIndex) {
    if (!_selectedObject || !_selectedObject.value() || !_map || programIndex < 0) {
        return;
    }
    auto mapObject = _selectedObject.value()->getMapObjectPtr();
    if (!mapObject) {
        return;
    }

    // Critters use the CRITTER section; items/scenery/walls use the ITEM section.
    const int scriptType = (mapObject->objectType() == ObjectType::Critter)
        ? static_cast<int>(MapScript::ScriptType::CRITTER)
        : static_cast<int>(MapScript::ScriptType::ITEM);

    // Direct (synchronous) connection: the model is mutated before this returns,
    // so updateScriptSection() reads the new state.
    Q_EMIT requestAttachScript(mapObject, scriptType, static_cast<uint32_t>(programIndex));
    updateScriptSection();
    Q_EMIT statusMessage(QString("Attached script %1 to object").arg(programIndex));
}

void SelectionPanel::onDetachScript() {
    if (!_selectedObject || !_selectedObject.value() || !_map) {
        return;
    }
    auto mapObject = _selectedObject.value()->getMapObjectPtr();
    if (!mapObject) {
        return;
    }
    Q_EMIT requestDetachScript(mapObject);
    updateScriptSection();
}

// Returns the selected object's MapObject (the inventory holder). The inventory
// section is only shown for container/critter types, so callers reach this only
// for objects that can hold inventory; it does not re-check that here.
MapObject* SelectionPanel::selectedInventoryHolder() const {
    if (!_selectedObject || !_selectedObject.value()) {
        return nullptr;
    }
    return _selectedObject.value()->getMapObjectPtr().get();
}

void SelectionPanel::refresh() {
    if (_selectedObject && _selectedObject.value()) {
        updateObjectInfo();
    }
}

void SelectionPanel::commitInventoryEdit(std::vector<std::shared_ptr<MapObject>> before) {
    auto* holder = selectedInventoryHolder();
    if (!holder) {
        return;
    }
    auto after = ObjectCommandController::cloneInventory(holder->inventory);
    populateInventoryTree();
    Q_EMIT requestInventoryEdit(_selectedObject.value()->getMapObjectPtr(),
        std::move(before), std::move(after));
}

void SelectionPanel::onAddInventoryClicked() {
    auto* holder = selectedInventoryHolder();
    if (!holder) {
        return;
    }

    ItemSelectorDialog dialog(_resources, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto itemPid = dialog.selectedPid();
    if (!itemPid) {
        return;
    }

    Pro* pro = nullptr;
    try {
        pro = _resources.loadPro(*itemPid);
    } catch (const std::exception& e) {
        spdlog::warn("onAddInventoryClicked: failed to load proto for pid {}: {}", *itemPid, e.what());
    }
    if (!pro) {
        QMessageBox::warning(this, "Add Inventory Item",
            QString("No item prototype found for PID 0x%1.").arg(*itemPid, 8, 16, QChar('0')));
        return;
    }

    auto before = ObjectCommandController::cloneInventory(holder->inventory);

    auto item = std::make_unique<MapObject>();
    item->pro_pid = *itemPid;
    item->frm_pid = pro->header.FID;
    item->amount = static_cast<uint32_t>(dialog.selectedAmount());
    item->elevation = holder->elevation;
    item->position = holder->position;

    holder->inventory.push_back(std::move(item));
    holder->objects_in_inventory = static_cast<uint32_t>(holder->inventory.size());

    commitInventoryEdit(std::move(before));
}

void SelectionPanel::onRemoveInventoryClicked() {
    QTreeWidgetItem* currentItem = _inventoryTree->currentItem();
    auto* holder = selectedInventoryHolder();
    if (!currentItem || !holder) {
        return;
    }

    const int row = _inventoryTree->indexOfTopLevelItem(currentItem);
    if (row < 0 || row >= static_cast<int>(holder->inventory.size())) {
        return;
    }

    auto before = ObjectCommandController::cloneInventory(holder->inventory);
    holder->inventory.erase(holder->inventory.begin() + row);
    holder->objects_in_inventory = static_cast<uint32_t>(holder->inventory.size());

    commitInventoryEdit(std::move(before));
}

void SelectionPanel::onInventoryItemChanged(QTreeWidgetItem* item, int column) {
    if (!item || column != COLUMN_AMOUNT) {
        return;
    }
    auto* holder = selectedInventoryHolder();
    if (!holder) {
        return;
    }

    const int row = _inventoryTree->indexOfTopLevelItem(item);
    if (row < 0 || row >= static_cast<int>(holder->inventory.size())) {
        return;
    }

    bool ok = false;
    const int newAmount = item->text(COLUMN_AMOUNT).toInt(&ok);
    if (!ok || newAmount < 1) {
        // Revert to the stored amount.
        item->setText(COLUMN_AMOUNT, QString::number(holder->inventory[row]->amount));
        return;
    }

    auto before = ObjectCommandController::cloneInventory(holder->inventory);
    holder->inventory[row]->amount = static_cast<uint32_t>(newAmount);

    auto after = ObjectCommandController::cloneInventory(holder->inventory);
    Q_EMIT requestInventoryEdit(_selectedObject.value()->getMapObjectPtr(),
        std::move(before), std::move(after));
}

void SelectionPanel::setupInventorySection() {
    _inventorySection = createSection("selection.inventory", "Inventory");
    _inventorySection->setVisible(false);

    auto* inventoryLayout = new QVBoxLayout();

    _inventoryTree = new QTreeWidget();
    _inventoryTree->setHeaderLabels({ "", "Name", "Type", "Qty" });
    _inventoryTree->setRootIsDecorated(false);
    _inventoryTree->setAlternatingRowColors(true);
    _inventoryTree->setSelectionMode(QAbstractItemView::SingleSelection);
    _inventoryTree->setMinimumWidth(0);
    _inventoryTree->setMinimumHeight(ui::constants::sizes::PANEL_MIN_HEIGHT);
    _inventoryTree->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
    _inventoryTree->setUniformRowHeights(true);
    _inventoryTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // The name takes what is left; icon, type and quantity size to their content.
    _inventoryTree->header()->setStretchLastSection(false);
    _inventoryTree->header()->setSectionResizeMode(COLUMN_ICON, QHeaderView::Fixed);
    _inventoryTree->header()->resizeSection(COLUMN_ICON, ICON_SIZE + ui::theme::spacing::NORMAL);
    _inventoryTree->header()->setSectionResizeMode(COLUMN_NAME, QHeaderView::Stretch);
    _inventoryTree->header()->setSectionResizeMode(COLUMN_TYPE, QHeaderView::ResizeToContents);
    _inventoryTree->header()->setSectionResizeMode(COLUMN_AMOUNT, QHeaderView::ResizeToContents);

    _inventoryTree->setItemDelegateForColumn(COLUMN_AMOUNT, _amountDelegate);

    _emptyInventoryLabel = new QLabel("No inventory items");
    _emptyInventoryLabel->setAlignment(Qt::AlignCenter);
    _emptyInventoryLabel->setStyleSheet(ui::theme::styles::smallLabel());

    _inventoryViewStack = new QStackedWidget();
    _inventoryViewStack->addWidget(_inventoryTree);
    _inventoryViewStack->addWidget(_emptyInventoryLabel);

    connect(_inventoryTree, &QTreeWidget::itemChanged, this, &SelectionPanel::onInventoryItemChanged);

    inventoryLayout->addWidget(_inventoryViewStack);

    auto* buttonLayout = new QHBoxLayout();

    _addInventoryButton = new QPushButton("Add Item...");
    _removeInventoryButton = new QPushButton("Remove");
    _removeInventoryButton->setEnabled(false);

    connect(_addInventoryButton, &QPushButton::clicked, this, &SelectionPanel::onAddInventoryClicked);
    connect(_removeInventoryButton, &QPushButton::clicked, this, &SelectionPanel::onRemoveInventoryClicked);
    connect(_inventoryTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        _removeInventoryButton->setEnabled(_inventoryTree->currentItem() != nullptr);
    });

    buttonLayout->addWidget(_addInventoryButton);
    buttonLayout->addWidget(_removeInventoryButton);
    buttonLayout->addStretch();

    inventoryLayout->addLayout(buttonLayout);
    _inventorySection->setContentLayout(inventoryLayout);
}

void SelectionPanel::updateInventorySection() {
    auto* holder = selectedInventoryHolder();
    // Only containers and critters can hold inventory.
    bool hasInventory = false;
    if (holder) {
        try {
            if (const Pro* pro = _resources.loadPro(holder->pro_pid)) {
                hasInventory = (pro->type() == ObjectType::Item && pro->itemType() == ItemType::Container)
                    || pro->type() == ObjectType::Critter;
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to load pro file for inventory check: {}", e.what());
        }
    }
    _inventorySection->setVisible(hasInventory);
    if (hasInventory) {
        populateInventoryTree();
    }
}

void SelectionPanel::populateInventoryTree() {
    spdlog::debug("SelectionPanel::populateInventoryTree: Starting to populate inventory tree");
    _inventoryTree->clear();

    if (!_selectedObject || !_selectedObject.value()) {
        spdlog::debug("SelectionPanel::populateInventoryTree: No selected object");
        return;
    }

    auto object = _selectedObject.value();
    auto mapObject = object->getMapObjectPtr();

    if (!mapObject) {
        spdlog::debug("SelectionPanel::populateInventoryTree: No map object");
        return;
    }

    if (mapObject->inventory.empty()) {
        _inventoryViewStack->setCurrentWidget(_emptyInventoryLabel);
        _inventorySection->setTitle("Inventory");
        _removeInventoryButton->setEnabled(false);
        return;
    }

    spdlog::debug("SelectionPanel::populateInventoryTree: Found {} inventory items", mapObject->inventory.size());

    ui::inventory::InventoryTreeOptions options;
    options.iconSize = ICON_SIZE;
    options.editable = true;
    options.setIconSizeHint = true;
    options.userRoleIsPid = true;
    options.userRoleColumn = ui::inventory::COLUMN_ICON;
    options.iconProvider = [this](const MapObject& item) { return getItemIcon(item); };
    // Block itemChanged while we repopulate so the per-row amount handler does not
    // fire against half-built rows.
    _inventoryTree->blockSignals(true);
    ui::inventory::populateInventoryTree(_inventoryTree, _resources, mapObject->inventory, options);
    _inventoryTree->blockSignals(false);

    if (_inventoryTree->topLevelItemCount() == 0) {
        _inventoryViewStack->setCurrentWidget(_emptyInventoryLabel);
        _removeInventoryButton->setEnabled(false);
        return;
    }

    _inventoryViewStack->setCurrentWidget(_inventoryTree);
    _inventorySection->setTitle(QString("Inventory (%1)").arg(_inventoryTree->topLevelItemCount()));

    _removeInventoryButton->setEnabled(_inventoryTree->currentItem() != nullptr);

    // Force tree widget to refresh display to ensure icons appear properly
    _inventoryTree->update();
    _inventoryTree->repaint();
    spdlog::debug("SelectionPanel::populateInventoryTree: Completed with {} items, forcing tree refresh", _inventoryTree->topLevelItemCount());
}

QPixmap SelectionPanel::getItemIcon(const MapObject& item) const {
    QPixmap baseIcon = ui::inventory::loadItemIcon(_resources, item.pro_pid, ICON_SIZE, true);
    if (baseIcon.isNull()) {
        baseIcon = createPlaceholderIcon();
    }

    return baseIcon;
}

QPixmap SelectionPanel::createPlaceholderIcon() const {
    spdlog::debug("SelectionPanel::createPlaceholderIcon: Creating {}x{} placeholder icon", ICON_SIZE, ICON_SIZE);
    QPixmap placeholder(ICON_SIZE, ICON_SIZE);
    placeholder.fill(Qt::lightGray);

    QPainter painter(&placeholder);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::darkGray, 2));
    painter.drawRect(1, 1, ICON_SIZE - 3, ICON_SIZE - 3);

    QFont font = painter.font();
    font.setPointSize(ICON_SIZE / 4);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(ui::theme::colors::textDark());
    painter.drawText(placeholder.rect(), Qt::AlignCenter, "?");

    return placeholder;
}

} // namespace geck
