#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace geck {

class Msg;

struct MessageEnumOption {
    int value;
    std::string label;
};

struct MessageEnumSpec {
    int value;
    int messageId;
};

std::optional<std::vector<std::string>> loadMessageRange(const Msg* msg, int firstId, size_t count);
std::optional<std::vector<MessageEnumOption>> loadMessageOptions(const Msg* msg, std::span<const MessageEnumSpec> specs);

} // namespace geck
