#include "resource/MapNameResolver.h"

#include "format/msg/Msg.h"
#include "reader/maps/MapsTxtDocumentReader.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>

namespace geck::resource {

namespace {

    MapsTxtDocument loadMapsTxt(GameResources& resources) {
        for (const char* path : { "data/maps.txt", "maps.txt" }) {
            if (const auto bytes = resources.files().readRawBytes(path); bytes.has_value()) {
                return parseMapsTxtDocument(std::string(bytes->begin(), bytes->end()));
            }
        }
        return MapsTxtDocument{};
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

std::string MapNameResolver::fileNameOfLookup(const std::string& lookupName) const {
    const auto info = _doc.findByLookupName(lookupName);
    return info.has_value() ? info->mapName : std::string{};
}

std::string MapNameResolver::lookupNameOf(const std::string& mapFileName) const {
    const auto info = _doc.findByName(mapFileName);
    return info.has_value() ? info->lookupName : std::string{};
}

} // namespace geck::resource
