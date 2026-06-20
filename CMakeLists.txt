cmake_minimum_required(VERSION 3.24)

project(
    gecko
    VERSION 1.0.0
    DESCRIPTION "Map editor for Fallout 2"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

    # Single owner of the build-tree resource staging. The editor and qt_tests both run from
    # this shared CMAKE_RUNTIME_OUTPUT_DIRECTORY, and each used to POST_BUILD-copy
    # ${CMAKE_SOURCE_DIR}/resources into the same ${CMAKE_BINARY_DIR}/resources — which raced under
    # parallel builds (intermittent "Error copying directory ..." on Windows CI). One ALL target now
    # owns the copy and consumers depend on it instead of duplicating it. (Single-config only;
    # multi-config generators keep their per-target, per-config copies below.)
    add_custom_target(copy_runtime_resources ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources ${CMAKE_BINARY_DIR}/resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/scripts ${CMAKE_BINARY_DIR}/resources/scripts
        COMMENT "Staging resources + scripts into the build tree"
        VERBATIM)
endif()

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Setting build type to Release as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

include(FetchContent)
include(GNUInstallDirs)

##################################################
# Version configuration
##################################################
configure_file(
    "${CMAKE_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)

##################################################
# Options
##################################################
option(GECK_USE_SYSTEM_LIBS "Use system libraries when available" ON)
option(GECK_BUILD_TESTS "Build tests" ON)
option(GECK_ENABLE_SANITIZERS "Enable sanitizer builds for debugging" OFF)
# Headless gecko-cli tool. Default ON: it only links the already-built core/resource/vault
# libraries (no Qt, no GL, no extra deps), so it adds nothing to fetch — the flag just lets
# minimal builds opt out, like GECK_BUILD_TESTS. (Contrast GECK_ENABLE_SCRIPTING, OFF because
# it downloads Luau.)
option(GECK_BUILD_CLI "Build the headless gecko-cli tool" ON)
# MCP server (gecko-mcp): a JSON-RPC stdio wrapper over the gecko_cli analyze/generate logic.
# Needs the CLI lib; pulls in header-only nlohmann/json. Default ON, but opts out independently.
option(GECK_BUILD_MCP "Build the MCP server (gecko-mcp)" ON)
# Tier-2 scripting (embeds Luau + LuaBridge3). OFF by default so a normal build never
# downloads or compiles Luau; CI builds it ON. See PLAN.md "Optional, compile-flag-gated".
option(GECK_ENABLE_SCRIPTING "Build the optional Luau scripting runtime" OFF)

##################################################
# Helper Functions
##################################################
function(geck_disable_target_warnings)
    foreach(_target IN LISTS ARGN)
        if(NOT TARGET ${_target})
            continue()
        endif()

        get_property(_is_alias TARGET ${_target} PROPERTY ALIASED_TARGET SET)
        if(_is_alias)
            get_property(_resolved TARGET ${_target} PROPERTY ALIASED_TARGET)
        else()
            set(_resolved ${_target})
        endif()

        if(NOT _resolved)
            continue()
        endif()

        get_target_property(_target_type ${_resolved} TYPE)
        if(NOT _target_type)
            continue()
        endif()

        if(_target_type MATCHES "INTERFACE")
            continue()
        endif()

        if(MSVC)
            target_compile_options(${_resolved} PRIVATE /w)
        else()
            target_compile_options(${_resolved} PRIVATE -w)
        endif()
    endforeach()
endfunction()

##################################################
# Dependencies
##################################################
include(${CMAKE_CURRENT_LIST_DIR}/cmake/dependencies.cmake)

##################################################
# Compiler settings
##################################################

# Enable verbose compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wshadow -Wnon-virtual-dtor -pedantic)
endif()

# Sanitizer options
if(GECK_ENABLE_SANITIZERS AND NOT MSVC)
    message(STATUS "Enabling sanitizers for debugging")
    add_compile_options(-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

if(MSVC)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    # Suppress deprecation warning for stdext::checked_array_iterator in debug builds
    add_compile_definitions(_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
endif()

# Targets
add_subdirectory(src)

if(GECK_BUILD_TESTS AND TARGET Catch2::Catch2WithMain)
    add_subdirectory(tests)
    enable_testing()
endif()

# Windows: one target stages the runtime DLLs into the shared output dir. Each executable used to
# copy its own $<TARGET_RUNTIME_DLLS> in a POST_BUILD step, and those steps raced on the Qt/SFML
# DLLs they share (intermittent, then persistent, "Error copying ...sfml-window-3.dll" on the
# Windows build). Copying the *union* of every executable's runtime DLLs from a single target makes
# it one sequential operation — no race. copy_if_different tolerates the DLLs shared across targets.
if(WIN32)
    set(_runtime_dll_targets ${PROJECT_NAME})
    if(GECK_BUILD_TESTS AND TARGET general_tests)
        list(APPEND _runtime_dll_targets general_tests performance_tests qt_tests)
    endif()
    set(_runtime_dll_genexprs "")
    foreach(_t IN LISTS _runtime_dll_targets)
        list(APPEND _runtime_dll_genexprs "$<TARGET_RUNTIME_DLLS:${_t}>")
    endforeach()
    add_custom_target(copy_runtime_dlls ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_runtime_dll_genexprs} $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND_EXPAND_LISTS
        DEPENDS ${_runtime_dll_targets}
        COMMENT "Staging runtime DLLs into the build tree")
endif()

##################################################
# Configuration summary
##################################################
message("")
message("${PROJECT_NAME} ${PROJECT_VERSION}
         compiler | ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}
        generator | ${CMAKE_GENERATOR}
       build type | ${CMAKE_BUILD_TYPE}
         cxxflags | ${CMAKE_CXX_FLAGS}
        build dir | ${CMAKE_BINARY_DIR}
   install prefix | ${CMAKE_INSTALL_PREFIX}
      system libs | ${GECK_USE_SYSTEM_LIBS}
      build tests | ${GECK_BUILD_TESTS}
      Qt6 version | ${Qt6_VERSION}
")

##################################################
# Installation
##################################################
include(InstallRequiredSystemLibraries)

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION .
)

install(DIRECTORY resources/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/resources
)

# Example generation scripts ship alongside the resources (resources/scripts).
install(DIRECTORY scripts/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/resources/scripts
)

# Platform-specific icon installation
if(UNIX AND NOT APPLE)
    # Install desktop file
    install(FILES resources/gecko.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    
    # Install icons
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES resources/icons/hicolor/${size}x${size}/apps/gecko.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
    endforeach()
endif()
