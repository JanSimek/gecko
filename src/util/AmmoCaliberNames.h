#pragma once

#include <optional>
#include <string>
#include <vector>

namespace geck {

class Msg;

std::optional<std::vector<std::string>> ammoCaliberNamesFromProtoMsg(const Msg* protoMsg);

} // namespace geck
