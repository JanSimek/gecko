#pragma once

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "format/pro/Pro.h"
#include "reader/pro/ProReader.h"
#include "writer/pro/ProWriter.h"

namespace geck::test {

/// Writes a Pro to `tempPath`, reads it back, and returns the reparsed copy.
/// Collapses the write-then-read block every PRO round-trip case repeats.
inline geck::Pro proRoundTrip(const geck::Pro& original, const std::filesystem::path& tempPath) {
    {
        geck::ProWriter writer{};
        writer.openFile(tempPath);
        REQUIRE(writer.write(original));
    } // writer destructor closes/flushes the stream before we read it back

    geck::ProReader reader{};
    auto reparsed = reader.openFile(tempPath);
    REQUIRE(reparsed != nullptr);
    return *reparsed;
}

} // namespace geck::test
