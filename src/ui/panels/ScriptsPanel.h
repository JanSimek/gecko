#pragma once

#include <QWidget>

class QTableWidget;
class QTableWidgetItem;
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

signals:
    /// Emitted when the user double-clicks a row owned by a map object. `sid` is the
    /// script's SID (its owning object's `map_scripts_pid`). Ownerless rows (spatial /
    /// timer / system scripts and the map's own header script) emit nothing.
    void scriptObjectActivated(int sid);

    /// Emitted when the user edits a local variable's value -> mark the map modified.
    void mapVariablesChanged();

private slots:
    void applyFilter();                             // hide rows that don't match _filterEdit; re-applied after every populate()
    void onCellDoubleClicked(int row);              // resolve the row's owning SID and emit scriptObjectActivated
    void onScriptSelectionChanged();                // show the selected script's local variables
    void onLocalVarChanged(QTableWidgetItem* item); // write an edited LVAR value back to map_local_vars

private:
    void populate();
    // Append one fully-built table row. `rowSid` is stashed in the Script ID cell for double-click
    // navigation (MapScript::NONE for ownerless rows); `ownerOid` drives the Owner column.
    void addRow(const QString& section, int programIndex, qulonglong rowSid, qulonglong ownerOid,
        const QString& detail, const Lst* lst);
    // The map's script with this SID (its `pid`), or nullptr (e.g. the ownerless map-header row's NONE).
    const MapScript* scriptByPid(qulonglong sid) const;

    resource::GameResources& _resources;
    Map* _map = nullptr;

    QLineEdit* _filterEdit;
    QTableWidget* _table;
    QTableWidget* _localVarsTable; // local variables (LVARs) of the currently selected script

    // True while _localVarsTable is (re)filled, so the setData/setItem calls during populate don't fire
    // onLocalVarChanged as if the user had edited a value.
    bool _suppressVarEdit = false;
};

} // namespace geck
