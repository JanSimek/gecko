#include "scripting/ScriptSeed.h"

#include <atomic>
#include <charconv>
#include <random>

namespace geck {

std::optional<std::uint32_t> parseScriptSeed(const std::string& text) {
    std::int64_t value = 0;
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(value)) & SCRIPT_SEED_MASK;
}

std::uint32_t randomScriptSeed() {
    static std::atomic<std::uint32_t> counter{ 0 };
    std::random_device rd;
    return (rd() ^ counter.fetch_add(1)) & SCRIPT_SEED_MASK;
}

} // namespace geck
