#include "cli/Endings.h"

#include "resource/ConfigLoad.h"
#include "cli/GameText.h"
#include "cli/JsonText.h"
#include "resource/GameVarsLoader.h"
#include "format/endgame/EndDeathTxt.h"
#include "format/endgame/EndgameTxt.h"
#include "format/gam/Gam.h"
#include "reader/endgame/EndDeathTxtReader.h"
#include "reader/endgame/EndgameTxtReader.h"
#include "resource/GameResources.h"
#include "resource/MapNameResolver.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <format>
#include <ostream>
#include <string>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    // endgameSetupDeathEnding plays this death ending, skipping the random pick, when the player dies
    // with GVAR_MODOC_SHITTY_DEATH (491) set (fallout2-ce endgame.cc).
    constexpr std::size_t MODOC_DEATH_ENDING = 12;
    constexpr int GVAR_MODOC_SHITTY_DEATH = 491;

    // GVAR_* name for a gvar index, or "" when out of range / no dictionary.
    std::string gvarNameOf(Gam* gam, int gvar) {
        if (gam == nullptr || gvar < 0 || static_cast<std::size_t>(gvar) >= gam->gvarCount()) {
            return {};
        }
        return gam->gvarKey(static_cast<std::size_t>(gvar));
    }

    // The gvar's name, or "gvar N" without a dictionary.
    std::string gvarLabel(Gam* gam, int gvar) {
        const std::string name = gvarNameOf(gam, gvar);
        return name.empty() ? std::format("gvar {}", gvar) : name;
    }

    // A city.txt area condition as {index, name}, or null for -1 (no check).
    ordered_json areaJson(const resource::MapNameResolver& names, int area) {
        if (area < 0) {
            return nullptr;
        }
        return { { "index", area }, { "name", orNull(names.areaName(area)) } };
    }

    ordered_json slidesJson(resource::GameResources& resources, Gam* gam, const EndgameTxt& endgame) {
        auto array = ordered_json::array();
        for (const Ending& ending : endgame.endings) {
            array.push_back({ { "gvar", ending.gvar }, { "gvarName", orNull(gvarNameOf(gam, ending.gvar)) },
                { "value", ending.value }, { "condition", std::format("{} == {}", gvarLabel(gam, ending.gvar), ending.value) },
                { "art", ending.art }, { "narrator", ending.narrator }, { "direction", ending.direction },
                { "subtitles", endingSubtitles(resources, ending.narrator) } });
        }
        return array;
    }

    ordered_json deathEndingsJson(resource::GameResources& resources, Gam* gam, const EndDeathTxt& endDeath) {
        const resource::MapNameResolver names(resources);
        auto array = ordered_json::array();
        for (std::size_t i = 0; i < endDeath.endings.size(); ++i) {
            const DeathEnding& ending = endDeath.endings[i];
            ordered_json entry = { { "index", i }, { "gvar", ending.gvar },
                { "gvarName", orNull(gvarNameOf(gam, ending.gvar)) }, { "value", ending.value },
                // The engine drops the line once the gvar reaches the value, whatever enddeath.txt's
                // header says (endgameDeathEndingValidate).
                { "condition", ending.gvar < 0 ? ordered_json(nullptr)
                                               : ordered_json(std::format("{} < {}", gvarLabel(gam, ending.gvar), ending.value)) },
                { "areaKnown", areaJson(names, ending.areaKnown) }, { "areaNotKnown", areaJson(names, ending.areaNotKnown) },
                { "minLevel", ending.minLevel }, { "percentage", ending.percentage }, { "narrator", ending.narrator },
                { "subtitles", endingSubtitles(resources, ending.narrator) } };
            if (i == MODOC_DEATH_ENDING) {
                entry["forcedWhen"] = std::format("{} != 0", gvarLabel(gam, GVAR_MODOC_SHITTY_DEATH));
            }
            array.push_back(std::move(entry));
        }
        return array;
    }

} // namespace

int buildEndings(resource::GameResources& resources, std::ostream& out) {
    const EndgameTxt endgame = resource::loadConfig<EndgameTxt>(resources, { "data/endgame.txt", "endgame.txt" },
        [](const std::string& text) { return parseEndgameTxt(text); });
    const EndDeathTxt endDeath = resource::loadConfig<EndDeathTxt>(resources, { "data/enddeath.txt", "enddeath.txt" },
        [](const std::string& text) { return parseEndDeathTxt(text); });
    if (endgame.endings.empty() && endDeath.endings.empty()) {
        out << "{\"endings\":[],\"deathEndings\":[],\"stats\":{\"endings\":0,\"deathEndings\":0}}\n";
        return 1;
    }

    Gam* gam = resource::loadVault13Gam(resources); // gvar name dictionary (may be null)

    ordered_json root;
    root["endings"] = slidesJson(resources, gam, endgame);
    root["deathEndings"] = deathEndingsJson(resources, gam, endDeath);
    root["stats"] = { { "endings", endgame.endings.size() }, { "deathEndings", endDeath.endings.size() } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
