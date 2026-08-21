#pragma once

#include <QWidget>

#include <memory>
#include <string>

class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QToolButton;

namespace geck::resource {
class GameResources;
}

namespace geck::worldmap {
class WorldMapScene;
struct AreaMarker;
}

namespace geck {

class WorldMapView;

/// The World Map screen: the rendered worldmap next to a searchable list of its areas.
///
/// Sits in the main window's central stack alongside the welcome screen and the map editor, because
/// it is a view of the game's data rather than of any one map — it needs mounted game data but no
/// open map.
class WorldMapWidget : public QWidget {
    Q_OBJECT

public:
    explicit WorldMapWidget(resource::GameResources& resources, QWidget* parent = nullptr);
    ~WorldMapWidget() override;

    /// (Re)reads city.txt/worldmap.txt and redraws. Safe to call whenever the data paths change;
    /// returns false (and shows why, in place of the map) when there is no worldmap to draw.
    bool reload();

    /// True once a worldmap has been loaded successfully.
    [[nodiscard]] bool hasScene() const;

Q_SIGNALS:
    /// The user asked to open a map belonging to an area (double-click, or the Open Map button).
    void openMapRequested(const QString& mapFileName);

private:
    void setupUi();
    void refreshAreaList();
    /// Puts the scene's missingArt() on screen, so black tiles and unclickable markers have a
    /// visible cause rather than only a log line.
    void showMissingArt();
    void onAreaSelected(const worldmap::AreaMarker* area);
    void onAreaActivated(const worldmap::AreaMarker* area);
    void onHovered(int worldX, int worldY, const worldmap::AreaMarker* area);
    void onFilterChanged(const QString& text);

    resource::GameResources& _resources;

    QStackedWidget* _canvasStack = nullptr; ///< the map, or the placeholder in its place
    WorldMapView* _view = nullptr;
    QLabel* _status = nullptr;      ///< live pointer readout: position, terrain, area
    QLabel* _summary = nullptr;     ///< what was loaded; not overwritten by the readout
    QLabel* _warning = nullptr;     ///< missing art, hidden when there is none
    QLabel* _placeholder = nullptr; ///< shown instead of the map when there is no worldmap data
    QLineEdit* _filter = nullptr;
    QListWidget* _areaList = nullptr;
    QToolButton* _openMapButton = nullptr;

    std::shared_ptr<worldmap::WorldMapScene> _scene;
    std::string _selectedMapFile; ///< the first map of the selected area, "" when none
};

} // namespace geck
