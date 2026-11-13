#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace geck::cli {

/// `s` as a JSON string, or null when it is empty: a name the mounted data does not provide.
inline nlohmann::ordered_json orNull(const std::string& s) {
    return s.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(s);
}

} // namespace geck::cli
