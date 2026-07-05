#include "resource/MapCompleteness.h"

#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "format/map/Tile.h"
#include "resource/FrmResolver.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"

#include <set>

namespace geck::resource {

namespace {

    // Load an index .lst through the repository, degrading to nullptr when it isn't mounted —
    // reported per-entry by the caller, never thrown.
    const Lst* tryLoadLst(GameResources& resources, std::string_view path) {
        try {
            return resources.repository().load<Lst>(std::string(path));
        } catch (const std::exception&) {
            return nullptr;
        }
    }

    // Tiles: distinct floor/roof ids across every elevation -> tiles.lst filename -> exists?
    void scanTiles(GameResources& resources, const Map& map, MapCompletenessReport& report) {
        std::set<int> usedTileIds;
        for (const auto& [elevation, tiles] : map.getMapFile().tiles) {
            for (const auto& tile : tiles) {
                if (tile.getFloor() != Map::EMPTY_TILE) {
                    usedTileIds.insert(tile.getFloor());
                }
                if (tile.getRoof() != Map::EMPTY_TILE) {
                    usedTileIds.insert(tile.getRoof());
                }
            }
        }
        report.usedTileCount = usedTileIds.size();

        const Lst* tilesLst = tryLoadLst(resources, ResourcePaths::Lst::TILES);
        report.tilesLstMounted = tilesLst != nullptr;

        for (int id : usedTileIds) {
            if (tilesLst == nullptr) {
                report.missingTiles.push_back({ id, {}, "tiles.lst not mounted" });
                continue;
            }
            const auto& names = tilesLst->list();
            if (id < 0 || static_cast<std::size_t>(id) >= names.size()) {
                report.missingTiles.push_back({ id, {}, "tile id out of tiles.lst range" });
                continue;
            }
            std::string art = std::string(ResourcePaths::Directories::TILES) + names[static_cast<std::size_t>(id)];
            if (!resources.files().exists(art)) {
                report.missingTiles.push_back({ id, std::move(art), "art not in mounted data" });
            }
        }
    }

    // Object art: distinct FIDs -> resolved art path -> exists? (resolve() can throw)
    void scanObjectArt(GameResources& resources, const Map& map, MapCompletenessReport& report) {
        std::set<std::uint32_t> checkedFids;
        for (const auto& [elevation, objects] : map.objects()) {
            for (const auto& object : objects) {
                if (object->position == -1) {
                    continue; // inventory/container child, not placed on the map
                }
                if (!checkedFids.insert(object->frm_pid).second) {
                    continue; // this FID already checked
                }
                std::string art;
                try {
                    art = resources.frmResolver().resolve(object->frm_pid);
                } catch (const std::exception&) {
                    report.missingObjectArt.push_back({ object->frm_pid, {}, "FID does not resolve" });
                    continue;
                }
                if (art.empty() || !resources.files().exists(art)) {
                    report.missingObjectArt.push_back({ object->frm_pid, std::move(art), "art not in mounted data" });
                }
            }
        }
        report.objectArtCount = checkedFids.size();
    }

    // Scripts: distinct scripts.lst program indices -> filename -> compiled .int exists?
    // The header's own script id is 1-based (0 / -1 = none); map_scripts entries carry the
    // 0-based program index directly (see ScriptsPanel / MapInfoPanel).
    void scanScripts(GameResources& resources, const Map& map, MapCompletenessReport& report) {
        std::set<std::uint32_t> programIndices;
        if (map.getMapFile().header.script_id > 0) {
            programIndices.insert(static_cast<std::uint32_t>(map.getMapFile().header.script_id - 1));
        }
        for (const auto& section : map.getMapFile().map_scripts) {
            for (const auto& script : section) {
                if (script.script_id != MapScript::NONE) {
                    programIndices.insert(script.script_id);
                }
            }
        }
        report.scriptCount = programIndices.size();

        const Lst* scriptsLst = tryLoadLst(resources, ResourcePaths::Lst::SCRIPTS);
        report.scriptsLstMounted = scriptsLst != nullptr;

        for (std::uint32_t index : programIndices) {
            if (scriptsLst == nullptr) {
                report.unresolvedScripts.push_back({ index, {}, "scripts.lst not mounted" });
                continue;
            }
            const auto& names = scriptsLst->list();
            if (index >= names.size()) {
                report.unresolvedScripts.push_back({ index, {}, "program index out of scripts.lst range" });
                continue;
            }
            const std::string& name = names[index];
            if (!resources.files().exists(std::string(ResourcePaths::Directories::SCRIPTS) + name)) {
                report.unresolvedScripts.push_back({ index, name, "compiled script not in mounted data" });
            }
        }
    }

} // namespace

MapCompletenessReport scanMapCompleteness(GameResources& resources, const Map& map) {
    MapCompletenessReport report;
    report.mounts = resources.files().mounts();
    scanTiles(resources, map, report);
    scanObjectArt(resources, map, report);
    scanScripts(resources, map, report);
    return report;
}

} // namespace geck::resource
