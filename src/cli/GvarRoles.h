#pragma once

#include <nlohmann/json.hpp>

#include <map>

namespace geck::resource {
class GameResources;
}

namespace geck::cli {

/// What the engine's own data files say each global variable means, keyed by gvar index: a list of
/// roles such as {"kind":"quest", …}, {"kind":"ending", …}, {"kind":"deathEnding", …},
/// {"kind":"karma", …} and {"kind":"holodisk", …}, read from quests.txt, endgame.txt, enddeath.txt,
/// karmavar.txt and holodisk.txt with their .msg text. Files that aren't mounted add nothing.
std::map<int, nlohmann::ordered_json> buildGvarRoles(resource::GameResources& resources);

} // namespace geck::cli
