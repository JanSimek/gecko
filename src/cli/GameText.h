#pragma once

#include <string>
#include <vector>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// Message `id` of `text/english/game/<file>` (e.g. "pipboy.msg"), or "" when the file isn't mounted,
/// the id is negative or the file has no such message.
std::string gameMessage(resource::GameResources& resources, const std::string& file, int id);

/// The subtitle lines of an ending's narration, `text/english/cuts/<narrator>.txt`; empty when the
/// file isn't mounted.
std::vector<std::string> endingSubtitles(resource::GameResources& resources, const std::string& narrator);

} // namespace geck::cli
