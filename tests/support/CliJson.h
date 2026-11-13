#pragma once

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace geck::test {

/// Runs a JSON-printing CLI command, `command(std::ostream&) -> int`, and returns its parsed output.
/// With no `rc` the command must succeed; with one, its exit code is stored there and a failed run
/// returns null instead of parsing. `output`, when given, receives the raw text either way.
template <class F>
nlohmann::json runJson(F&& command, int* rc = nullptr, std::string* output = nullptr) {
    std::ostringstream out;
    const int code = std::forward<F>(command)(static_cast<std::ostream&>(out));
    const std::string text = out.str();
    INFO("tool output: " << text);
    if (output != nullptr) {
        *output = text;
    }
    if (rc != nullptr) {
        *rc = code;
        if (code != 0) {
            return {};
        }
    }
    REQUIRE(code == 0);
    return nlohmann::json::parse(text);
}

} // namespace geck::test
