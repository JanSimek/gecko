#include "SelectionPanel.h"
#include "ui/common/InventoryItemUiHelper.h"
#include "ui/dialogs/FrmSelectorDialog.h"
#include "ui/dialogs/ProEditorDialog.h"
#include "ui/dialogs/ObjectFlagsDialog.h"
#include "ui/dialogs/LightPropertiesDialog.h"
#include "ui/dialogs/SceneryDestinationDialog.h"
#include "ui/dialogs/InstancePropertiesDialog.h"
#include "ui/dialogs/CritterPropertiesDialog.h"
#include "ui/dialogs/ScriptSelectorDialog.h"
#include "ui/theme/ThemeManager.h"
#include "ui/UIConstants.h"
#include "resource/ResourcePaths.h"
#include "format/map/MapScript.h"

#include <algorithm>

#include <QFormLayout>
#include <QPixmap>
#include <QApplication>
#include <QTimer>
#include <QPainter>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QSpinBox>
#include <QEnterEvent>
#include <spdlog/spdlog.h>
#include <cmath>

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

const int SelectionPanel::ICON_SIZE = 96; // Larger icons than the separate panel

QSize SelectionPanel::sizeHint() const {
    return QSize(ui::constants::sizes::PANEL_PREFERRED_WIDTH, ui::constants::sizes::PANEL_PREFERRED_HEIGHT);
}

QSize SelectionPanel::minimumSizeHint() const {
    return QSize(ui::constants::sizes::PANEL_MIN_SIZE_WIDTH, ui::constants::sizes::PANEL_MIN_SIZE_HEIGHT);
}

SelectionPanel::SelectionPanel(resource::GameResources& resources, QWidget* parent)
    : QWidget(parent)
    , _mainLayout(nullptr)
    , _scrollArea(nullptr)
    , _contentWidget(nullptr)
    , _contentLayout(nullptr)
    , _stackedWidget(nullptr)
    , _objectPanelWidget(nullptr)
    , _objectInfoGroup(nullptr)
    , _objectSpriteLabel(nullptr)
    , _objectNameEdit(nullptr)
    , _objectTypeEdit(nullptr)
    , _objectMessageIdSpin(nullptr)
    , _objectPositionSpin(nullptr)
    , _objectProtoPidSpin(nullptr)
    , _objectFrmPidSpin(nullptr)
    , _objectFrmPathEdit(nullptr)
    , _changeFrmButton(nullptr)
    , _editProButton(nullptr)
    , _editExitGridButton(nullptr)
    , _editFlagsButton(nullptr)
    , _editLightButton(nullptr)
    , _editDestinationButton(nullptr)
    , _editInteractionButton(nullptr)
    , _editCritterButton(nullptr)
    , _scriptContainer(nullptr)
    , _scriptValueEdit(nullptr)
    , _attachScriptButton(nullptr)
    , _detachScriptButton(nullptr)
    , _inventoryGroup(nullptr)
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
    , _hoverSpriteLabel(nullptr)
    , _resources(resources)
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

    // === Object Panel ===
    _objectPanelWidget = new QWidget();
    QVBoxLayout* objectLayout = new QVBoxLayout(_objectPanelWidget);

    _objectInfoGroup = new QGroupBox("Object Information");

    QHBoxLayout* objectInfoMainLayout = new QHBoxLayout(_objectInfoGroup);

    // Left side: sprite and button container.
    QVBoxLayout* leftSideLayout = new QVBoxLayout();

    _hoverSpriteLabel = new HoverSpriteLabel();
    _hoverSpriteLabel->setText("No object selected");
    _hoverSpriteLabel->setAlignment(Qt::AlignCenter);
    _hoverSpriteLabel->setMinimumHeight(ui::constants::sizes::PREVIEW_MEDIUM);
    _hoverSpriteLabel->setMinimumWidth(ui::constants::sizes::PREVIEW_MEDIUM);
    _hoverSpriteLabel->setMaximumHeight(ui::constants::sizes::PREVIEW_MEDIUM);
    _hoverSpriteLabel->setMaximumWidth(ui::constants::sizes::PREVIEW_MEDIUM);
    _hoverSpriteLabel->setScaledContents(false);
    _hoverSpriteLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    _hoverSpriteLabel->setStyleSheet(ui::theme::styles::previewArea());

    connect(_hoverSpriteLabel->editButton(), &QPushButton::clicked, this, &SelectionPanel::onChangeFrmClicked);

    leftSideLayout->addWidget(_hoverSpriteLabel);

    _editProButton = new QPushButton("Edit PRO...");
    _editProButton->setEnabled(false);
    connect(_editProButton, &QPushButton::clicked, this, &SelectionPanel::onEditProClicked);
    leftSideLayout->addWidget(_editProButton);
    leftSideLayout->addStretch();

    QFormLayout* objectFormLayout = new QFormLayout();

    _objectSpriteLabel = nullptr;

    _objectNameEdit = new QLineEdit();
    _objectNameEdit->setReadOnly(true);
    _objectNameEdit->setPlaceholderText("No object selected");
    objectFormLayout->addRow("Name:", _objectNameEdit);

    _objectTypeEdit = new QLineEdit();
    _objectTypeEdit->setReadOnly(true);
    _objectTypeEdit->setPlaceholderText("No object selected");
    objectFormLayout->addRow("Type:", _objectTypeEdit);

    _objectMessageIdSpin = new QSpinBox();
    _objectMessageIdSpin->setRange(0, INT_MAX);
    _objectMessageIdSpin->setReadOnly(true);
    _objectMessageIdSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    objectFormLayout->addRow("Message ID:", _objectMessageIdSpin);

    _objectPositionSpin = new QSpinBox();
    _objectPositionSpin->setRange(0, INT_MAX);
    _objectPositionSpin->setReadOnly(true);
    _objectPositionSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    objectFormLayout->addRow("Position:", _objectPositionSpin);

    _objectProtoPidSpin = new QSpinBox();
    _objectProtoPidSpin->setRange(0, INT_MAX);
    _objectProtoPidSpin->setReadOnly(true);
    _objectProtoPidSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    objectFormLayout->addRow("Proto PID:", _objectProtoPidSpin);

    _objectFrmPidSpin = new QSpinBox();
    _objectFrmPidSpin->setRange(0, INT_MAX);
    _objectFrmPidSpin->setReadOnly(true);
    _objectFrmPidSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    objectFormLayout->addRow("FRM PID:", _objectFrmPidSpin);

    _objectFrmPathEdit = new QLineEdit();
    _objectFrmPathEdit->setReadOnly(true);
    _objectFrmPathEdit->setPlaceholderText("FRM path");
    objectFormLayout->addRow("FRM Path:", _objectFrmPathEdit);

    _changeFrmButton = nullptr;

    _editExitGridButton = new QPushButton("Edit Exit Grid...");
    _editExitGridButton->setEnabled(false);
    _editExitGridButton->setVisible(false); // Only shown for exit-grid marker objects
    connect(_editExitGridButton, &QPushButton::clicked, this, &SelectionPanel::onEditExitGridClicked);
    objectFormLayout->addRow("", _editExitGridButton);

    // Per-instance editors. Visibility is decided per object type in updateObjectInfo().
    _editFlagsButton = new QPushButton("Edit Flags...");
    _editFlagsButton->setVisible(false);
    connect(_editFlagsButton, &QPushButton::clicked, this, &SelectionPanel::onEditFlagsClicked);
    objectFormLayout->addRow("", _editFlagsButton);

    _editLightButton = new QPushButton("Edit Light...");
    _editLightButton->setVisible(false);
    connect(_editLightButton, &QPushButton::clicked, this, &SelectionPanel::onEditLightClicked);
    objectFormLayout->addRow("", _editLightButton);

    _editDestinationButton = new QPushButton("Edit Destination...");
    _editDestinationButton->setVisible(false);
    connect(_editDestinationButton, &QPushButton::clicked, this, &SelectionPanel::onEditDestinationClicked);
    objectFormLayout->addRow("", _editDestinationButton);

    _editInteractionButton = new QPushButton("Edit Interaction...");
    _editInteractionButton->setVisible(false);
    connect(_editInteractionButton, &QPushButton::clicked, this, &SelectionPanel::onEditInteractionClicked);
    objectFormLayout->addRow("", _editInteractionButton);

    _editCritterButton = new QPushButton("Edit Critter...");
    _editCritterButton->setVisible(false);
    connect(_editCritterButton, &QPushButton::clicked, this, &SelectionPanel::onEditCritterClicked);
    objectFormLayout->addRow("", _editCritterButton);

    // Script attachment controls (spanning row, shown for scriptable objects).
    _scriptContainer = new QWidget();
    QVBoxLayout* scriptLayout = new QVBoxLayout(_scriptContainer);
    scriptLayout->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout* scriptValueRow = new QHBoxLayout();
    scriptValueRow->addWidget(new QLabel("Script:"));
    _scriptValueEdit = new QLineEdit();
    _scriptValueEdit->setReadOnly(true);
    _scriptValueEdit->setPlaceholderText("None");
    scriptValueRow->addWidget(_scriptValueEdit, 1);
    scriptLayout->addLayout(scriptValueRow);
    QHBoxLayout* scriptButtonRow = new QHBoxLayout();
    _attachScriptButton = new QPushButton("Attach Script...");
    _detachScriptButton = new QPushButton("Detach");
    _detachScriptButton->setEnabled(false);
    connect(_attachScriptButton, &QPushButton::clicked, this, &SelectionPanel::onAttachScriptClicked);
    connect(_detachScriptButton, &QPushButton::clicked, this, &SelectionPanel::onDetachScriptClicked);
    scriptButtonRow->addWidget(_attachScriptButton);
    scriptButtonRow->addWidget(_detachScriptButton);
    scriptLayout->addLayout(scriptButtonRow);
    _scriptContainer->setVisible(false);
    objectFormLayout->addRow(_scriptContainer);

    objectInfoMainLayout->addLayout(leftSideLayout);
    objectInfoMainLayout->addLayout(objectFormLayout, 1);

    objectLayout->addWidget(_objectInfoGroup);

    setupInventorySection();
    objectLayout->addWidget(_inventoryGroup);

    objectLayout->addStretch();

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

void SelectionPanel::updateObjectInfo() {
    if (!_selectedObject || !_selectedObject.value()) {
        clearObjectInfo();
        return;
    }

    try {
        auto& selectedMapObject = _selectedObject.value()->getMapObject();
        int32_t PID = selectedMapObject.pro_pid;

        auto pro = _resources.repository().load<Pro>(ProHelper::basePath(_resources, PID));

        if (pro) {
            auto msg = ProHelper::msgFile(_resources, pro->type());
            std::string objectName = "Unknown";
            if (msg) {
                try {
                    objectName = msg->message(pro->header.message_id).text;
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to get message for ID {}: {}", pro->header.message_id, e.what());
                }
            }

            _objectNameEdit->setText(QString::fromStdString(objectName));
            _objectTypeEdit->setText(QString::fromStdString(pro->typeToString()));
            _objectMessageIdSpin->setValue(static_cast<int>(pro->header.message_id));
            _objectPositionSpin->setValue(selectedMapObject.position);
            _objectProtoPidSpin->setValue(static_cast<int>(selectedMapObject.pro_pid));

            _objectFrmPidSpin->setValue(static_cast<int>(selectedMapObject.frm_pid));

            // frm_pid == 0 means the object uses the prototype's FID.
            uint32_t activeFrmPid = selectedMapObject.frm_pid != 0 ? selectedMapObject.frm_pid : pro->header.FID;
            std::string frmPath = _resources.frmResolver().resolve(activeFrmPid);
            _objectFrmPathEdit->setText(QString::fromStdString(frmPath));

            _hoverSpriteLabel->editButton()->setEnabled(true);
            _editProButton->setEnabled(true);

            if (selectedMapObject.isExitGridMarker()) {
                _editExitGridButton->setVisible(true);
                _editExitGridButton->setEnabled(true);
            } else {
                _editExitGridButton->setVisible(false);
                _editExitGridButton->setEnabled(false);
            }

            // Per-instance editors, gated by object type.
            const auto objectType = pro->type();
            const bool isScenery = objectType == Pro::OBJECT_TYPE::SCENERY;
            Pro::SCENERY_TYPE sceneryType = Pro::SCENERY_TYPE::GENERIC;
            if (isScenery) {
                sceneryType = static_cast<Pro::SCENERY_TYPE>(pro->objectSubtypeId());
            }
            const bool hasDestination = isScenery
                && (sceneryType == Pro::SCENERY_TYPE::STAIRS
                    || sceneryType == Pro::SCENERY_TYPE::LADDER_TOP
                    || sceneryType == Pro::SCENERY_TYPE::LADDER_BOTTOM
                    || sceneryType == Pro::SCENERY_TYPE::ELEVATOR);
            const bool isDoor = isScenery && sceneryType == Pro::SCENERY_TYPE::DOOR;
            const bool isContainer = objectType == Pro::OBJECT_TYPE::ITEM
                && pro->itemType() == Pro::ITEM_TYPE::CONTAINER;

            // Flags and light apply to every real object (exit-grid markers use
            // their own editor, handled above).
            _editFlagsButton->setVisible(true);
            _editLightButton->setVisible(true);
            _editDestinationButton->setVisible(hasDestination);
            _editInteractionButton->setVisible(isDoor || isContainer);
            _editCritterButton->setVisible(objectType == Pro::OBJECT_TYPE::CRITTER);

            // Scripts can be attached to items, critters, scenery and walls
            // (engine mapper instance editors). Tiles/misc markers cannot.
            const bool scriptable = objectType == Pro::OBJECT_TYPE::ITEM
                || objectType == Pro::OBJECT_TYPE::CRITTER
                || objectType == Pro::OBJECT_TYPE::SCENERY
                || objectType == Pro::OBJECT_TYPE::WALL;
            _scriptContainer->setVisible(scriptable);
            if (scriptable) {
                updateScriptSection();
            }

            // Convert the SFML sprite to a QPixmap for display.
            const auto& sprite = _selectedObject.value()->getSprite();
            const auto& texture = sprite.getTexture();

            auto image = texture.copyToImage();
            auto textureRect = sprite.getTextureRect();

            const std::uint8_t* pixels = image.getPixelsPtr();
            QImage qImage(pixels, image.getSize().x, image.getSize().y, QImage::Format_RGBA8888);

            // Crop to the sprite's current frame rectangle.
            if (textureRect.size.x > 0 && textureRect.size.y > 0) {
                qImage = qImage.copy(textureRect.position.x, textureRect.position.y, textureRect.size.x, textureRect.size.y);
            }

            QPixmap pixmap = QPixmap::fromImage(qImage);
            if (!pixmap.isNull()) {
                QSize maxSize(128, 128);

                if (pixmap.width() > maxSize.width() || pixmap.height() > maxSize.height()) {
                    pixmap = pixmap.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }

                _hoverSpriteLabel->setPixmap(pixmap);
                _hoverSpriteLabel->setText("");
            } else {
                _hoverSpriteLabel->setText("Failed to convert sprite");
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
    _objectNameEdit->clear();
    _objectNameEdit->setPlaceholderText("No object selected");

    _objectTypeEdit->clear();
    _objectTypeEdit->setPlaceholderText("No object selected");

    _objectMessageIdSpin->setValue(0);
    _objectPositionSpin->setValue(0);
    _objectProtoPidSpin->setValue(0);
    _objectFrmPidSpin->setValue(0);

    _objectFrmPathEdit->clear();
    _objectFrmPathEdit->setPlaceholderText("FRM path");

    _hoverSpriteLabel->editButton()->setEnabled(false);
    _editProButton->setEnabled(false);
    _editExitGridButton->setEnabled(false);
    _editExitGridButton->setVisible(false);
    _editFlagsButton->setVisible(false);
    _editLightButton->setVisible(false);
    _editDestinationButton->setVisible(false);
    _editInteractionButton->setVisible(false);
    _editCritterButton->setVisible(false);
    _scriptContainer->setVisible(false);

    _hoverSpriteLabel->clear();
    _hoverSpriteLabel->setText("No object selected");
    _objectInfoGroup->setTitle("Object Information");

    _inventoryGroup->setVisible(false);
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
        spdlog::info("SelectionPanel: Filtering FRM dialog by object type: {}", static_cast<int>(*objectTypeFilter));
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

                // Custom FID: 0xFF in the high byte of the baseId (not from an LST).
                bool isCustomFid = ((derivedFrmPid & 0x00FF0000) == 0x00FF0000);

                if (derivedFrmPid != currentFrmPid) {
                    if (isCustomFid) {
                        spdlog::warn("SelectionPanel: Using custom FID 0x{:08X} for non-LST FRM - may not work in game", derivedFrmPid);

                        // Update the visual but keep the original frm_pid for game compatibility:
                        // the editor shows the new FRM while the saved map stays loadable in-game.
                        spdlog::info("SelectionPanel: Keeping original frm_pid {} for game compatibility, visual uses custom FRM", currentFrmPid);

                        Q_EMIT statusMessage(QString("Warning: Custom FRM may not display correctly in game"));
                    } else {
                        // Valid LST-based FID, safe to persist.
                        mapObject.frm_pid = derivedFrmPid;
                        spdlog::info("SelectionPanel: Updated MapObject frm_pid from {} to {} for persistent save",
                            currentFrmPid, derivedFrmPid);
                    }

                    updateObjectInfo();
                } else {
                    spdlog::debug("SelectionPanel: FRM PID unchanged ({}), no MapObject update needed", derivedFrmPid);
                    _objectFrmPathEdit->setText(QString::fromStdString(newFrmPath));
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
                if ((currentFrmPid >> 24) == 1) { // FID type field 1 == critter
                    spdlog::warn("SelectionPanel: FRM '{}' not found in critters.lst - change may not persist in game", filename);
                    spdlog::info("SelectionPanel: Keeping original FRM PID ({}) for game compatibility", currentFrmPid);

                    Q_EMIT statusMessage(QString("Warning: FRM '%1' may not display correctly in game - not found in critters.lst")
                            .arg(QString::fromStdString(filename)));
                }

                // Visual-only update for the derivation-failed path; frm_pid is left untouched.
                Q_EMIT objectFrmPathChanged(_selectedObject.value(), newFrmPath);

                _objectFrmPathEdit->setText(QString::fromStdString(newFrmPath));
                updateObjectInfo();
            }

            spdlog::info("SelectionPanel: Changed object FRM visual to path: {}", newFrmPath);

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

    const uint32_t objectType = mapObject->objectType();
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

    Pro::SCENERY_TYPE sceneryType = Pro::SCENERY_TYPE::GENERIC;
    try {
        auto pro = _resources.repository().load<Pro>(ProHelper::basePath(_resources, mapObject->pro_pid));
        if (!pro || pro->type() != Pro::OBJECT_TYPE::SCENERY) {
            return;
        }
        sceneryType = static_cast<Pro::SCENERY_TYPE>(pro->objectSubtypeId());
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
        auto pro = _resources.repository().load<Pro>(ProHelper::basePath(_resources, mapObject->pro_pid));
        if (!pro) {
            return;
        }
        isDoor = pro->type() == Pro::OBJECT_TYPE::SCENERY
            && static_cast<Pro::SCENERY_TYPE>(pro->objectSubtypeId()) == Pro::SCENERY_TYPE::DOOR;
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

    CritterPropertiesDialog dialog(mapObject->ai_packet, mapObject->group_id,
        mapObject->current_hp, mapObject->current_rad, mapObject->current_poison, this);
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
    if (!_scriptContainer) {
        return;
    }
    if (!_selectedObject || !_selectedObject.value() || !_map) {
        _scriptValueEdit->clear();
        _attachScriptButton->setEnabled(false);
        _detachScriptButton->setEnabled(false);
        return;
    }

    auto mapObject = _selectedObject.value()->getMapObjectPtr();
    const bool attached = mapObject && mapObject->map_scripts_pid != -1;

    QString text;
    if (attached) {
        const uint32_t sid = static_cast<uint32_t>(mapObject->map_scripts_pid);
        const int section = MapScript::sidSection(sid);
        if (section >= 0 && section < Map::SCRIPT_SECTIONS) {
            for (const auto& s : _map->getMapFile().map_scripts[section]) {
                if (s.pid == sid) {
                    auto* lst = _resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS);
                    if (lst && s.script_id < lst->list().size()) {
                        text = QString::fromStdString(lst->list().at(s.script_id));
                    } else {
                        text = QString("Script #%1").arg(s.script_id);
                    }
                    break;
                }
            }
        }
    }

    _scriptValueEdit->setText(text);
    _attachScriptButton->setEnabled(true);
    _detachScriptButton->setEnabled(attached);
}

void SelectionPanel::onAttachScriptClicked() {
    if (!_selectedObject || !_selectedObject.value() || !_map) {
        return;
    }
    auto mapObject = _selectedObject.value()->getMapObjectPtr();
    if (!mapObject) {
        return;
    }

    auto* scriptsLst = _resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS);
    if (!scriptsLst) {
        QMessageBox::warning(this, "Attach Script", "Could not load scripts.lst.");
        return;
    }

    ScriptSelectorDialog dialog(scriptsLst->list(), -1, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const int programIndex = dialog.selectedIndex();
    if (programIndex < 0) {
        return;
    }

    const uint32_t objectType = mapObject->objectType();
    // Critters use the CRITTER section; items/scenery/walls use the ITEM section.
    const int scriptType = (objectType == static_cast<uint32_t>(Pro::OBJECT_TYPE::CRITTER))
        ? static_cast<int>(MapScript::ScriptType::CRITTER)
        : static_cast<int>(MapScript::ScriptType::ITEM);

    // Direct (synchronous) connection: the model is mutated before this returns,
    // so updateScriptSection() reads the new state.
    Q_EMIT requestAttachScript(mapObject, scriptType, static_cast<uint32_t>(programIndex));
    updateScriptSection();
    Q_EMIT statusMessage(QString("Attached script %1 to object").arg(programIndex));
}

void SelectionPanel::onDetachScriptClicked() {
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

    bool ok = false;
    const int index = QInputDialog::getInt(this, "Add Inventory Item",
        "Item proto index:", 1, 1, 0x00FFFFFF, 1, &ok);
    if (!ok) {
        return;
    }

    const uint32_t itemPid = Pro::makePid(Pro::OBJECT_TYPE::ITEM, static_cast<uint32_t>(index));

    Pro* pro = nullptr;
    try {
        pro = _resources.repository().load<Pro>(ProHelper::basePath(_resources, itemPid));
    } catch (const std::exception& e) {
        spdlog::warn("onAddInventoryClicked: failed to load proto for pid {}: {}", itemPid, e.what());
    }
    if (!pro) {
        QMessageBox::warning(this, "Add Inventory Item",
            QString("No item prototype found for index %1.").arg(index));
        return;
    }

    auto before = ObjectCommandController::cloneInventory(holder->inventory);

    auto item = std::make_unique<MapObject>();
    item->pro_pid = itemPid;
    item->frm_pid = pro->header.FID;
    item->amount = 1;
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

    // Refresh the icon to reflect the new quantity.
    uint32_t pid = item->data(COLUMN_ICON, Qt::UserRole).toUInt();
    QPixmap iconWithQuantity = ui::inventory::loadItemIcon(_resources, pid, ICON_SIZE, true);
    if (iconWithQuantity.isNull()) {
        iconWithQuantity = createPlaceholderIcon();
    }
    QIcon icon;
    icon.addPixmap(iconWithQuantity, QIcon::Normal, QIcon::Off);
    item->setIcon(COLUMN_ICON, icon);
    item->setSizeHint(COLUMN_ICON, QSize(ICON_SIZE, ICON_SIZE));

    auto after = ObjectCommandController::cloneInventory(holder->inventory);
    Q_EMIT requestInventoryEdit(_selectedObject.value()->getMapObjectPtr(),
        std::move(before), std::move(after));
}

void SelectionPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    bool shouldUseHorizontal = (width() >= HORIZONTAL_LAYOUT_MIN_WIDTH && _inventoryGroup && _inventoryGroup->isVisible());

    if (shouldUseHorizontal != _isHorizontalLayout) {
        switchLayout(shouldUseHorizontal);
    }
}

void SelectionPanel::switchLayout(bool horizontal) {
    if (!_objectInfoGroup || !_inventoryGroup) {
        return;
    }

    if (horizontal) {
        applyHorizontalLayout();
    } else {
        applyVerticalLayout();
    }

    _isHorizontalLayout = horizontal;
}

void SelectionPanel::applyHorizontalLayout() {
    QWidget* newContainer = new QWidget();
    QHBoxLayout* hLayout = new QHBoxLayout(newContainer);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(ui::constants::SPACING_WIDE);

    QWidget* leftSide = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftSide);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(_objectInfoGroup);
    leftLayout->addStretch();

    // 50/50 split between object info and inventory.
    hLayout->addWidget(leftSide, 1);
    hLayout->addWidget(_inventoryGroup, 1);

    QLayout* oldLayout = _objectPanelWidget->layout();
    if (oldLayout) {
        // Detach widgets before deleting the layout so they survive.
        oldLayout->removeWidget(_objectInfoGroup);
        oldLayout->removeWidget(_inventoryGroup);
        delete oldLayout;
    }

    QVBoxLayout* wrapperLayout = new QVBoxLayout(_objectPanelWidget);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->addWidget(newContainer);
}

void SelectionPanel::applyVerticalLayout() {
    if (_objectPanelWidget->layout()) {
        QLayout* currentLayout = _objectPanelWidget->layout();

        // Detach widgets before deleting the layout so they survive.
        for (int i = currentLayout->count() - 1; i >= 0; --i) {
            QLayoutItem* item = currentLayout->itemAt(i);
            if (item && item->widget()) {
                item->widget()->setParent(nullptr);
            }
        }

        delete currentLayout;
    }

    QVBoxLayout* vLayout = new QVBoxLayout(_objectPanelWidget);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->addWidget(_objectInfoGroup);
    vLayout->addWidget(_inventoryGroup);
    vLayout->addStretch();
}

void SelectionPanel::setupInventorySection() {
    _inventoryGroup = new QGroupBox("Inventory");
    _inventoryGroup->setVisible(false);

    QVBoxLayout* inventoryLayout = new QVBoxLayout(_inventoryGroup);

    _inventoryTree = new QTreeWidget();
    _inventoryTree->setHeaderLabels({ "", "Name", "Type", "Amount" });
    _inventoryTree->setColumnWidth(COLUMN_ICON, ICON_SIZE + 20);
    _inventoryTree->setColumnWidth(COLUMN_NAME, ui::constants::column_widths::NAME_SHORT);
    _inventoryTree->setColumnWidth(COLUMN_TYPE, ui::constants::column_widths::TYPE);
    _inventoryTree->setColumnWidth(COLUMN_AMOUNT, ui::constants::column_widths::AMOUNT_WIDE);
    _inventoryTree->setRootIsDecorated(false);
    _inventoryTree->setAlternatingRowColors(true);
    _inventoryTree->setSelectionMode(QAbstractItemView::SingleSelection);
    _inventoryTree->setMinimumHeight(ui::constants::sizes::PANEL_MIN_HEIGHT);
    _inventoryTree->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
    _inventoryTree->setUniformRowHeights(true);

    _inventoryTree->setItemDelegateForColumn(COLUMN_AMOUNT, _amountDelegate);

    _emptyInventoryLabel = new QLabel("No inventory items");
    _emptyInventoryLabel->setAlignment(Qt::AlignCenter);
    _emptyInventoryLabel->setStyleSheet(ui::theme::styles::smallLabel());

    _inventoryViewStack = new QStackedWidget();
    _inventoryViewStack->addWidget(_inventoryTree);
    _inventoryViewStack->addWidget(_emptyInventoryLabel);

    connect(_inventoryTree, &QTreeWidget::itemChanged, this, &SelectionPanel::onInventoryItemChanged);

    inventoryLayout->addWidget(_inventoryViewStack);

    QHBoxLayout* buttonLayout = new QHBoxLayout();

    _addInventoryButton = new QPushButton("Add Item");
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
}

void SelectionPanel::updateInventorySection() {
    bool wasVisible = _inventoryGroup->isVisible();

    if (!_selectedObject || !_selectedObject.value()) {
        _inventoryGroup->setVisible(false);
        // Re-run layout when inventory visibility changes.
        if (wasVisible) {
            resizeEvent(nullptr);
        }
        return;
    }

    auto object = _selectedObject.value();
    auto mapObject = object->getMapObjectPtr();

    if (!mapObject) {
        _inventoryGroup->setVisible(false);
        if (wasVisible) {
            resizeEvent(nullptr);
        }
        return;
    }

    // Only containers and critters can hold inventory.
    try {
        auto pro = _resources.repository().load<Pro>(ProHelper::basePath(_resources, mapObject->pro_pid));
        if (pro) {
            bool hasInventory = (pro->type() == Pro::OBJECT_TYPE::ITEM && pro->itemType() == Pro::ITEM_TYPE::CONTAINER) || pro->type() == Pro::OBJECT_TYPE::CRITTER;

            _inventoryGroup->setVisible(hasInventory);

            if (wasVisible != hasInventory) {
                resizeEvent(nullptr);
            }

            if (hasInventory) {
                populateInventoryTree();
            }
        } else {
            _inventoryGroup->setVisible(false);
            if (wasVisible) {
                resizeEvent(nullptr);
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load pro file for inventory check: {}", e.what());
        _inventoryGroup->setVisible(false);
        if (wasVisible) {
            resizeEvent(nullptr);
        }
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
    options.iconProvider = [this](const MapObject& item) { return getItemIconWithQuantity(item); };
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

    _removeInventoryButton->setEnabled(_inventoryTree->currentItem() != nullptr);

    // Force tree widget to refresh display to ensure icons appear properly
    _inventoryTree->update();
    _inventoryTree->repaint();
    spdlog::debug("SelectionPanel::populateInventoryTree: Completed with {} items, forcing tree refresh", _inventoryTree->topLevelItemCount());
}

QPixmap SelectionPanel::getItemIconWithQuantity(const MapObject& item) const {
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
