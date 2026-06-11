#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

namespace geck::test {

/// True when running under a headless CI environment (CI or GITHUB_ACTIONS set),
/// where tests that need a graphics context (texture/sprite creation, spatial
/// index builds) cannot run. Replaces the per-file CI-detection block that was
/// copy-pasted across the graphics-dependent test suites.
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
    return std::getenv("CI") != nullptr || std::getenv("GITHUB_ACTIONS") != nullptr;
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
