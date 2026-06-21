#pragma once

#include "format/maps/MapsTxt.h"

#include <string>

namespace geck::resource {

class GameResources;

/// Resolves Fallout 2 map identifiers to names, from the mounted game data:
///  - the **.map filename** for a map index, via the vault `MapsTxt` reader (`data/maps.txt`);
///  - the **friendly display name** for `(index, elevation)`, via the engine's mapGetName formula
///    `map.msg[index*3 + elevation + 200]` (fallout2-ce map.cc).
///
/// Shared by the headless analyze/describe_map path and the editor's exit-grid dialog so the index↔
/// name rules live in one place. `maps.txt` is read once at construction; `map.msg` is loaded through
/// the repository cache on demand. Degrades to empty strings when the data isn't mounted.
class MapNameResolver {
public:
    explicit MapNameResolver(GameResources& resources);

    const MapsTxt& mapsTxt() const { return _mapsTxt; }

    /// The .map filename for a maps.txt index, or "" when the index is unknown or negative
    /// (e.g. the -1 town-map / -2 worldmap exit sentinels).
    std::string fileNameOf(int mapIndex) const;

    /// The friendly map.msg name for `(mapIndex, elevation)`, or "" when unavailable.
    std::string displayName(int mapIndex, int elevation) const;

    /// The maps.txt index for a .map filename (basename, any case), or -1 if absent.
    int indexOf(const std::string& mapFileName) const;

private:
    GameResources& _resources;
    MapsTxt _mapsTxt;
};

} // namespace geck::resource
