#pragma once

#include "format/gam/Gam.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>

namespace geck::resource {

/// data/vault13.gam (the GAME_GLOBAL_VARS dictionary: every GVAR by index, with its start value)
/// through the repository cache, or nullptr when it isn't mounted.
inline Gam* loadVault13Gam(GameResources& resources) {
    for (const char* path : { "data/vault13.gam", "vault13.gam" }) {
        try {
            if (Gam* gam = resources.repository().load<Gam>(path); gam != nullptr) {
                return gam;
            }
        } catch (const std::exception& e) {
            spdlog::debug("vault13.gam not at {}: {}", path, e.what()); // try the next candidate path
        }
    }
    return nullptr;
}

} // namespace geck::resource
