#include "cli/ResourceInspect.h"

#include "cli/MapLoad.h"
#include "format/map/Map.h"
#include "resource/DataFileSystem.h"
#include "resource/GameResources.h"
#include "resource/MapCompleteness.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <ostream>
#include <regex>
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

    // The scan itself is shared with the editor's completeness panel (resource/MapCompleteness);
    // this function only renders the report as JSON.
    const resource::MapCompletenessReport report = resource::scanMapCompleteness(resources, *map);

    auto missingTiles = json::array();
    for (const auto& tile : report.missingTiles) {
        missingTiles.push_back({ { "id", tile.id },
            { "art", tile.art.empty() ? json(nullptr) : json(tile.art) },
            { "reason", tile.reason } });
    }

    auto missingObjectArt = json::array();
    for (const auto& art : report.missingObjectArt) {
        missingObjectArt.push_back({ { "pid", art.fid },
            { "art", art.art.empty() ? json(nullptr) : json(art.art) },
            { "reason", art.reason } });
    }

    auto missingScripts = json::array();
    for (const auto& script : report.unresolvedScripts) {
        missingScripts.push_back({ { "programIndex", script.programIndex },
            { "name", script.name.empty() ? json(nullptr) : json(script.name) },
            { "reason", script.reason } });
    }

    auto mounts = json::array();
    for (const auto& mount : report.mounts) {
        mounts.push_back(sourceInfoJson(mount));
    }

    json result{
        { "map", mapPath },
        { "usedTileCount", report.usedTileCount },
        { "objectArtCount", report.objectArtCount },
        { "scriptCount", report.scriptCount },
        { "missingTiles", std::move(missingTiles) },
        { "missingObjectArt", std::move(missingObjectArt) },
        { "missingScripts", std::move(missingScripts) },
        { "mounts", std::move(mounts) },
        { "tilesLstMounted", report.tilesLstMounted },
        { "scriptsLstMounted", report.scriptsLstMounted },
    };
    out << result.dump() << "\n";
    return 0;
}

} // namespace geck::cli
