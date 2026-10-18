#pragma once

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "format/pro/Pro.h"
#include "reader/pro/ProReader.h"
#include "writer/pro/ProWriter.h"

namespace geck::test {

/// Writes `pro` to `path` with ProWriter (creating the parent directories); closed when it returns.
inline void writePro(const std::filesystem::path& path, const geck::Pro& pro) {
    geck::ProWriter writer{};
    writer.openFile(path);
    REQUIRE(writer.write(pro));
}

/// Writes a Pro to `tempPath`, reads it back, and returns the reparsed copy.
/// Collapses the write-then-read block every PRO round-trip case repeats.
inline geck::Pro proRoundTrip(const geck::Pro& original, const std::filesystem::path& tempPath) {
    writePro(tempPath, original);
    geck::ProReader reader{};
    auto reparsed = reader.openFile(tempPath);
    REQUIRE(reparsed != nullptr);
    return *reparsed;
}

} // namespace geck::test
