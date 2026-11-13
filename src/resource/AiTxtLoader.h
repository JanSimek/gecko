#pragma once

#include "format/ai/AiPacket.h"
#include "reader/ai/AiTxtReader.h"
#include "resource/ConfigLoad.h"
#include "resource/GameResources.h"

#include <string>

namespace geck::resource {

/// Load `data/ai.txt` (or a bare `ai.txt`) from the mounted game data into an AiTxt. Tries the canonical
/// path and a bare fallback for odd mounts; returns an empty AiTxt when the file isn't present, so
/// callers fall back to the raw packet number rather than inventing labels.
inline AiTxt loadAiTxt(GameResources& resources) {
    return loadConfig<AiTxt>(resources, { "data/ai.txt", "ai.txt" }, [](const std::string& text) { return parseAiTxt(text); });
}

} // namespace geck::resource
