#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace geck {

/// The container around a deflate stream: DAT2 entries are zlib streams, save-slot maps are gzip
/// files (fallout2-ce loadsave.cc writes them with fileCopyCompressed).
enum class DeflateContainer {
    Zlib,
    Gzip,
};

/// True when `bytes` start with the gzip magic.
[[nodiscard]] bool isGzip(std::span<const std::uint8_t> bytes);

/// Inflates a whole stream. Throws ParseException for a corrupt, truncated or foreign stream, and
/// when `expectedSize` is given but the output has another length.
[[nodiscard]] std::vector<std::uint8_t> inflateStream(std::span<const std::uint8_t> input, DeflateContainer container,
    std::optional<std::size_t> expectedSize = std::nullopt);

} // namespace geck
