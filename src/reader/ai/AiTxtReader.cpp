#include "reader/ai/AiTxtReader.h"

#include "reader/TextParsing.h"

#include <sstream>

namespace geck {

namespace {

    using geck::text::toLower;
    using geck::text::trim;

    int parseIntOr(const std::string& value, int fallback) {
        try {
            return std::stoi(value);
        } catch (const std::exception&) {
            return fallback; // a non-numeric value just leaves the field at its default
        }
    }

    // Apply one key=value to the in-progress packet. Only behaviour-relevant keys are recognized;
    // everything else (animation ranges, fonts, colours, …) is ignored.
    void applyField(AiPacket& packet, const std::string& key, const std::string& value) {
        if (key == "packet_num") {
            packet.packetNum = parseIntOr(value, packet.packetNum);
        } else if (key == "aggression") {
            packet.aggression = parseIntOr(value, packet.aggression);
        } else if (key == "secondary_freq") {
            packet.secondaryFreq = parseIntOr(value, packet.secondaryFreq);
        } else if (key == "disposition") {
            packet.disposition = value;
        } else if (key == "run_away_mode") {
            packet.runAwayMode = value;
        } else if (key == "area_attack_mode") {
            packet.areaAttackMode = value;
        } else if (key == "best_weapon") {
            packet.bestWeapon = value;
        } else if (key == "distance") {
            packet.distance = value;
        }
    }

} // namespace

AiTxt parseAiTxt(std::istream& in) {
    AiTxt out;
    AiPacket current;
    bool inSection = false;
    const auto flush = [&] {
        // A packet is only usable if it declared the number critters reference.
        if (inSection && current.packetNum >= 0) {
            out.add(current);
        }
    };

    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == ';') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            flush();
            current = AiPacket{};
            current.name = trim(trimmed.substr(1, trimmed.size() - 2));
            inSection = true;
            continue;
        }
        if (!inSection) {
            continue; // a stray key before the first section
        }
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        applyField(current, toLower(trim(trimmed.substr(0, eq))), trim(trimmed.substr(eq + 1)));
    }
    flush();
    return out;
}

AiTxt parseAiTxt(const std::string& text) {
    std::istringstream in(text);
    return parseAiTxt(in);
}

} // namespace geck
