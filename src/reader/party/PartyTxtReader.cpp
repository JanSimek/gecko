#include "reader/party/PartyTxtReader.h"

#include "reader/IniParser.h"
#include "reader/TextParsing.h"

#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace geck {

namespace {

    using geck::text::intOr;
    using geck::text::splitCsv;
    using geck::text::toLower;
    using geck::text::trim;

    constexpr int MAX_LEVEL_PIDS = 10; // fallout2-ce PARTY_MEMBER_MAX_LEVEL

    // The raw keys of one [Party Member N] section; resolved after the whole file is read because
    // the engine only reads the level fields when level_up_every is present.
    using Section = std::map<std::string, std::string, std::less<>>;

    std::vector<std::string> optionList(const Section& section, const std::string& key) {
        std::vector<std::string> out;
        if (const auto it = section.find(key); it != section.end()) {
            for (const auto& value : splitCsv(it->second)) {
                if (!value.empty()) {
                    out.push_back(toLower(value));
                }
            }
        }
        return out;
    }

    std::vector<int> levelPids(const std::string& value) {
        std::vector<int> pids;
        for (const auto& pid : splitCsv(value)) {
            if (static_cast<int>(pids.size()) == MAX_LEVEL_PIDS) {
                break;
            }
            pids.push_back(intOr(pid, 0));
        }
        return pids;
    }

    PartyMember toMember(int index, const Section& section) {
        PartyMember member;
        member.index = index;
        member.pid = intOr(section.at("party_member_pid"), -1);
        member.areaAttackMode = optionList(section, "area_attack_mode");
        member.attackWho = optionList(section, "attack_who");
        member.bestWeapon = optionList(section, "best_weapon");
        member.chemUse = optionList(section, "chem_use");
        member.distance = optionList(section, "distance");
        member.runAwayMode = optionList(section, "run_away_mode");
        member.disposition = optionList(section, "disposition");
        if (const auto every = section.find("level_up_every"); every != section.end()) {
            member.levelUpEvery = intOr(every->second, 0);
            if (const auto minimum = section.find("level_minimum"); minimum != section.end()) {
                member.levelMinimum = intOr(minimum->second, 0);
            }
            if (const auto pids = section.find("level_pids"); pids != section.end()) {
                member.levelPids = levelPids(pids->second);
            }
        }
        return member;
    }

} // namespace

PartyTxt parsePartyTxt(std::istream& in) {
    std::map<int, Section> sections;
    Section* current = nullptr;
    ini::parse(
        in,
        [&sections, &current](const std::string& section) {
            const std::string prefix = "party member ";
            const std::string lower = toLower(section);
            current = lower.rfind(prefix, 0) == 0 ? &sections[intOr(trim(lower.substr(prefix.size())), -1)] : nullptr;
        },
        [&current](const std::string& key, const std::string& value) {
            if (current != nullptr) {
                (*current)[key] = value;
            }
        });

    PartyTxt out;
    for (int index = 0;; ++index) {
        const auto it = sections.find(index);
        if (it == sections.end() || !it->second.contains("party_member_pid")) {
            break;
        }
        out.members.push_back(toMember(index, it->second));
    }
    return out;
}

PartyTxt parsePartyTxt(const std::string& text) {
    std::istringstream in(text);
    return parsePartyTxt(in);
}

} // namespace geck
