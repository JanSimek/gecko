#pragma once

#include "format/ai/AiPacket.h"
#include "reader/ai/AiTxtReader.h"
#include "resource/GameResources.h"

#include <string>

namespace geck::resource {

/// Load `data/ai.txt` (or a bare `ai.txt`) from the mounted game data into an AiTxt. Tries the canonical
/// path and a bare fallback for odd mounts; returns an empty AiTxt when the file isn't present, so
/// callers fall back to the raw packet number rather than inventing labels.
inline AiTxt loadAiTxt(GameResources& resources) {
    for (const char* path : { "data/ai.txt", "ai.txt" }) {
        if (const auto bytes = resources.files().readRawBytes(path); bytes.has_value()) {
            return parseAiTxt(std::string(bytes->begin(), bytes->end()));
        }
    }
    return AiTxt{};
}

} // namespace geck::resource
