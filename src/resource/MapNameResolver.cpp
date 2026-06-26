#include "resource/MapNameResolver.h"

#include "format/msg/Msg.h"
#include "reader/maps/MapsTxtReader.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>

namespace geck::resource {

namespace {

    MapsTxt loadMapsTxt(GameResources& resources) {
        for (const char* path : { "data/maps.txt", "maps.txt" }) {
            if (const auto bytes = resources.files().readRawBytes(path); bytes.has_value()) {
                return parseMapsTxt(std::string(bytes->begin(), bytes->end()));
            }
        }
        return MapsTxt{};
    }

} // namespace

MapNameResolver::MapNameResolver(GameResources& resources)
    : _resources(resources)
    , _doc(loadMapsTxt(resources)) {
}

std::string MapNameResolver::fileNameOf(int mapIndex) const {
    const auto info = _doc.find(mapIndex);
    return info.has_value() ? info->mapName : std::string{};
}

std::string MapNameResolver::displayName(int mapIndex, int elevation) const {
    if (mapIndex < 0 || elevation < 0) {
        return {};
    }
    try {
        if (Msg* msg = _resources.repository().load<Msg>("text/english/game/map.msg"); msg != nullptr) {
            return msg->message(mapIndex * 3 + elevation + 200).text;
        }
    } catch (const std::exception& e) {
        spdlog::debug("MapNameResolver: map.msg unavailable: {}", e.what());
    }
    return {};
}

int MapNameResolver::indexOf(const std::string& mapFileName) const {
    const auto info = _doc.findByName(mapFileName); // findByName lowercases + normalizes internally
    return info.has_value() ? info->index : -1;
}

namespace {

    bool lessByFileNameCi(const MapName& lhs, const MapName& rhs) {
        const auto toLower = [](unsigned char c) { return std::tolower(c); };
        return std::lexicographical_compare(
            lhs.fileName.begin(), lhs.fileName.end(),
            rhs.fileName.begin(), rhs.fileName.end(),
            [&](unsigned char a, unsigned char b) { return toLower(a) < toLower(b); });
    }

} // namespace

std::vector<MapName> MapNameResolver::allMaps() const {
    std::vector<MapName> maps;
    for (const auto& section : _doc.sections) {
        if (section.index < 0) {
            continue; // skip non-Map sections and any negative sentinels
        }
        maps.push_back({ section.index, fileNameOf(section.index), displayName(section.index, 0) });
    }
    std::sort(maps.begin(), maps.end(), lessByFileNameCi);
    return maps;
}

std::string MapNameResolver::fileNameOfLookup(const std::string& lookupName) const {
    const auto info = _doc.findByLookupName(lookupName);
    return info.has_value() ? info->mapName : std::string{};
}

std::string MapNameResolver::lookupNameOf(const std::string& mapFileName) const {
    const auto info = _doc.findByName(mapFileName);
    return info.has_value() ? info->lookupName : std::string{};
}

} // namespace geck::resource
