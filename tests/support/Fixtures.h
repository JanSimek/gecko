#pragma once

#include <filesystem>
#include <string>

// GECK_TEST_DATA_DIR is injected per test target via target_compile_definitions
// and points at the in-source tests/data tree. Resolving fixtures by absolute
// path makes tests independent of the working directory (running the binary
// directly, from a debugger, or an IDE no longer silently fails to find data).
#ifndef GECK_TEST_DATA_DIR
#error "GECK_TEST_DATA_DIR must be defined for this test target (see tests/CMakeLists.txt)"
#endif

namespace geck::test {

/// Absolute path to the in-source tests/data directory.
inline std::filesystem::path dataDir() {
    return std::filesystem::path(GECK_TEST_DATA_DIR);
}

/// Absolute path to a fixture file under tests/data.
inline std::filesystem::path dataPath(const std::string& name) {
    return dataDir() / name;
}

} // namespace geck::test
