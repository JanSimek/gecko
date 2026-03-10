#include "MessageEnumUtils.h"

#include "../format/msg/Msg.h"

namespace geck {

std::optional<std::vector<std::string>> loadMessageRange(const Msg* msg, int firstId, size_t count) {
    if (!msg) {
        return std::nullopt;
    }

    const auto& messages = msg->getMessages();
    std::vector<std::string> values;
    values.reserve(count);

    for (size_t index = 0; index < count; ++index) {
        auto it = messages.find(firstId + static_cast<int>(index));
        if (it == messages.end() || it->second.text.empty()) {
            return std::nullopt;
        }

        values.push_back(it->second.text);
    }

    return values;
}

std::optional<std::vector<MessageEnumOption>> loadMessageOptions(const Msg* msg, std::span<const MessageEnumSpec> specs) {
    if (!msg) {
        return std::nullopt;
    }

    const auto& messages = msg->getMessages();
    std::vector<MessageEnumOption> options;
    options.reserve(specs.size());

    for (const MessageEnumSpec& spec : specs) {
        auto it = messages.find(spec.messageId);
        if (it == messages.end() || it->second.text.empty()) {
            return std::nullopt;
        }

        options.push_back({ spec.value, it->second.text });
    }

    return options;
}

} // namespace geck
