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

    std::string toLower(std::string s) {
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

} // namespace

MapNameResolver::MapNameResolver(GameResources& resources)
    : _resources(resources)
    , _mapsTxt(loadMapsTxt(resources)) {
}

std::string MapNameResolver::fileNameOf(int mapIndex) const {
    const MapInfo* info = _mapsTxt.find(mapIndex);
    return info != nullptr ? info->mapName : std::string{};
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
    const MapInfo* info = _mapsTxt.findByName(toLower(mapFileName));
    return info != nullptr ? info->index : -1;
}

std::string MapNameResolver::fileNameOfLookup(const std::string& lookupName) const {
    const MapInfo* info = _mapsTxt.findByLookupName(lookupName);
    return info != nullptr ? info->mapName : std::string{};
}

std::string MapNameResolver::lookupNameOf(const std::string& mapFileName) const {
    const MapInfo* info = _mapsTxt.findByName(toLower(mapFileName));
    return info != nullptr ? info->lookupName : std::string{};
}

} // namespace geck::resource
