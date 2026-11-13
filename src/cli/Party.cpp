#include "cli/Party.h"

#include "resource/ConfigLoad.h"
#include "cli/ProtoName.h"
#include "format/party/PartyTxt.h"
#include "reader/party/PartyTxtReader.h"
#include "resource/GameResources.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <ostream>
#include <string>

namespace geck::cli {

namespace {

    using ordered_json = nlohmann::ordered_json;

    ordered_json protoJson(resource::GameResources& resources, int pid) {
        const std::string name = protoDisplayName(resources, static_cast<uint32_t>(pid));
        return { { "pid", pid }, { "name", name.empty() ? ordered_json(nullptr) : ordered_json(name) } };
    }

} // namespace

int buildParty(resource::GameResources& resources, std::ostream& out) {
    const PartyTxt party = resource::loadConfig<PartyTxt>(resources, { "data/party.txt", "party.txt" },
        [](const std::string& text) { return parsePartyTxt(text); });
    if (party.members.empty()) {
        out << "{\"members\":[],\"stats\":{\"members\":0}}\n";
        return 1;
    }

    auto members = ordered_json::array();
    for (const PartyMember& member : party.members) {
        if (member.index == 0) {
            continue; // the player; partyMemberGetDescription starts at 1
        }
        ordered_json entry = protoJson(resources, member.pid);
        entry["index"] = member.index;
        entry["options"] = { { "areaAttackMode", member.areaAttackMode }, { "attackWho", member.attackWho },
            { "bestWeapon", member.bestWeapon }, { "chemUse", member.chemUse }, { "distance", member.distance },
            { "runAwayMode", member.runAwayMode }, { "disposition", member.disposition } };
        entry["levelMinimum"] = member.levelMinimum;
        entry["levelUpEvery"] = member.levelUpEvery;
        auto levels = ordered_json::array();
        for (const int pid : member.levelPids) {
            if (pid != -1) {
                levels.push_back(protoJson(resources, pid));
            }
        }
        entry["levels"] = std::move(levels);
        members.push_back(std::move(entry));
    }

    ordered_json root;
    root["members"] = members;
    root["stats"] = { { "members", members.size() } };
    out << root.dump(2, ' ', false, ordered_json::error_handler_t::replace) << "\n";
    return 0;
}

} // namespace geck::cli
