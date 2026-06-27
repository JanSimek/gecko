#pragma once

#include "format/maps/MapsTxt.h"

#include <string>
#include <vector>

namespace geck::resource {

class GameResources;

/// One map entry for the by-name destination picker: its maps.txt index plus the resolved .map
/// filename and the primary (elevation 0) map.msg display name.
struct MapName {
    int index = -1;
    std::string fileName;
    std::string displayName;
};

/// Resolves Fallout 2 map identifiers to names, from the mounted game data:
///  - the **.map filename** for a map index, via the `data/maps.txt` document;
///  - the **friendly display name** for `(index, elevation)`, via the engine's mapGetName formula
///    `map.msg[index*3 + elevation + 200]` (fallout2-ce map.cc).
///
/// Shared by the headless analyze/describe_map path and the editor's exit-grid dialog so the index↔
/// name rules live in one place. `maps.txt` is read once at construction; `map.msg` is loaded through
/// the repository cache on demand. Degrades to empty strings when the data isn't mounted.
class MapNameResolver {
public:
    explicit MapNameResolver(GameResources& resources);

    /// The .map filename for a maps.txt index, or "" when the index is unknown or negative
    /// (e.g. the -1 town-map / -2 worldmap exit sentinels).
    std::string fileNameOf(int mapIndex) const;

    /// The friendly map.msg name for `(mapIndex, elevation)`, or "" when unavailable.
    std::string displayName(int mapIndex, int elevation) const;

    /// The maps.txt index for a .map filename (basename, any case), or -1 if absent.
    int indexOf(const std::string& mapFileName) const;

    /// Every `[Map N]` section with a non-negative index, as `{ index, fileNameOf(index),
    /// displayName(index, 0) }`, sorted by filename (case-insensitive). The single source for the
    /// editor's by-name destination-map picker, so the index↔name rules stay here. Empty when no
    /// maps.txt is mounted.
    std::vector<MapName> allMaps() const;

    /// The .map filename for a maps.txt lookup_name (city.txt's entrance map key, e.g. "Arroyo
    /// Bridge"), case-insensitive, or "" if no map has that lookup_name. The join from the worldmap
    /// layer (city.txt areas) to the .map files / exit-grid graph.
    std::string fileNameOfLookup(const std::string& lookupName) const;

    /// The lookup_name for a .map filename (basename, any case), or "" if absent. The reverse join,
    /// from a .map file to the worldmap key city.txt uses.
    std::string lookupNameOf(const std::string& mapFileName) const;

private:
    GameResources& _resources;
    MapsTxt _doc;
};

} // namespace geck::resource
