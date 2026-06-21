#include "cli/Quests.h"

#include "cli/ConfigLoad.h"
#include "cli/GlobalVars.h" // loadGameGam
#include "format/gam/Gam.h"
#include "format/msg/Msg.h"
#include "format/quests/QuestsTxt.h"
#include "reader/quests/QuestsTxtReader.h"
#include "resource/GameResources.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <ostream>
#include <string>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    // Message `index` from the .msg at `path`, or "" if unavailable. quests.txt indexes map.msg
    // (location names, the 1500+ range) and quests.msg (descriptions) directly by these ids.
    std::string msgText(resource::GameResources& resources, const char* path, int index) {
        if (index < 0) {
            return {};
        }
        try {
            if (Msg* msg = resources.repository().load<Msg>(path); msg != nullptr) {
                return msg->message(index).text;
            }
        } catch (const std::exception& e) {
            spdlog::debug("quests: {} unavailable: {}", path, e.what());
        }
        return {};
    }

    ordered_json orNull(const std::string& s) {
        return s.empty() ? ordered_json(nullptr) : ordered_json(s);
    }

} // namespace

int buildQuests(resource::GameResources& resources, std::ostream& out) {
    const QuestsTxt quests = loadConfig<QuestsTxt>(resources, { "data/quests.txt", "quests.txt" },
        [](const std::string& text) { return parseQuestsTxt(text); });
    if (quests.quests.empty()) {
        out << "{\"quests\":[],\"stats\":{\"quests\":0}}\n";
        return 1;
    }

    Gam* gam = loadGameGam(resources); // gvar name/default dictionary (may be null)

    auto array = ordered_json::array();
    for (const Quest& quest : quests.quests) {
        ordered_json gvarName = nullptr;
        ordered_json gvarStart = nullptr;
        if (gam != nullptr && quest.gvar >= 0 && static_cast<std::size_t>(quest.gvar) < gam->gvarCount()) {
            gvarName = gam->gvarKey(quest.gvar);
            gvarStart = gam->gvarValue(quest.gvar);
        }
        array.push_back({ { "location", quest.location },
            { "area", orNull(msgText(resources, "text/english/game/map.msg", quest.location)) },
            { "gvar", quest.gvar }, { "gvarName", gvarName }, { "gvarStart", gvarStart },
            { "displayThreshold", quest.displayThreshold }, { "completedThreshold", quest.completedThreshold },
            { "description", orNull(msgText(resources, "text/english/game/quests.msg", quest.description)) } });
    }

    ordered_json root;
    root["quests"] = std::move(array);
    root["stats"] = { { "quests", quests.quests.size() } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
