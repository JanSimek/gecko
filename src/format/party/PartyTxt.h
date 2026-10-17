#pragma once

#include <string>
#include <vector>

/// @file
/// @brief Model for Fallout 2's `data/party.txt` — the companions and the combat options each allows.
///
/// An INI file with one `[Party Member N]` section per companion, read from 0 up to the first missing
/// section (fallout2-ce party_member.cc `partyMembersParseConfig`). Member 0 is the player and unused.
///
/// @verbatim
/// [Party Member 1]   ; pMMyron_PID
/// party_member_pid=16777376
/// area_attack_mode=always, sometimes, be_careful
/// distance=stay_close, charge, snipe, on_your_own, stay
/// level_minimum=6
/// level_up_every=4
/// level_pids=16777541,16777542,16777543,16777544
/// @endverbatim
///
/// The option lists name the choices the player may set in the companion's combat control panel.
/// A companion levels up every `level_up_every` player levels once the player reaches
/// `level_minimum`, swapping to the next proto in `level_pids` (-1 = none).
///
/// @see fallout2-ce `party_member.cc` (PartyMemberDescription)

namespace geck {

struct PartyMember {
    int index = 0; ///< N of `[Party Member N]`
    int pid = -1;  ///< party_member_pid
    std::vector<std::string> areaAttackMode;
    std::vector<std::string> attackWho;
    std::vector<std::string> bestWeapon;
    std::vector<std::string> chemUse;
    std::vector<std::string> distance;
    std::vector<std::string> runAwayMode;
    std::vector<std::string> disposition;
    int levelMinimum = 0;
    int levelUpEvery = 0;
    std::vector<int> levelPids; ///< the proto each level-up swaps to, in order
};

/// Parsed `data/party.txt`: the members the engine loads, in index order.
struct PartyTxt {
    std::vector<PartyMember> members;
};

} // namespace geck
