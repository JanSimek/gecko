#include "cli/Quests.h"

#include "resource/ConfigLoad.h"
#include "cli/GameText.h"
#include "cli/JsonText.h"
#include "resource/GameVarsLoader.h"
#include "format/gam/Gam.h"
#include "format/quests/QuestsTxt.h"
#include "reader/quests/QuestsTxtReader.h"
#include "resource/GameResources.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <ostream>
#include <string>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;
} // namespace

int buildQuests(resource::GameResources& resources, std::ostream& out) {
    const QuestsTxt quests = resource::loadConfig<QuestsTxt>(resources, { "data/quests.txt", "quests.txt" },
        [](const std::string& text) { return parseQuestsTxt(text); });
    if (quests.quests.empty()) {
        out << "{\"quests\":[],\"stats\":{\"quests\":0}}\n";
        return 1;
    }

    Gam* gam = resource::loadVault13Gam(resources); // gvar name/default dictionary (may be null)

    auto array = ordered_json::array();
    for (const Quest& quest : quests.quests) {
        ordered_json gvarName = nullptr;
        ordered_json gvarStart = nullptr;
        if (gam != nullptr && quest.gvar >= 0 && static_cast<std::size_t>(quest.gvar) < gam->gvarCount()) {
            const auto gvarIndex = static_cast<std::size_t>(quest.gvar);
            gvarName = gam->gvarKey(gvarIndex);
            gvarStart = gam->gvarValue(gvarIndex);
        }
        array.push_back({ { "location", quest.location },
            { "area", orNull(gameMessage(resources, "map.msg", quest.location)) },
            { "gvar", quest.gvar }, { "gvarName", gvarName }, { "gvarStart", gvarStart },
            { "displayThreshold", quest.displayThreshold }, { "completedThreshold", quest.completedThreshold },
            { "description", orNull(gameMessage(resources, "quests.msg", quest.description)) } });
    }

    ordered_json root;
    root["quests"] = std::move(array);
    root["stats"] = { { "quests", quests.quests.size() } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
