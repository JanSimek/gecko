cmake_minimum_required(VERSION 3.31)

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

# System dependencies first
find_package(PkgConfig QUIET)

# Qt6 for dialogs and future UI migration
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg)
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

# spdlog - prefer system package
if(GECK_USE_SYSTEM_LIBS)
    find_package(spdlog)
endif()

if(NOT spdlog_FOUND)
    message(STATUS "Using FetchContent for spdlog")
    FetchContent_Declare(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG v1.12.0
            GIT_SHALLOW TRUE
    )
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Build examples")
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Build tests")
    FetchContent_MakeAvailable(spdlog)
    geck_disable_target_warnings(spdlog spdlog_header_only)
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

# Catch2 for testing
if(GECK_BUILD_TESTS)
    FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.4.0
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    geck_disable_target_warnings(Catch2 Catch2WithMain)
endif()

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
endif()

# Targets
add_subdirectory(src)

if(GECK_BUILD_TESTS AND TARGET Catch2::Catch2WithMain)
    add_subdirectory(tests)
    enable_testing()
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
