#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace geck {

class Tile;

/**
 * @brief The presentation/event sink an ObjectCommandController notifies as it mutates a map.
 *
 * The command layer performs the mutation; this interface is everything it needs to call
 * *back* into its host for presentation refresh and elevation/tile access. Splitting it out
 * means the command controller depends on one narrow seam instead of six loose std::functions,
 * and lets a headless caller (gecko-cli generation) supply a trivial implementation instead of
 * wiring up rendering/UI infrastructure. Mirrors the existing TilePlacementContext /
 * ExitGridContext idiom: a narrow host interface the full editor (EditorWidget) implements.
 */
class CommandHost {
public:
    virtual ~CommandHost() = default;

    /// Rebuild object visuals after a placement/deletion/edit.
    virtual void refreshObjects() = 0;
    /// A command was recorded — refresh undo/redo affordances.
    virtual void undoStackChanged() = 0;
    /// The tile vector for an elevation, creating it if the elevation was empty.
    virtual std::vector<Tile>& ensureElevationTiles(int elevation) = 0;
    /// The elevation edits currently target.
    virtual int getCurrentElevation() const = 0;
    /// A single floor/roof tile's sprite changed and should be re-stitched.
    virtual void updateTileSprite(int hexIndex, bool isRoof, int elevation) = 0;
    /// Reload every tile sprite (after a bulk tile change such as a cleared/copied elevation).
    virtual void reloadTiles() = 0;
};

/**
 * @brief CommandHost backed by std::function callbacks.
 *
 * For callers that already produce the host hooks as closures (the editor wraps EditorWidget
 * methods; the CLI supplies no-ops + trivial accessors) rather than implementing CommandHost
 * directly. Owns the closures; outlive the ObjectCommandController that references it.
 */
class CallbackCommandHost : public CommandHost {
public:
    CallbackCommandHost(std::function<void()> refreshObjects,
        std::function<void()> undoStackChanged,
        std::function<std::vector<Tile>&(int)> ensureElevationTiles,
        std::function<int()> getCurrentElevation,
        std::function<void(int, bool, int)> updateTileSprite,
        std::function<void()> reloadTiles)
        : _refreshObjects(std::move(refreshObjects))
        , _undoStackChanged(std::move(undoStackChanged))
        , _ensureElevationTiles(std::move(ensureElevationTiles))
        , _getCurrentElevation(std::move(getCurrentElevation))
        , _updateTileSprite(std::move(updateTileSprite))
        , _reloadTiles(std::move(reloadTiles)) {
    }

    void refreshObjects() override { _refreshObjects(); }
    void undoStackChanged() override { _undoStackChanged(); }
    std::vector<Tile>& ensureElevationTiles(int elevation) override { return _ensureElevationTiles(elevation); }
    int getCurrentElevation() const override { return _getCurrentElevation(); }
    void updateTileSprite(int hexIndex, bool isRoof, int elevation) override { _updateTileSprite(hexIndex, isRoof, elevation); }
    void reloadTiles() override { _reloadTiles(); }

private:
    std::function<void()> _refreshObjects;
    std::function<void()> _undoStackChanged;
    std::function<std::vector<Tile>&(int)> _ensureElevationTiles;
    std::function<int()> _getCurrentElevation;
    std::function<void(int, bool, int)> _updateTileSprite;
    std::function<void()> _reloadTiles;
};

} // namespace geck
