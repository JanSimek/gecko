#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStyledItemDelegate>
#include <QResizeEvent>
#include <QEnterEvent>
#include <memory>

#include "../../editor/Object.h"
#include "../../format/map/Tile.h"
#include "../../selection/SelectionState.h"

namespace geck {

class Map;
namespace resource {
class GameResources;
}

// Custom hover-enabled sprite label for FRM previews
class HoverSpriteLabel : public QLabel {
    Q_OBJECT
public:
    HoverSpriteLabel(QWidget* parent = nullptr);
    QPushButton* editButton() const { return _editButton; }

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupEditButton();
    void positionEditButton();
    QPushButton* _editButton = nullptr;
};

class SelectionPanel : public QWidget {
    Q_OBJECT

public:
    explicit SelectionPanel(resource::GameResources& resources, QWidget* parent = nullptr);

    void setMap(Map* map);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;

signals:
    void objectFrmChanged(std::shared_ptr<Object> object, uint32_t newFrmPid);
    void objectFrmPathChanged(std::shared_ptr<Object> object, const std::string& newFrmPath);
    void requestObjectHighlight(std::shared_ptr<Object> object);
    void statusMessage(const QString& message);
    void requestProEditor(std::shared_ptr<Object> object);
    void requestExitGridEditor(std::shared_ptr<Object> object);

public slots:
    void selectObject(std::shared_ptr<Object> selectedObject);
    void selectTile(int tileIndex, int elevation, bool isRoof);
    void clearSelection();
    void handleSelectionChanged(const selection::SelectionState& selection, int elevation);

private slots:
    void onChangeFrmClicked();
    void onEditProClicked();
    void onEditExitGridClicked();
    void onAddInventoryClicked();
    void onRemoveInventoryClicked();
    void onInventoryItemChanged(QTreeWidgetItem* item, int column);

private:
    void setupUI();
    void updateObjectInfo();
    void updateTileInfo();
    void clearObjectInfo();
    void clearTileInfo();
    void loadTilePreview(class Lst* tilesList, uint16_t tileId);
    void showObjectPanel();
    void showTilePanel();

    // Inventory methods
    void setupInventorySection();
    void updateInventorySection();
    void populateInventoryTree();
    QPixmap getItemIconWithQuantity(const MapObject& item) const;
    QPixmap createPlaceholderIcon() const;

    // Layout management
    void switchLayout(bool horizontal);
    void applyHorizontalLayout();
    void applyVerticalLayout();

    QVBoxLayout* _mainLayout;
    QScrollArea* _scrollArea;
    QWidget* _contentWidget;
    QVBoxLayout* _contentLayout;

    // Stacked widget to switch between object and tile panels
    QStackedWidget* _stackedWidget;

    // Object panel widgets
    QWidget* _objectPanelWidget;
    QGroupBox* _objectInfoGroup;
    QLabel* _objectSpriteLabel;
    QLineEdit* _objectNameEdit;
    QLineEdit* _objectTypeEdit;
    QSpinBox* _objectMessageIdSpin;
    QSpinBox* _objectPositionSpin;
    QSpinBox* _objectProtoPidSpin;
    QSpinBox* _objectFrmPidSpin;
    QLineEdit* _objectFrmPathEdit;
    QPushButton* _changeFrmButton;
    QPushButton* _editProButton;
    QPushButton* _editExitGridButton;

    // Inventory section (appears when object has inventory)
    QGroupBox* _inventoryGroup;
    QStackedWidget* _inventoryViewStack;
    QTreeWidget* _inventoryTree;
    QLabel* _emptyInventoryLabel;
    QPushButton* _addInventoryButton;
    QPushButton* _removeInventoryButton;

    // Tile panel widgets
    QWidget* _tilePanelWidget;
    QGroupBox* _tileInfoGroup;
    QLabel* _tilePreviewLabel;
    QSpinBox* _tileIndexSpin;
    QSpinBox* _elevationSpin;
    QSpinBox* _hexXSpin;
    QSpinBox* _hexYSpin;
    QSpinBox* _worldXSpin;
    QSpinBox* _worldYSpin;
    QLineEdit* _tileTypeEdit;
    QSpinBox* _tileIdSpin;
    QLineEdit* _tileNameEdit;

    // Inventory tree columns
    enum InventoryColumns {
        COLUMN_ICON = 0,
        COLUMN_NAME = 1,
        COLUMN_TYPE = 2,
        COLUMN_AMOUNT = 3,
        COLUMN_COUNT
    };

    // Visual styling constants
    static const int ICON_SIZE;

    // Custom delegate for editable amount column
    class AmountDelegate;
    AmountDelegate* _amountDelegate;

    // Hover sprite label instance
    HoverSpriteLabel* _hoverSpriteLabel;

    // Current selection state
    resource::GameResources& _resources;
    std::optional<std::shared_ptr<Object>> _selectedObject;
    int _selectedTileIndex;
    int _selectedElevation;
    bool _isRoofSelected;
    bool _hasTileSelection;
    Map* _map;

    // Layout management
    static constexpr int HORIZONTAL_LAYOUT_MIN_WIDTH = 650;
    bool _isHorizontalLayout = false;
};

} // namespace geck
