#include "ScriptsPanel.h"

#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapScript.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "resource/ScriptNames.h"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstdint>
#include <string>

namespace geck {

namespace {
    enum Column {
        COL_SECTION = 0,
        COL_SCRIPT_ID = 1,
        COL_FILENAME = 2,
        COL_NAME = 3,
        COL_OWNER = 4,
        COL_DETAIL = 5,
        COL_COUNT = 6,
    };
} // namespace

ScriptsPanel::ScriptsPanel(resource::GameResources& resources, QWidget* parent)
    : QWidget(parent)
    , _resources(resources) {

    auto* mainLayout = new QVBoxLayout(this);

    _filterEdit = new QLineEdit(this);
    _filterEdit->setPlaceholderText("Filter scripts...");
    mainLayout->addWidget(_filterEdit);

    auto* splitter = new QSplitter(Qt::Vertical, this);

    _table = new QTableWidget(0, COL_COUNT, this);
    _table->setObjectName("scriptsTable");
    _table->setHorizontalHeaderLabels({ "Section", "Script ID", "Filename", "Name", "Owner OID", "Detail" });
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->verticalHeader()->setVisible(false);
    _table->horizontalHeader()->setSectionResizeMode(COL_SECTION, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(COL_SCRIPT_ID, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(COL_FILENAME, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(COL_NAME, QHeaderView::Stretch);
    _table->horizontalHeader()->setSectionResizeMode(COL_OWNER, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(COL_DETAIL, QHeaderView::ResizeToContents);
    splitter->addWidget(_table);

    // The selected script's local variables (its slice of map_local_vars), shown below the list.
    auto* lvarContainer = new QWidget(this);
    auto* lvarLayout = new QVBoxLayout(lvarContainer);
    lvarLayout->setContentsMargins(0, 0, 0, 0);
    // Read-only: for a BASE map the engine forces every local variable to 0 at load (fallout2-ce
    // scripts.cc), so the stored value is informational only — the runtime start value.
    lvarLayout->addWidget(new QLabel("Local variables (runtime — start at 0)", lvarContainer));

    _localVarsTable = new QTableWidget(0, 2, lvarContainer);
    _localVarsTable->setObjectName("localVarsTable");
    _localVarsTable->setHorizontalHeaderLabels({ "#", "Value" });
    _localVarsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _localVarsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _localVarsTable->verticalHeader()->setVisible(false);
    _localVarsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _localVarsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    lvarLayout->addWidget(_localVarsTable);
    splitter->addWidget(lvarContainer);

    splitter->setStretchFactor(0, 3); // the script list gets the bulk of the height
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter);

    _table->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(_filterEdit, &QLineEdit::textChanged, this, &ScriptsPanel::applyFilter);
    connect(_table, &QTableWidget::cellDoubleClicked, this,
        [this](int row, int /*column*/) { onCellDoubleClicked(row); });
    connect(_table, &QTableWidget::itemSelectionChanged, this, &ScriptsPanel::onScriptSelectionChanged);
    connect(_table, &QTableWidget::customContextMenuRequested, this, &ScriptsPanel::onTableContextMenu);
}

void ScriptsPanel::setMap(Map* map) {
    _map = map;
    populate();
}

void ScriptsPanel::populate() {
    // Clearing/refilling the table churns the selection, which would otherwise fire
    // spatialScriptSelected(NONE) and clobber the map-side selection. Suppress those emits here;
    // MainWindow re-asserts the selection after an edit if it should survive.
    _suppressSpatialSelectionSignal = true;

    // Sorting must be off while inserting, or rows shuffle mid-build and setItem targets the wrong cell.
    _table->setSortingEnabled(false);
    _table->setRowCount(0);
    _localVarsTable->setRowCount(0); // selection is dropped by the rebuild; clear its detail view too

    if (_map == nullptr) {
        _table->setSortingEnabled(true);
        _suppressSpatialSelectionSignal = false;
        return;
    }

    // scripts.lst maps a 0-based program index (MapScript.script_id) to a filename. Loaded once; degrades
    // to empty filenames when game data isn't mounted (e.g. a freshly generated map).
    const Lst* lst = _resources.repository().load<Lst>(std::string(ResourcePaths::Lst::SCRIPTS));

    const auto& mapFile = _map->getMapFile();

    for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
        // The map_scripts section index is the script type (System/Spatial/Timer/Item/Critter).
        const auto type = static_cast<MapScript::ScriptType>(section);
        const QString sectionName = QString::fromUtf8(std::string(MapScript::toString(type)).c_str());

        for (const MapScript& script : mapFile.map_scripts[section]) {
            QString detail;
            if (type == MapScript::ScriptType::SPATIAL) {
                detail = QString("radius %1").arg(script.spatial_radius);
            } else if (type == MapScript::ScriptType::TIMER) {
                detail = QString("%1 ms").arg(script.timer);
            }
            // The script's own SID (its pid) drives double-click navigation; script_oid is the owner shown.
            addRow(sectionName, static_cast<int>(script.script_id), static_cast<qulonglong>(script.pid),
                static_cast<qulonglong>(script.script_oid), detail, lst);
        }
    }

    // The map's own script lives in the header (header.script_id), not in any map_scripts section, so it
    // is listed as one extra "Map" row. Its id is 1-based (unlike section scripts' 0-based script_id), so
    // subtract 1 to resolve it — mirroring the Map Info panel. It has no owning object, so the NONE
    // sentinel makes a double-click here do nothing. <=0 means no map script.
    if (mapFile.header.script_id > 0) {
        addRow(QStringLiteral("Map"), mapFile.header.script_id - 1, static_cast<qulonglong>(MapScript::NONE),
            static_cast<qulonglong>(MapScript::NONE), QStringLiteral("(map script)"), lst);
    }

    _table->setSortingEnabled(true);

    applyFilter(); // a re-populate (map switch) must honour the filter still shown in the box

    _suppressSpatialSelectionSignal = false;
}

void ScriptsPanel::addRow(const QString& section, int programIndex, qulonglong rowSid, qulonglong ownerOid,
    const QString& detail, const Lst* lst) {
    const int row = _table->rowCount();
    _table->insertRow(row);

    _table->setItem(row, COL_SECTION, new QTableWidgetItem(section));

    // Numeric display + numeric sort for the program index; rowSid in UserRole lets a double-click jump
    // to the owning object (see onCellDoubleClicked).
    auto* idItem = new QTableWidgetItem;
    idItem->setData(Qt::DisplayRole, programIndex);
    idItem->setData(Qt::UserRole, rowSid);
    _table->setItem(row, COL_SCRIPT_ID, idItem);

    QString filename;
    if (lst != nullptr && programIndex >= 0 && static_cast<size_t>(programIndex) < lst->list().size()) {
        filename = QString::fromStdString(lst->list().at(static_cast<size_t>(programIndex)));
    }
    _table->setItem(row, COL_FILENAME, new QTableWidgetItem(filename));
    _table->setItem(row, COL_NAME,
        new QTableWidgetItem(QString::fromStdString(resource::scriptDescription(_resources, programIndex))));

    // Owned rows carry a numeric DisplayRole so the Owner column sorts numerically; NONE/0 shows "—".
    auto* ownerItem = new QTableWidgetItem;
    if (ownerOid != MapScript::NONE && ownerOid != 0) {
        ownerItem->setData(Qt::DisplayRole, ownerOid);
    } else {
        ownerItem->setText(QStringLiteral("—"));
    }
    _table->setItem(row, COL_OWNER, ownerItem);

    _table->setItem(row, COL_DETAIL, new QTableWidgetItem(detail));
}

void ScriptsPanel::applyFilter() {
    const QString text = _filterEdit->text();
    for (int row = 0; row < _table->rowCount(); ++row) {
        bool match = text.isEmpty();
        for (int col = 0; !match && col < _table->columnCount(); ++col) {
            const QTableWidgetItem* item = _table->item(row, col);
            if (item != nullptr && item->text().contains(text, Qt::CaseInsensitive)) {
                match = true;
            }
        }
        _table->setRowHidden(row, !match);
    }
}

void ScriptsPanel::onCellDoubleClicked(int row) {
    // The row's SID is stashed in UserRole; NONE marks an ownerless row (the map's own header
    // script), which has neither an object to navigate to nor an editable record.
    const uint32_t sid = sidOfRow(row);
    if (sid == MapScript::NONE) {
        return;
    }

    // Double-clicking a spatial row edits it. Only object-owned script types (ITEM / CRITTER) can be
    // navigated to; SYSTEM and TIMER scripts are ownerless, so a double-click there does nothing.
    const MapScript::ScriptType type = MapScript::fromPid(sid);
    if (type == MapScript::ScriptType::SPATIAL) {
        Q_EMIT spatialScriptEditRequested(sid);
    } else if (type == MapScript::ScriptType::ITEM || type == MapScript::ScriptType::CRITTER) {
        Q_EMIT scriptObjectActivated(static_cast<int>(sid));
    }
}

void ScriptsPanel::onTableContextMenu(const QPoint& pos) {
    const int row = _table->indexAt(pos).row();
    const uint32_t sid = sidOfRow(row);
    if (MapScript::fromPid(sid) != MapScript::ScriptType::SPATIAL) {
        return; // Edit/Delete are spatial-only (other scripts live on their owning object).
    }

    _table->selectRow(row); // right-click also selects, syncing the map highlight

    QMenu menu(this);
    const QAction* editAction = menu.addAction(tr("Edit Spatial Script..."));
    const QAction* deleteAction = menu.addAction(tr("Delete Spatial Script"));
    const QAction* chosen = menu.exec(_table->viewport()->mapToGlobal(pos));
    if (chosen == editAction) {
        Q_EMIT spatialScriptEditRequested(sid);
    } else if (chosen == deleteAction) {
        Q_EMIT spatialScriptDeleteRequested(sid);
    }
}

uint32_t ScriptsPanel::sidOfRow(int row) const {
    if (row < 0) {
        return MapScript::NONE;
    }
    const QTableWidgetItem* idItem = _table->item(row, COL_SCRIPT_ID);
    return idItem == nullptr ? MapScript::NONE
                             : static_cast<uint32_t>(idItem->data(Qt::UserRole).toULongLong());
}

void ScriptsPanel::selectSpatialScriptRow(uint32_t sid) {
    _suppressSpatialSelectionSignal = true;
    if (sid == MapScript::NONE) {
        _table->clearSelection();
    } else {
        int targetRow = -1;
        for (int row = 0; row < _table->rowCount(); ++row) {
            if (sidOfRow(row) == sid) {
                targetRow = row;
                break;
            }
        }
        if (targetRow >= 0) {
            _table->selectRow(targetRow);
            _table->scrollToItem(_table->item(targetRow, COL_SCRIPT_ID));
        } else {
            _table->clearSelection();
        }
    }
    _suppressSpatialSelectionSignal = false;
}

const MapScript* ScriptsPanel::scriptByPid(qulonglong sid) const {
    if (_map == nullptr || sid == MapScript::NONE) {
        return nullptr;
    }
    for (const auto& section : _map->getMapFile().map_scripts) {
        for (const MapScript& script : section) {
            if (script.pid == sid) {
                return &script;
            }
        }
    }
    return nullptr;
}

void ScriptsPanel::onScriptSelectionChanged() {
    _localVarsTable->setRowCount(0);

    const int row = _table->currentRow();
    const uint32_t sid = _table->selectionModel()->hasSelection() ? sidOfRow(row) : MapScript::NONE;

    // Mirror the shared spatial selection: a spatial row selects that script on the map, anything
    // else clears it. Suppressed while a rebuild or a map-driven sync is churning the selection.
    if (!_suppressSpatialSelectionSignal) {
        const bool spatial = MapScript::fromPid(sid) == MapScript::ScriptType::SPATIAL;
        Q_EMIT spatialScriptSelected(spatial ? sid : MapScript::NONE);
    }

    // Resolve the selected row back to its MapScript and show its slice of map_local_vars
    // (local_var_offset .. +local_var_count). Ownerless rows (the map-header row) resolve to nullptr.
    const MapScript* script = scriptByPid(sid);
    if (script == nullptr || script->local_var_offset == MapScript::NONE) {
        return;
    }

    const auto& lvars = _map->getMapFile().map_local_vars;
    for (uint32_t i = 0; i < script->local_var_count; ++i) {
        const std::size_t index = static_cast<std::size_t>(script->local_var_offset) + i;
        if (index >= lvars.size()) {
            break; // header count outruns the stored array — stop rather than read out of bounds
        }
        const int lvarRow = _localVarsTable->rowCount();
        _localVarsTable->insertRow(lvarRow);

        auto* indexItem = new QTableWidgetItem;
        indexItem->setData(Qt::DisplayRole, static_cast<int>(i));
        _localVarsTable->setItem(lvarRow, 0, indexItem);

        auto* valueItem = new QTableWidgetItem;
        valueItem->setData(Qt::DisplayRole, lvars[index]);
        _localVarsTable->setItem(lvarRow, 1, valueItem);
    }
}

} // namespace geck
