# Third-party dependency resolution for Gecko.
#
# Included from the root CMakeLists.txt after the project options and the
# geck_disable_target_warnings() helper are defined (both are used below).
# include() does not change CMAKE_CURRENT_SOURCE_DIR, so paths like
# ${CMAKE_CURRENT_SOURCE_DIR}/vendor/... still resolve against the project root.

# System dependencies first
find_package(PkgConfig QUIET)

# Qt6 for dialogs and future UI migration
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg)
if(GECK_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# SFML - prefer system package
if(GECK_USE_SYSTEM_LIBS)
    find_package(SFML 3 COMPONENTS System Window Graphics)
endif()

if(NOT SFML_FOUND)
    message(STATUS "Using FetchContent for SFML")
    FetchContent_Declare(
            SFML
            GIT_REPOSITORY https://github.com/SFML/SFML.git
            GIT_TAG 3.0.1
            GIT_SHALLOW TRUE
    )
    set(SFML_BUILD_AUDIO FALSE CACHE BOOL "Build audio module")
    set(SFML_BUILD_NETWORK FALSE CACHE BOOL "Build network module")
    FetchContent_MakeAvailable(SFML)
    geck_disable_target_warnings(sfml-system sfml-window sfml-graphics sfml-network sfml-audio sfml-main)
endif()

# ZLIB - use FetchContent as fallback if not found
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    message(STATUS "Using FetchContent for ZLIB")
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_SHALLOW TRUE
    )
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "Build zlib examples")
    FetchContent_MakeAvailable(zlib)
    geck_disable_target_warnings(zlibstatic zlib)
    # Create ZLIB::ZLIB target for compatibility
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
    # Set up include directories as SYSTEM to suppress warnings
    target_include_directories(zlibstatic SYSTEM INTERFACE
        ${zlib_SOURCE_DIR}
        ${zlib_BINARY_DIR}
    )
    set(ZLIB_FOUND TRUE)
    set(ZLIB_LIBRARIES zlibstatic)
    set(ZLIB_INCLUDE_DIRS ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
endif()

# macOS find_package(ZLIB) points at the SDK's <SDK>/usr/include (the C stdlib
# dir); propagated as -isystem it shadows libc++ and breaks <cstddef> on clang 21.
# zlib.h is already on the default search path, so drop the redundant include dir.
if(APPLE AND ZLIB_FOUND AND TARGET ZLIB::ZLIB)
    get_target_property(_geck_zlib_inc ZLIB::ZLIB INTERFACE_INCLUDE_DIRECTORIES)
    if(_geck_zlib_inc MATCHES "/usr/include$")
        set_target_properties(ZLIB::ZLIB PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
        message(STATUS "Stripped SDK /usr/include from ZLIB::ZLIB to protect libc++ header order")
    endif()
endif()

# spdlog - prefer system package
if(GECK_USE_SYSTEM_LIBS)
    find_package(spdlog)
endif()

if(NOT spdlog_FOUND)
    message(STATUS "Using FetchContent for spdlog")
    FetchContent_Declare(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            # Bumped from v1.12.0: its bundled fmt fails under Apple clang 21
            # (FMT_COMPILE_STRING rejected as non-constexpr). We also use
            # SPDLOG_USE_STD_FORMAT (below) so spdlog uses C++20 <format> instead
            # of bundled fmt, side-stepping the macOS-SDK/libc++ fmt incompatibilities
            # entirely (the codebase has no fmt:: usage or custom fmt formatters).
            GIT_TAG v1.15.3
            GIT_SHALLOW TRUE
    )
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Build examples")
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Build tests")
    set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "Use std::format instead of bundled fmt")
    FetchContent_MakeAvailable(spdlog)
    # Note: spdlog creates namespace targets, we'll handle warnings via system includes
endif()

# Wrap spdlog target to treat headers as system includes
set(GECKO_SPDLOG_TARGET "")
if(TARGET spdlog::spdlog_header_only)
    set(GECKO_SPDLOG_TARGET spdlog::spdlog_header_only)
elseif(TARGET spdlog::spdlog)
    set(GECKO_SPDLOG_TARGET spdlog::spdlog)
endif()

if(GECKO_SPDLOG_TARGET)
    add_library(gecko_spdlog INTERFACE)
    target_link_libraries(gecko_spdlog INTERFACE ${GECKO_SPDLOG_TARGET})
    # spdlog is consumed header-only, so every TU must see SPDLOG_USE_STD_FORMAT to
    # match how spdlog itself was built; otherwise the bundled fmt headers are pulled
    # in and break under Apple clang 21 / macOS SDK libc++.
    target_compile_definitions(gecko_spdlog INTERFACE SPDLOG_USE_STD_FORMAT)
    target_include_directories(gecko_spdlog SYSTEM INTERFACE
        $<TARGET_PROPERTY:${GECKO_SPDLOG_TARGET},INTERFACE_INCLUDE_DIRECTORIES>
    )
    add_library(gecko::spdlog ALIAS gecko_spdlog)
else()
    message(FATAL_ERROR "spdlog target was not created")
endif()

# VFSPP - Virtual File System library (local submodule)
add_library(vfspp INTERFACE)
add_library(vfspp::vfspp ALIAS vfspp)
target_include_directories(vfspp SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/vfspp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/vfspp/vendor/miniz-cpp
)
target_compile_features(vfspp INTERFACE cxx_std_17)

# Make SFML available
if(NOT SFML_FOUND)
    FetchContent_MakeAvailable(SFML)
endif()

# Catch2 for testing - prefer system package, fall back to FetchContent
if(GECK_BUILD_TESTS)
    if(GECK_USE_SYSTEM_LIBS)
        find_package(Catch2 3 QUIET)
    endif()

    if(NOT Catch2_FOUND)
        message(STATUS "Using FetchContent for Catch2")
        FetchContent_Declare(
                Catch2
                GIT_REPOSITORY https://github.com/catchorg/Catch2.git
                # Pinned to the exact v3.4.0 commit rather than the tag, and cloned
                # in full (no GIT_SHALLOW). A commit hash lets FetchContent verify
                # the checkout without a network update step on every reconfigure,
                # which avoids the flaky "fatal: not a git repository" gitupdate
                # failure; a full clone avoids the incomplete checkouts that a
                # shallow fetch of a tag occasionally produced.
                GIT_TAG 6e79e682b726f524310d55dec8ddac4e9c52fb5f # v3.4.0
        )
        FetchContent_MakeAvailable(Catch2)
        geck_disable_target_warnings(Catch2 Catch2WithMain)
    endif()
endif()

# Optional scripting runtime: embed Luau (its VM/Compiler/Ast) + LuaBridge3
# (header-only binding). Gated on GECK_ENABLE_SCRIPTING so a default build never fetches
# or compiles them. Declared here (before the project's global /W4 /WX) so Luau's own
# sources aren't held to the editor's warning policy.
if(GECK_ENABLE_SCRIPTING)
    set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
    set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        Luau
        GIT_REPOSITORY https://github.com/luau-lang/luau.git
        GIT_TAG 8f33df910d790c1321a20028af8d8134fa3e0334 # 0.724
    )
    FetchContent_MakeAvailable(Luau)
    geck_disable_target_warnings(Luau.VM Luau.Compiler Luau.Ast Luau.Common Luau.Bytecode)
    # Consumed under /W4 /WX, so mark the targets SYSTEM: their headers are treated as
    # external (warnings suppressed) when included, not only when Luau itself builds.
    foreach(_luau_target IN ITEMS Luau.VM Luau.Compiler Luau.Ast Luau.Common Luau.Bytecode)
        if(TARGET ${_luau_target})
            set_target_properties(${_luau_target} PROPERTIES SYSTEM TRUE)
        endif()
    endforeach()

    # LuaBridge3 is header-only; its LUABRIDGE_TESTING auto-disables when consumed as a
    # subproject, so MakeAvailable just exposes the headers (used via its source dir).
    FetchContent_Declare(
        LuaBridge3
        GIT_REPOSITORY https://github.com/kunitoki/LuaBridge3.git
        GIT_TAG 53e031b7df6a14d43f92a54fd792b76dbadcc970 # 3.0-rc12
    )
    FetchContent_MakeAvailable(LuaBridge3)
endif()
