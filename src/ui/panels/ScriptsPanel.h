#pragma once

#include <QWidget>

class QTableWidget;
class QLineEdit;

namespace geck {

class Map;
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

private slots:
    void applyFilter(); // hide rows that don't match _filterEdit; re-applied after every populate()

private:
    void populate();

    resource::GameResources& _resources;
    Map* _map = nullptr;

    QLineEdit* _filterEdit;
    QTableWidget* _table;
};

} // namespace geck
