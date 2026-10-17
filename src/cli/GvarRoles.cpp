#include "cli/GvarRoles.h"

#include "resource/ConfigLoad.h"
#include "cli/GameText.h"
#include "format/endgame/EndDeathTxt.h"
#include "format/endgame/EndgameTxt.h"
#include "format/holodisk/HolodiskTxt.h"
#include "format/karma/KarmaVarTxt.h"
#include "format/quests/QuestsTxt.h"
#include "reader/endgame/EndDeathTxtReader.h"
#include "reader/endgame/EndgameTxtReader.h"
#include "reader/holodisk/HolodiskTxtReader.h"
#include "reader/karma/KarmaVarTxtReader.h"
#include "reader/quests/QuestsTxtReader.h"
#include "resource/GameResources.h"

#include <string>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    ordered_json orNull(const std::string& s) {
        return s.empty() ? ordered_json(nullptr) : ordered_json(s);
    }

    void add(std::map<int, ordered_json>& roles, int gvar, ordered_json role) {
        if (gvar < 0) {
            return;
        }
        auto& list = roles[gvar];
        if (list.is_null()) {
            list = ordered_json::array();
        }
        list.push_back(std::move(role));
    }

} // namespace

std::map<int, ordered_json> buildGvarRoles(resource::GameResources& resources) {
    std::map<int, ordered_json> roles;

    const auto quests = resource::loadConfig<QuestsTxt>(resources, { "data/quests.txt", "quests.txt" },
        [](const std::string& text) { return parseQuestsTxt(text); });
    for (const Quest& quest : quests.quests) {
        add(roles, quest.gvar, { { "kind", "quest" }, { "description", orNull(gameMessage(resources, "quests.msg", quest.description)) },
                                   { "displayThreshold", quest.displayThreshold }, { "completedThreshold", quest.completedThreshold } });
    }

    const auto endgame = resource::loadConfig<EndgameTxt>(resources, { "data/endgame.txt", "endgame.txt" },
        [](const std::string& text) { return parseEndgameTxt(text); });
    for (const Ending& ending : endgame.endings) {
        add(roles, ending.gvar, { { "kind", "ending" }, { "value", ending.value }, { "narrator", ending.narrator } });
    }

    const auto endDeath = resource::loadConfig<EndDeathTxt>(resources, { "data/enddeath.txt", "enddeath.txt" },
        [](const std::string& text) { return parseEndDeathTxt(text); });
    for (const DeathEnding& ending : endDeath.endings) {
        add(roles, ending.gvar, { { "kind", "deathEnding" }, { "droppedAt", ending.value }, { "narrator", ending.narrator } });
    }

    // Shown on the character screen while the gvar is non-zero (fallout2-ce character_editor.cc).
    const auto karma = resource::loadConfig<KarmaVarTxt>(resources, { "data/karmavar.txt", "karmavar.txt" },
        [](const std::string& text) { return parseKarmaVarTxt(text); });
    for (const KarmaVar& entry : karma.entries) {
        add(roles, entry.gvar, { { "kind", "karma" }, { "title", orNull(gameMessage(resources, "editor.msg", entry.name)) },
                                   { "description", orNull(gameMessage(resources, "editor.msg", entry.description)) } });
    }

    // Listed in the Pip-Boy while the gvar is non-zero (fallout2-ce pipboy.cc).
    const auto holodisks = resource::loadConfig<HolodiskTxt>(resources, { "data/holodisk.txt", "holodisk.txt" },
        [](const std::string& text) { return parseHolodiskTxt(text); });
    for (const Holodisk& holodisk : holodisks.holodisks) {
        add(roles, holodisk.gvar, { { "kind", "holodisk" }, { "title", orNull(gameMessage(resources, "pipboy.msg", holodisk.title)) },
                                      { "textMessage", holodisk.text } });
    }

    return roles;
}

} // namespace geck::cli
