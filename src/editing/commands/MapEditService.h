#pragma once

#include <SFML/Graphics.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "format/map/Map.h"

namespace geck {

class Object;
class Tile;
struct MapObject;
class ScriptEditService;
class UndoBatcher;

/**
 * @brief Map-wide editing operations.
 *
 * One of the aggregate services ObjectCommandController delegates to: resetting
 * the map to a blank slate, clearing an elevation's objects, and copying one
 * elevation onto another. Clearing/copying touch scripts, so the service borrows
 * ScriptEditService for the section snapshot/restore/erase machinery.
 */
class MapEditService {
public:
    MapEditService(std::unique_ptr<Map>& map,
        std::vector<std::shared_ptr<Object>>& objects,
        std::vector<sf::Sprite>& wallBlockerOverlays,
        ScriptEditService& scriptService,
        UndoBatcher& batcher,
        std::function<void()> refreshObjects,
        std::function<void()> reloadTiles,
        std::function<void()> headerChanged);

    /// Replaces the bound map with a fresh empty Fallout 2 map and refreshes the
    /// view. Destructive reset (like File > New); NOT recorded on the undo stack, and it clears
    /// the history recorded against the old map.
    void newEmptyMap();

    /// Deletes every object on an elevation as one undoable command (pruning their
    /// scripts). Returns false if the elevation has no objects.
    bool clearElevationObjects(int elevation);

    /// Copies tiles + objects (deep-cloned, scripts detached) from one elevation to
    /// another as one undoable command, overwriting the destination.
    bool copyElevation(int fromElevation, int toElevation);

    /// Replaces the map header as one undoable command. Consecutive edits sharing a non-empty
    /// `mergeKey` (a spin box stepped through values) collapse into one undo entry. Returns false
    /// if nothing changes.
    bool setHeader(const Map::MapHeader& after, const std::string& description, const std::string& mergeKey = {});

    /// Enables or disables an elevation as one undoable command. Disabling also deletes its tiles,
    /// its objects, their scripts and the spatial scripts placed on it: the engine reads every
    /// elevation's objects whatever the flag says, so they would stay in the file with no floor,
    /// and their scripts would run with no owner. Returns false if nothing changes or the
    /// elevation is the last one present.
    bool setElevationPresent(int elevation, bool present);

private:
    // Everything setElevationPresent changes, so undo/redo can put it back verbatim.
    struct ElevationState {
        uint32_t flags = 0;
        std::optional<std::vector<Tile>> tiles;
        std::optional<std::vector<std::shared_ptr<MapObject>>> objects;
    };
    ElevationState captureElevation(int elevation) const;
    void applyElevationState(int elevation, const ElevationState& state);
    // setElevationPresent's steps: whether the change is allowed, and the edit in each direction.
    bool canToggleElevation(int elevation, bool present) const;
    void addElevation(int elevation);
    void removeElevation(int elevation, const std::optional<std::vector<std::shared_ptr<MapObject>>>& objects);

    // Deep-clones an elevation's objects, retargeted to toElevation with their
    // script linkage detached (scripts are not copied).
    std::vector<std::shared_ptr<MapObject>> cloneElevationObjects(int fromElevation, int toElevation);
    // Replaces an elevation's objects (and optionally tiles) and refreshes the view.
    // Shared by the undo and redo halves of copyElevation.
    void applyElevationSnapshot(int elevation, const std::vector<std::shared_ptr<MapObject>>& objects,
        const std::vector<Tile>& tiles, bool haveTiles);

    std::unique_ptr<Map>& _map;
    std::vector<std::shared_ptr<Object>>& _objects;
    std::vector<sf::Sprite>& _wallBlockerOverlays;
    ScriptEditService& _scriptService;
    UndoBatcher& _batcher;
    std::function<void()> _refreshObjects;
    std::function<void()> _reloadTiles;
    std::function<void()> _headerChanged;
};

} // namespace geck
