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

#include "editor/Object.h"
#include "format/map/Tile.h"
#include "selection/SelectionState.h"
#include "ui/editing/ObjectCommandController.h"

namespace geck {

class Map;
namespace resource {
    class GameResources;
}

/// @brief Hover-enabled sprite label for FRM previews (shows an edit button on hover).
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

    /// @brief Opens the PRO editor dialog for the currently selected object.
    /// @return true if a PRO editor was opened, false otherwise.
    bool openProEditorForSelectedObject();

protected:
    void resizeEvent(QResizeEvent* event) override;

signals:
    void objectFrmChanged(std::shared_ptr<Object> object, uint32_t newFrmPid);
    void objectFrmPathChanged(std::shared_ptr<Object> object, const std::string& newFrmPath);
    void requestObjectHighlight(std::shared_ptr<Object> object);
    void statusMessage(const QString& message);
    void requestExitGridEditor(std::shared_ptr<Object> object);

    /// Emitted when an in-panel instance editor (flags, light, scenery
    /// destination, interaction) produces a before/after change. MainWindow
    /// forwards it to the active EditorWidget so it is recorded as one undoable
    /// command.
    void requestInstanceEdit(std::shared_ptr<Object> object,
        MapObjectInstanceState before,
        MapObjectInstanceState after,
        QString description);

    /// Emitted after an inventory edit so it is recorded as one undoable command.
    void requestInventoryEdit(std::shared_ptr<MapObject> container,
        std::vector<std::shared_ptr<MapObject>> before,
        std::vector<std::shared_ptr<MapObject>> after);

    /// Script attach/detach, routed to the editor's ObjectCommandController so
    /// they are undoable.
    void requestAttachScript(std::shared_ptr<MapObject> object, int scriptType, uint32_t programIndex);
    void requestDetachScript(std::shared_ptr<MapObject> object);

public slots:
    void selectObject(std::shared_ptr<Object> selectedObject);
    void selectTile(int tileIndex, int elevation, bool isRoof);
    void clearSelection();
    void handleSelectionChanged(const selection::SelectionState& selection, int elevation);
    /// Re-reads the selected object's fields (e.g. after an undo/redo).
    void refresh();

private slots:
    void onChangeFrmClicked();
    void onEditProClicked();
    void onEditExitGridClicked();
    void onEditFlagsClicked();
    void onEditLightClicked();
    void onEditDestinationClicked();
    void onEditInteractionClicked();
    void onEditCritterClicked();
    void onAttachScriptClicked();
    void onDetachScriptClicked();
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
    /// The selected object's MapObject if it can hold inventory, else nullptr.
    MapObject* selectedInventoryHolder() const;
    /// Captures the after-snapshot, refreshes the tree, and emits an undoable
    /// inventory edit. `before` is the snapshot taken before the mutation.
    void commitInventoryEdit(std::vector<std::shared_ptr<MapObject>> before);

    // Script attachment: refreshes the displayed script name/buttons.
    void updateScriptSection();
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
    QPushButton* _editFlagsButton;
    QPushButton* _editLightButton;
    QPushButton* _editDestinationButton;
    QPushButton* _editInteractionButton;
    QPushButton* _editCritterButton;

    // Script attachment controls (shown for scriptable object types)
    QWidget* _scriptContainer;
    QLineEdit* _scriptValueEdit;
    QPushButton* _attachScriptButton;
    QPushButton* _detachScriptButton;

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
