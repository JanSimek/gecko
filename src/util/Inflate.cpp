#include "util/Inflate.h"

#include "reader/ReaderExceptions.h"

#include <zlib.h>

#include <format>
#include <string>

namespace geck {

bool isGzip(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 2 && bytes[0] == 0x1F && bytes[1] == 0x8B;
}

std::vector<std::uint8_t> inflateStream(std::span<const std::uint8_t> input, DeflateContainer container,
    std::optional<std::size_t> expectedSize) {
    // windowBits 15 reads exactly a zlib stream and 15 + 16 exactly a gzip one, so a foreign
    // container fails instead of half-inflating.
    const int windowBits = container == DeflateContainer::Gzip ? 15 + 16 : 15;
    z_stream zs{};
    if (inflateInit2(&zs, windowBits) != Z_OK) {
        throw ParseException("zlib inflateInit2 failed");
    }
    // zlib never writes through next_in; it is non-const only because ZLIB_CONST is not defined.
    zs.next_in = const_cast<Bytef*>(input.data());
    zs.avail_in = static_cast<uInt>(input.size());

    std::vector<std::uint8_t> out;
    out.reserve(expectedSize.value_or(input.size() * 4));
    std::vector<std::uint8_t> chunk(std::size_t{ 1 } << 16);
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        zs.next_out = chunk.data();
        zs.avail_out = static_cast<uInt>(chunk.size());
        rc = inflate(&zs, Z_NO_FLUSH);
        const bool failed = rc != Z_OK && rc != Z_STREAM_END;
        const bool truncated = rc == Z_OK && zs.avail_in == 0 && zs.avail_out != 0;
        if (failed || truncated) {
            inflateEnd(&zs);
            const char* name = container == DeflateContainer::Gzip ? "gzip" : "zlib";
            throw ParseException(truncated ? std::format("{} stream is truncated", name)
                                           : std::format("{} inflate failed (zlib result {})", name, rc));
        }
        out.insert(out.end(), chunk.data(), chunk.data() + (chunk.size() - zs.avail_out));
    }
    inflateEnd(&zs);

    if (expectedSize.has_value() && out.size() != *expectedSize) {
        throw ParseException(std::format("inflated {} bytes, expected {}", out.size(), *expectedSize));
    }
    return out;
}

} // namespace geck
