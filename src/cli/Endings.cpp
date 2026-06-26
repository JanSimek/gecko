#include "cli/Endings.h"

#include "cli/ConfigLoad.h"
#include "cli/GlobalVars.h" // loadGameGam
#include "format/endgame/EndgameTxt.h"
#include "format/gam/Gam.h"
#include "reader/endgame/EndgameTxtReader.h"
#include "resource/GameResources.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <ostream>
#include <string>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    // GVAR_* name for a gvar index, or "" when out of range / no dictionary.
    std::string gvarNameOf(Gam* gam, int gvar) {
        if (gam == nullptr || gvar < 0 || static_cast<std::size_t>(gvar) >= gam->gvarCount()) {
            return {};
        }
        return gam->gvarKey(static_cast<std::size_t>(gvar));
    }

} // namespace

int buildEndings(resource::GameResources& resources, std::ostream& out) {
    const EndgameTxt endgame = loadConfig<EndgameTxt>(resources, { "data/endgame.txt", "endgame.txt" },
        [](const std::string& text) { return parseEndgameTxt(text); });
    if (endgame.endings.empty()) {
        out << "{\"endings\":[],\"stats\":{\"endings\":0}}\n";
        return 1;
    }

    Gam* gam = loadGameGam(resources); // gvar name dictionary (may be null)

    auto array = ordered_json::array();
    for (const Ending& ending : endgame.endings) {
        const std::string name = gvarNameOf(gam, ending.gvar);
        const std::string condition = (name.empty() ? ("gvar " + std::to_string(ending.gvar)) : name)
            + " == " + std::to_string(ending.value);
        array.push_back({ { "gvar", ending.gvar },
            { "gvarName", name.empty() ? ordered_json(nullptr) : ordered_json(name) },
            { "value", ending.value }, { "condition", condition },
            { "art", ending.art }, { "narrator", ending.narrator },
            { "direction", ending.direction } });
    }

    ordered_json root;
    root["endings"] = std::move(array);
    root["stats"] = { { "endings", endgame.endings.size() } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
