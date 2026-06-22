#pragma once

#include <string>
#include <unordered_map>

namespace geck {

/// One AI packet from `data/ai.txt` — the behaviour the engine drives a critter with. A critter's
/// `ai_packet` field is the `packet_num` here. Only the purpose-relevant fields are kept (the file
/// also carries animation frame ranges, fonts, colours, etc. that say nothing about behaviour).
/// String fields hold the engine's own labels (`coward`/`berserk`, `snipe`/`charge`, …) verbatim —
/// no remapping, per the engine-fidelity rule.
struct AiPacket {
    int packetNum = -1;         ///< matches MapObject::ai_packet
    std::string name;           ///< the ai.txt section header, e.g. "Aggressive - Tough"
    int aggression = 0;         ///< higher = more likely to engage
    std::string disposition;    ///< coward / defensive / aggressive / berserk / custom / none
    std::string runAwayMode;    ///< when it flees (none/coward/…)
    std::string areaAttackMode; ///< burst/grenade targeting policy (often absent)
    std::string bestWeapon;     ///< preferred weapon class (melee/ranged/…)
    std::string distance;       ///< preferred engagement distance (stay/charge/snipe/…)
    int secondaryFreq = 0;      ///< how often it uses its secondary attack
};

/// `data/ai.txt` parsed into packets keyed by `packet_num`. Built by parseAiTxt(); read-only after.
class AiTxt {
public:
    void add(AiPacket packet) { _byNum[packet.packetNum] = std::move(packet); }

    /// The packet a critter's `ai_packet` references, or nullptr if the file defines no such number.
    [[nodiscard]] const AiPacket* byPacketNum(int packetNum) const {
        const auto it = _byNum.find(packetNum);
        return it == _byNum.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t size() const { return _byNum.size(); }

    /// All packets keyed by packet_num — for enumerating them (e.g. populating a picker).
    [[nodiscard]] const std::unordered_map<int, AiPacket>& packets() const { return _byNum; }

private:
    std::unordered_map<int, AiPacket> _byNum;
};

} // namespace geck
