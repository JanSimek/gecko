#include "AmmoCaliberNames.h"

#include "FalloutEngineEnums.h"
#include "../format/msg/Msg.h"

namespace geck {
namespace {

    constexpr int FirstCaliberMessageId = fallout::protoMessageId(fallout::CaliberType::None);
    constexpr size_t CaliberMessageCount = fallout::enumCount<fallout::CaliberType>();

} // namespace

std::optional<std::vector<std::string>> ammoCaliberNamesFromProtoMsg(const Msg* protoMsg) {
    if (!protoMsg) {
        return std::nullopt;
    }

    const auto& messages = protoMsg->getMessages();
    std::vector<std::string> names;
    names.reserve(CaliberMessageCount);

    for (size_t index = 0; index < CaliberMessageCount; ++index) {
        auto it = messages.find(FirstCaliberMessageId + static_cast<int>(index));
        if (it == messages.end() || it->second.text.empty()) {
            return std::nullopt;
        }

        names.push_back(it->second.text);
    }

    return names;
}

} // namespace geck
