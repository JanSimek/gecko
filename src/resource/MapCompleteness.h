#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "resource/DataFileSystem.h"

namespace geck {

class Map;

namespace resource {

    class GameResources;

    /// A floor/roof tile id the map places but whose art does not resolve in the mounted data.
    /// `art` is the tiles.lst-derived VFS path, or empty when it couldn't be formed — then
    /// `reason` says why ("tiles.lst not mounted", "tile id out of tiles.lst range").
    struct MissingTile {
        int id = 0;
        std::string art;
        std::string reason;
    };

    /// A distinct object FID whose sprite does not resolve. `art` is the FrmResolver path, or
    /// empty when resolution itself failed ("FID does not resolve").
    struct MissingObjectArt {
        std::uint32_t fid = 0;
        std::string art;
        std::string reason;
    };

    /// A distinct scripts.lst program index the map references (header script or a map_scripts
    /// entry) that does not resolve to a compiled script in the mounted data. `name` is the
    /// scripts.lst entry, or empty when the index itself is bad — then `reason` says why
    /// ("scripts.lst not mounted", "program index out of scripts.lst range",
    /// "compiled script not in mounted data").
    struct UnresolvedScript {
        std::uint32_t programIndex = 0;
        std::string name;
        std::string reason;
    };

    /// Everything a map references that the mounted data cannot supply, plus the mount context
    /// needed to judge whether the data paths themselves are sane. Computed by
    /// scanMapCompleteness; surfaced by the editor's completeness panel and by
    /// `gecko-cli resource missing` / the MCP resource_missing tool.
    struct MapCompletenessReport {
        std::size_t usedTileCount = 0;  //!< distinct floor/roof tile ids across all elevations
        std::size_t objectArtCount = 0; //!< distinct FIDs of placed (non-inventory) objects
        std::size_t scriptCount = 0;    //!< distinct scripts.lst program indices referenced

        std::vector<MissingTile> missingTiles;
        std::vector<MissingObjectArt> missingObjectArt;
        std::vector<UnresolvedScript> unresolvedScripts;

        std::vector<MountedSourceInfo> mounts; //!< data paths in mount (priority) order
        bool tilesLstMounted = false;          //!< art/tiles/tiles.lst loaded
        bool scriptsLstMounted = false;        //!< scripts/scripts.lst loaded

        bool complete() const {
            return missingTiles.empty() && missingObjectArt.empty() && unresolvedScripts.empty();
        }
        std::size_t missingCount() const {
            return missingTiles.size() + missingObjectArt.size() + unresolvedScripts.size();
        }
    };

    /// Check every resource the map references against the mounted data: used tile art, placed
    /// objects' sprites, and referenced scripts, mirroring the resolve steps the (tolerant) map
    /// loader runs. Read-only and Qt-free; never throws — an unmounted index file becomes a
    /// per-entry reason, not an error.
    MapCompletenessReport scanMapCompleteness(GameResources& resources, const Map& map);

} // namespace resource
} // namespace geck
