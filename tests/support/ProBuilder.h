#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>

#include "format/pro/Pro.h"
#include "reader/pro/ProReader.h"
#include "writer/pro/ProWriter.h"

#include "support/TempFile.h"

namespace geck::test {

/// Writes `pro` to `path` with ProWriter (creating the parent directories); closed when it returns.
inline void writePro(const std::filesystem::path& path, const geck::Pro& pro) {
    geck::ProWriter writer{};
    writer.openFile(path);
    REQUIRE(writer.write(pro));
}

/// A Pro written by ProWriter and read back by ProReader, with the size of the file in between.
struct ProRoundTrip {
    geck::Pro pro;
    std::uintmax_t fileSize;
};

/// Writes `original` to a temp file, reads it back, and returns the reparsed copy and the file size.
/// Collapses the write-then-read block every PRO round-trip case repeats.
inline ProRoundTrip proRoundTrip(const geck::Pro& original) {
    const TempFile file{ "geck_pro_roundtrip", ".pro" };
    writePro(file.path(), original);
    geck::ProReader reader{};
    auto reparsed = reader.openFile(file.path());
    REQUIRE(reparsed != nullptr);
    return { *reparsed, std::filesystem::file_size(file.path()) };
}

} // namespace geck::test
