#pragma once

#include <QWidget>

#include <cstdint>

class QTableWidget;
class QLineEdit;

namespace geck {

class Map;
class Lst;
struct MapScript;
namespace resource {
    class GameResources;
}

/// @brief Dockable panel listing every script in the current map.
///
/// Shows the full per-section script list (System / Spatial / Timer / Item /
/// Critter) in a sortable, filterable table. The Map Info panel keeps only a
/// concise counts summary and points the user here for the detail.
class ScriptsPanel : public QWidget {
    Q_OBJECT

public:
    explicit ScriptsPanel(resource::GameResources& resources, QWidget* parent = nullptr);

    /// Repopulate the table from `map`, or clear it when `map` is null.
    void setMap(Map* map);

    /// Select the row for the spatial script with this SID (or clear the selection when it is
    /// MapScript::NONE / not found). Used to mirror a map-side selection. Does not re-emit
    /// spatialScriptSelected, so it is safe to call in response to that signal.
    void selectSpatialScriptRow(uint32_t sid);

signals:
    /// Emitted when the user double-clicks a row owned by a map object. `sid` is the
    /// script's SID (its owning object's `map_scripts_pid`). Ownerless rows (spatial /
    /// timer / system scripts and the map's own header script) emit nothing.
    void scriptObjectActivated(int sid);

    /// A spatial-script row became the current selection (or MapScript::NONE when the current
    /// row is not a spatial script). Drives the shared map/panel selection.
    void spatialScriptSelected(uint32_t sid);
    /// The user asked to edit / delete the spatial script with this SID (context menu or
    /// double-click). Handled by MainWindow, which owns the dialog and command controller.
    void spatialScriptEditRequested(uint32_t sid);
    void spatialScriptDeleteRequested(uint32_t sid);
    /// The user asked to open the SSL source of the script at this 0-based scripts.lst program
    /// index (context menu, any row). Handled by MainWindow via ScriptSourceService.
    void scriptSourceEditRequested(int programIndex);

private slots:
    void applyFilter();                         // hide rows that don't match _filterEdit; re-applied after every populate()
    void onCellDoubleClicked(int row);          // resolve the row's owning SID and emit scriptObjectActivated
    void onScriptSelectionChanged();            // show the selected script's local variables + sync spatial selection
    void onTableContextMenu(const QPoint& pos); // Edit/Delete menu for a spatial-script row

private:
    void populate();
    // Append one fully-built table row. `rowSid` is stashed in the Script ID cell for double-click
    // navigation (MapScript::NONE for ownerless rows); `ownerOid` drives the Owner column.
    void addRow(const QString& section, int programIndex, qulonglong rowSid, qulonglong ownerOid,
        const QString& detail, const Lst* lst);
    // The map's script with this SID (its `pid`), or nullptr (e.g. the ownerless map-header row's NONE).
    const MapScript* scriptByPid(qulonglong sid) const;
    // The SID stashed in row's Script-ID cell, or MapScript::NONE if the row/cell is missing.
    uint32_t sidOfRow(int row) const;
    // The row's 0-based scripts.lst program index (the Script ID cell's display value), or -1.
    int programIndexOfRow(int row) const;

    resource::GameResources& _resources;
    Map* _map = nullptr;

    QLineEdit* _filterEdit;
    QTableWidget* _table;
    QTableWidget* _localVarsTable; // local variables (LVARs) of the currently selected script

    // Guards selectSpatialScriptRow() against re-emitting spatialScriptSelected (feedback loop when
    // the map-side selection drives the panel row and vice versa).
    bool _suppressSpatialSelectionSignal = false;
};

} // namespace geck
