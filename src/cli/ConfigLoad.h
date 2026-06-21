#pragma once

#include <initializer_list>
#include <string>

#include "resource/GameResources.h"

namespace geck::cli {

/// Load a Fallout text config (maps.txt / city.txt / worldmap.txt / quests.txt …) by trying each
/// candidate VFS path in turn and parsing the first that exists with `parse` (a callable taking the
/// file text and returning T). Returns a default-constructed T when none is mounted, so callers
/// degrade gracefully. One place for the "data/<x> then <x>, read bytes, parse" idiom the world
/// tools share.
template <typename T, typename Parse>
T loadConfig(resource::GameResources& resources, std::initializer_list<const char*> paths, Parse parse) {
    for (const char* path : paths) {
        if (const auto bytes = resources.files().readRawBytes(path); bytes.has_value()) {
            return parse(std::string(bytes->begin(), bytes->end()));
        }
    }
    return T{};
}

} // namespace geck::cli
