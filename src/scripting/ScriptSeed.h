#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace geck {

/// Luau's math.randomseed keeps a 31-bit seed, so every seed a script run gets is masked to it.
inline constexpr std::uint32_t SCRIPT_SEED_MASK = 0x7FFFFFFF;

/// The seed `--arg seed=N` names: an integer (any that fits 64 bits, negative included) masked to
/// the 31 bits a run keeps, or nullopt when the text is not an integer. Single runs and batches
/// read it the same way, so `--arg seed=N` reproduces either.
[[nodiscard]] std::optional<std::uint32_t> parseScriptSeed(std::string_view text);

/// A fresh seed for a run given none. Successive calls differ even where random_device repeats.
[[nodiscard]] std::uint32_t randomScriptSeed();

} // namespace geck
