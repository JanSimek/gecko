#pragma once

#include <vector>

#include <QString>

namespace geck {

class Map;
class ViewportController;
class Tile;
struct TileChange;

namespace selection {
    class SelectionManager;
}

/**
 * @brief Narrow abstraction over the editor host that TilePlacementManager depends on.
 *
 * TilePlacementManager used to hold a raw EditorWidget* (and include EditorWidget.h),
 * which coupled tile placement to the full editor and made the manager untestable in
 * isolation. This interface declares exactly the methods TilePlacementManager needs
 * from its host; EditorWidget implements it.
 *
 * getViewportController() and getCurrentElevation() are also declared by
 * SelectionDataProvider; EditorWidget's single override of each satisfies both
 * interfaces (identical signatures, separate bases, no shared base => no ambiguity).
 */
class TilePlacementContext {
public:
    virtual ~TilePlacementContext() = default;

    virtual Map* getMap() const = 0;

    virtual void updateTileSprite(int hexIndex, bool isRoof) = 0;
    virtual void updateTileSprite(int hexIndex, bool isRoof, int elevation) = 0;

    virtual selection::SelectionManager* getSelectionManager() const = 0;
    virtual ViewportController* getViewportController() const = 0;
    virtual int getCurrentElevation() const = 0;
    virtual std::vector<Tile>& ensureElevationTiles(int elevation) = 0;
    virtual void registerTileEdit(const QString& description, const std::vector<TileChange>& changes) = 0;
};

} // namespace geck
