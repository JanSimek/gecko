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
endif()

# ZLIB
find_package(ZLIB REQUIRED)

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
