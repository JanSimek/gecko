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
#include <QToolButton>
#include <functional>
#include <memory>

#include "editor/Object.h"
#include "format/map/Tile.h"
#include "selection/SelectionState.h"
#include "editing/commands/ObjectCommandController.h"
#include "ui/ScriptSourceService.h"

namespace geck {

class CollapsibleSection;
class ElidedLabel;
class Map;
class ObjectScriptSection;
class Settings;
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

/// The Selection dock: what is selected and its editors. Objects get one column that fits the default
/// dock width - a header (sprite, name, where, ids, edit actions) over collapsible Properties, Script
/// and Inventory sections; tiles get their own page.
class SelectionPanel : public QWidget {
    Q_OBJECT

public:
    /// `settings` remembers which sections are collapsed; without it they all start expanded.
    explicit SelectionPanel(resource::GameResources& resources, std::shared_ptr<Settings> settings = nullptr,
        QWidget* parent = nullptr);

    void setMap(Map* map);

    /// How the script section learns whether a script is compiled and what its source overrides
    /// (MainWindow passes ScriptSourceService::scriptStatus).
    void setScriptStatusProvider(std::function<ScriptSourceService::ScriptStatus(int)> provider);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// @brief Opens the PRO editor dialog for the currently selected object.
    /// @return true if a PRO editor was opened, false otherwise.
    bool openProEditorForSelectedObject();

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
    /// Open the SSL source of the attached script (its 0-based scripts.lst program index).
    /// Handled by MainWindow via ScriptSourceService, exactly like the map-script "Edit Source".
    void requestEditScriptSource(int programIndex);
    /// Raise the Scripts panel on the row of the script with this SID.
    void requestShowScriptInPanel(uint32_t sid);

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
    void onAttachScript(int programIndex);
    void onDetachScript();
    void onAddInventoryClicked();
    void onRemoveInventoryClicked();
    void onInventoryItemChanged(QTreeWidgetItem* item, int column);

private:
    void setupUI();
    QWidget* createObjectPage();
    CollapsibleSection* createSection(const QString& key, const QString& title);
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
    /// The selected object's MapObject (the inventory holder), or nullptr if
    /// nothing is selected.
    MapObject* selectedInventoryHolder() const;
    /// Captures the after-snapshot, refreshes the tree, and emits an undoable
    /// inventory edit. `before` is the snapshot taken before the mutation.
    void commitInventoryEdit(std::vector<std::shared_ptr<MapObject>> before);

    // Script attachment: refreshes the script section for the selected object.
    void updateScriptSection();
    QPixmap getItemIcon(const MapObject& item) const;
    QPixmap createPlaceholderIcon() const;

    QVBoxLayout* _mainLayout;
    QScrollArea* _scrollArea;
    QWidget* _contentWidget;
    QVBoxLayout* _contentLayout;

    // Stacked widget to switch between object and tile panels
    QStackedWidget* _stackedWidget;

    // Object page: header
    QWidget* _objectPanelWidget;
    HoverSpriteLabel* _hoverSpriteLabel;
    ElidedLabel* _objectNameLabel;
    ElidedLabel* _objectSummaryLabel; // type, hex (col, row), elevation
    ElidedLabel* _objectIdsLabel;     // PID and FID in hex
    QPushButton* _editProButton;
    QPushButton* _editMenuButton; // "Edit ▾": the per-object editors that apply
    QAction* _editExitGridAction;
    QAction* _editFlagsAction;
    QAction* _editLightAction;
    QAction* _editDestinationAction;
    QAction* _editInteractionAction;
    QAction* _editCritterAction;

    // Object page: sections
    CollapsibleSection* _propertiesSection;
    ElidedLabel* _messageIdLabel;
    ElidedLabel* _facingLabel;
    ElidedLabel* _lightLabel;
    ElidedLabel* _artPathLabel;
    CollapsibleSection* _scriptSection;
    ObjectScriptSection* _scriptView;
    CollapsibleSection* _inventorySection;
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

    // Inventory icon size: a row, not a thumbnail - the name and type carry the detail.
    static constexpr int ICON_SIZE = 32;

    // Custom delegate for editable amount column
    class AmountDelegate;
    AmountDelegate* _amountDelegate;

    // Current selection state
    resource::GameResources& _resources;
    std::shared_ptr<Settings> _settings;
    std::optional<std::shared_ptr<Object>> _selectedObject;
    int _selectedTileIndex;
    int _selectedElevation;
    bool _isRoofSelected;
    bool _hasTileSelection;
    Map* _map;
};

} // namespace geck
