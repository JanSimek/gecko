#pragma once

#include <catch2/catch_test_macros.hpp>

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>

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
    const bool ci = environmentVariable("CI").has_value() || environmentVariable("GITHUB_ACTIONS").has_value();
#if defined(__linux__)
    return ci && !environmentVariable("DISPLAY").has_value();
#else
    return ci;
#endif
}

/// True where SFML cannot query live keyboard state: Linux with neither an X11 nor a Wayland display.
inline bool keyboardStateUnavailable() {
#if defined(__linux__)
    return !environmentVariable("DISPLAY").has_value() && !environmentVariable("WAYLAND_DISPLAY").has_value();
#else
    return false;
#endif
}

/// A 1x1 white texture, for sprites whose bounds matter but whose pixels don't. Needs a graphics
/// context: call it after GECK_SKIP_IF_HEADLESS_CI().
inline sf::Texture whiteTexture() {
    sf::Texture texture;
    REQUIRE(texture.loadFromImage(sf::Image{ sf::Vector2u{ 1, 1 }, sf::Color::White }));
    return texture;
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

/// Skips the current Catch2 test where SFML cannot read the keyboard state the code under test
/// queries (headless Linux).
#define GECK_SKIP_IF_NO_KEYBOARD_STATE()                                                           \
    do {                                                                                           \
        if (::geck::test::keyboardStateUnavailable()) {                                            \
            SKIP("This path queries live keyboard state, which SFML cannot do on headless Linux"); \
        }                                                                                          \
    } while (false)
