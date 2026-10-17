#include "cli/GameText.h"

#include "format/msg/Msg.h"
#include "reader/endgame/EndDeathTxtReader.h"
#include "resource/GameResources.h"

#include <spdlog/spdlog.h>

#include <exception>

namespace geck::cli {

std::string gameMessage(resource::GameResources& resources, const std::string& file, int id) {
    if (id < 0) {
        return {};
    }
    const std::string path = "text/english/game/" + file;
    try {
        if (const Msg* msg = resources.repository().tryLoad<Msg>(path); msg != nullptr) {
            const auto& messages = msg->getMessages();
            if (const auto it = messages.find(id); it != messages.end()) {
                return it->second.text;
            }
        }
    } catch (const std::exception& e) {
        spdlog::debug("{} unavailable: {}", path, e.what());
    }
    return {};
}

std::vector<std::string> endingSubtitles(resource::GameResources& resources, const std::string& narrator) {
    if (narrator.empty()) {
        return {};
    }
    const auto bytes = resources.files().readRawBytes("text/english/cuts/" + narrator + ".txt");
    if (!bytes.has_value()) {
        return {};
    }
    return parseEndingSubtitles(std::string(bytes->begin(), bytes->end()));
}

} // namespace geck::cli
