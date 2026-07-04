#include "cli/ResourceInspect.h"

#include "cli/MapLoad.h"
#include "format/lst/Lst.h"
#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/Tile.h"
#include "resource/DataFileSystem.h"
#include "resource/FrmResolver.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <optional>
#include <ostream>
#include <regex>
#include <set>
#include <string>

using json = nlohmann::json;

namespace geck::cli {

namespace {

    // JSON for a resolved source, or null when the path isn't mounted.
    json sourceInfoJson(const std::optional<resource::MountedSourceInfo>& info) {
        if (!info) {
            return nullptr;
        }
        const char* kind = info->kind == resource::MountedSourceInfo::Kind::Dat ? "dat" : "directory";
        return json{ { "kind", kind }, { "path", info->sourcePath.generic_string() }, { "label", info->displayLabel } };
    }

    // Compile a glob ('*','?') into a case-insensitive substring matcher. Substring (not fully
    // anchored) so a user pattern matches regardless of the VFS's leading slash / alias prefix:
    // "art/tiles/gras*" and "gras03*" both find "/art/tiles/gras030.frm". Every other regex
    // metacharacter is escaped so a path is matched literally.
    std::regex compileGlob(const std::string& glob) {
        std::string pattern;
        pattern.reserve(glob.size() * 2);
        for (char ch : glob) {
            switch (ch) {
                case '*':
                    pattern.append(".*");
                    break;
                case '?':
                    pattern.push_back('.');
                    break;
                case '.':
                case '\\':
                case '+':
                case '(':
                case ')':
                case '[':
                case ']':
                case '{':
                case '}':
                case '^':
                case '$':
                case '|':
                    pattern.push_back('\\');
                    pattern.push_back(ch);
                    break;
                default:
                    pattern.push_back(ch);
                    break;
            }
        }
        return std::regex(pattern, std::regex::icase);
    }

} // namespace

int resourceFind(resource::GameResources& resources, const std::string& path, std::ostream& out) {
    auto& files = resources.files();
    const bool found = files.exists(path);
    json result{
        { "path", path },
        { "found", found },
        { "source", sourceInfoJson(found ? files.sourceInfo(path) : std::nullopt) },
    };
    out << result.dump() << "\n";
    return 0; // "not found" is a valid answer, not a failure
}

int resourceList(resource::GameResources& resources, const std::string& glob, std::ostream& out) {
    // Cap the reported entries so a broad glob (or "*") can't dump tens of thousands of lines; the
    // 'truncated' flag tells the caller the match set was larger.
    constexpr std::size_t kMaxEntries = 2000;

    auto& files = resources.files();
    const std::regex matcher = compileGlob(glob);

    auto entries = json::array();
    std::size_t matched = 0;
    for (const auto& entry : files.list("*")) {
        if (!std::regex_search(entry.generic_string(), matcher)) {
            continue;
        }
        ++matched;
        if (entries.size() < kMaxEntries) {
            entries.push_back({ { "path", entry.generic_string() }, { "source", sourceInfoJson(files.sourceInfo(entry)) } });
        }
    }

    json result{
        { "pattern", glob },
        { "count", matched },
        { "truncated", matched > kMaxEntries },
        { "entries", std::move(entries) },
    };
    out << result.dump() << "\n";
    return 0;
}

int resourceMissing(resource::GameResources& resources, const std::string& mapPath, std::ostream& out) {
    auto map = cli::loadMap(resources, mapPath);
    if (!map) {
        out << json{ { "map", mapPath }, { "error", "could not read or parse map" } }.dump() << "\n";
        return 1;
    }

    auto& files = resources.files();

    // --- Tiles: distinct floor/roof ids across every elevation -> tiles.lst filename -> exists? ---
    std::set<int> usedTileIds;
    for (const auto& [elevation, tiles] : map->getMapFile().tiles) {
        for (const auto& tile : tiles) {
            if (tile.getFloor() != Map::EMPTY_TILE) {
                usedTileIds.insert(tile.getFloor());
            }
            if (tile.getRoof() != Map::EMPTY_TILE) {
                usedTileIds.insert(tile.getRoof());
            }
        }
    }

    const Lst* tilesLst = nullptr;
    try {
        tilesLst = resources.repository().load<Lst>(std::string(ResourcePaths::Lst::TILES));
    } catch (const std::exception&) {
        tilesLst = nullptr; // reported below as an unresolved tiles.lst
    }

    auto missingTiles = json::array();
    for (int id : usedTileIds) {
        if (tilesLst == nullptr) {
            missingTiles.push_back({ { "id", id }, { "art", nullptr }, { "reason", "tiles.lst not mounted" } });
            continue;
        }
        const auto& names = tilesLst->list();
        if (id < 0 || static_cast<std::size_t>(id) >= names.size()) {
            missingTiles.push_back({ { "id", id }, { "art", nullptr }, { "reason", "tile id out of tiles.lst range" } });
            continue;
        }
        const std::string art = "art/tiles/" + names[static_cast<std::size_t>(id)];
        if (!files.exists(art)) {
            missingTiles.push_back({ { "id", id }, { "art", art } });
        }
    }

    // --- Object art: distinct FIDs -> resolved art path -> exists? (resolve() can throw) ---
    std::set<std::uint32_t> checkedFids;
    auto missingObjectArt = json::array();
    for (const auto& [elevation, objects] : map->objects()) {
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
                missingObjectArt.push_back({ { "pid", object->frm_pid }, { "art", nullptr }, { "reason", "FID does not resolve" } });
                continue;
            }
            if (art.empty() || !files.exists(art)) {
                missingObjectArt.push_back({ { "pid", object->frm_pid }, { "art", art.empty() ? json(nullptr) : json(art) } });
            }
        }
    }

    json result{
        { "map", mapPath },
        { "usedTileCount", usedTileIds.size() },
        { "objectArtCount", checkedFids.size() },
        { "missingTiles", std::move(missingTiles) },
        { "missingObjectArt", std::move(missingObjectArt) },
    };
    out << result.dump() << "\n";
    return 0;
}

} // namespace geck::cli
