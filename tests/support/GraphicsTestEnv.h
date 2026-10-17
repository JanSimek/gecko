#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <optional>
#include <string>

namespace geck::test {

/// The value of environment variable `name`, or nullopt when unset. MSVC rejects std::getenv as
/// unsafe (C4996, an error under /WX), so Windows reads it through _dupenv_s.
inline std::optional<std::string> environmentVariable(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string copy(value);
    free(value);
    return copy;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::optional<std::string>(value) : std::nullopt;
#endif
}

/// True when running under a headless CI environment (CI or GITHUB_ACTIONS set),
/// where tests that need a graphics context (texture/sprite creation, spatial
/// index builds) cannot run. A Linux CI job that runs the tests under Xvfb sets
/// DISPLAY and has Mesa GL, so it is not headless. Replaces the per-file
/// CI-detection block that was copy-pasted across the graphics-dependent suites.
inline bool headlessCiEnvironment() {
#ifdef _WIN32
    char* ci = nullptr;
    char* githubActions = nullptr;
    size_t len = 0;
    _dupenv_s(&ci, &len, "CI");
    _dupenv_s(&githubActions, &len, "GITHUB_ACTIONS");
    const bool present = (ci != nullptr || githubActions != nullptr);
    free(ci);
    free(githubActions);
    return present;
#else
    const bool ci = std::getenv("CI") != nullptr || std::getenv("GITHUB_ACTIONS") != nullptr;
#if defined(__linux__)
    return ci && std::getenv("DISPLAY") == nullptr;
#else
    return ci;
#endif
#endif
}

} // namespace geck::test

/// Skips the current Catch2 test when running headless in CI. Use at the top of a
/// TEST_CASE that requires a real graphics context.
#define GECK_SKIP_IF_HEADLESS_CI()                            \
    do {                                                      \
        if (::geck::test::headlessCiEnvironment()) {          \
            SKIP("Graphics tests skipped in CI environment"); \
        }                                                     \
    } while (false)
