#include "ScriptsPanel.h"

#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapScript.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "resource/ScriptNames.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QVBoxLayout>

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

    _table = new QTableWidget(0, COL_COUNT, this);
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
    mainLayout->addWidget(_table);

    connect(_filterEdit, &QLineEdit::textChanged, this, &ScriptsPanel::applyFilter);
}

void ScriptsPanel::setMap(Map* map) {
    _map = map;
    populate();
}

void ScriptsPanel::populate() {
    // Sorting must be off while inserting, or rows shuffle mid-build and setItem targets the wrong cell.
    _table->setSortingEnabled(false);
    _table->setRowCount(0);

    if (_map == nullptr) {
        _table->setSortingEnabled(true);
        return;
    }

    // scripts.lst maps a 0-based program index (MapScript.script_id) to a filename. Loaded once; degrades
    // to empty filenames when game data isn't mounted (e.g. a freshly generated map).
    const Lst* lst = _resources.repository().load<Lst>(std::string(ResourcePaths::Lst::SCRIPTS));

    const auto& mapFile = _map->getMapFile();

    int row = 0;
    for (int section = 0; section < Map::SCRIPT_SECTIONS; ++section) {
        // The map_scripts section index is the script type (System/Spatial/Timer/Item/Critter).
        const auto type = static_cast<MapScript::ScriptType>(section);
        const QString sectionName = QString::fromUtf8(std::string(MapScript::toString(type)).c_str());

        for (const MapScript& script : mapFile.map_scripts[section]) {
            _table->insertRow(row);

            _table->setItem(row, COL_SECTION, new QTableWidgetItem(sectionName));

            // Numeric display + numeric sort for the program index.
            auto* idItem = new QTableWidgetItem;
            idItem->setData(Qt::DisplayRole, static_cast<int>(script.script_id));
            _table->setItem(row, COL_SCRIPT_ID, idItem);

            QString filename;
            if (lst != nullptr && script.script_id < lst->list().size()) {
                filename = QString::fromStdString(lst->list().at(script.script_id));
            }
            _table->setItem(row, COL_FILENAME, new QTableWidgetItem(filename));

            const std::string name = resource::scriptDisplayName(_resources, static_cast<int>(script.script_id));
            _table->setItem(row, COL_NAME, new QTableWidgetItem(QString::fromStdString(name)));

            // script_oid links the script to its owner object; NONE (-1) or 0 means no owner. Owned rows
            // carry a numeric DisplayRole so the column sorts numerically (script_oid is uint32_t).
            auto* ownerItem = new QTableWidgetItem;
            if (script.script_oid != MapScript::NONE && script.script_oid != 0) {
                ownerItem->setData(Qt::DisplayRole, static_cast<qulonglong>(script.script_oid));
            } else {
                ownerItem->setText(QStringLiteral("—"));
            }
            _table->setItem(row, COL_OWNER, ownerItem);

            QString detail;
            if (type == MapScript::ScriptType::SPATIAL) {
                detail = QString("radius %1").arg(script.spatial_radius);
            } else if (type == MapScript::ScriptType::TIMER) {
                detail = QString("%1 ms").arg(script.timer);
            }
            _table->setItem(row, COL_DETAIL, new QTableWidgetItem(detail));

            ++row;
        }
    }

    _table->setSortingEnabled(true);

    applyFilter(); // a re-populate (map switch) must honour the filter still shown in the box
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

} // namespace geck
