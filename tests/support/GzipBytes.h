#pragma once

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include "support/TempFile.h" // readAllBytes

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace geck::test {

/// gzip-compress `raw` the way the engine stores maps in a save slot (fileCopyCompressed).
// zlib's next_in is not const, so the input is taken by value.
inline std::vector<uint8_t> gzipBytes(std::vector<uint8_t> raw) {
    z_stream zs{};
    REQUIRE(deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    std::vector<uint8_t> out(deflateBound(&zs, static_cast<uLong>(raw.size())) + 32);
    zs.next_in = raw.data();
    zs.avail_in = static_cast<uInt>(raw.size());
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());
    REQUIRE(deflate(&zs, Z_FINISH) == Z_STREAM_END);
    out.resize(zs.total_out);
    deflateEnd(&zs);
    return out;
}

inline void writeAllBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace geck::test
