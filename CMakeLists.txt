cmake_minimum_required(VERSION 3.24)

# Set minimum policy version for legacy dependencies
cmake_policy(VERSION 3.24)

project(
    geck-mapper
    VERSION 0.1
    DESCRIPTION "Map editor for Fallout 2"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Setting build type to Release as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

include(FetchContent)
include(GNUInstallDirs)

##################################################
# Options
##################################################
option(GECK_USE_SYSTEM_LIBS "Use system libraries when available" OFF)
option(GECK_BUILD_TESTS "Build tests" ON)

##################################################
# Dependencies
##################################################

# System dependencies first
find_package(PkgConfig QUIET)

# Qt6 for dialogs and future UI migration
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# SFML - prefer system package
if(GECK_USE_SYSTEM_LIBS)
    find_package(SFML 2.5 COMPONENTS system window graphics)
endif()

if(NOT SFML_FOUND)
    message(STATUS "Using FetchContent for SFML")
    FetchContent_Declare(
            SFML
            GIT_REPOSITORY https://github.com/SFML/SFML.git
            GIT_TAG 2.6.1
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

# vfspp - using existing external directory temporarily
add_subdirectory("external/vfspp" EXCLUDE_FROM_ALL)

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
   system libs    | ${GECK_USE_SYSTEM_LIBS}
   build tests    | ${GECK_BUILD_TESTS}
   Qt6 version    | ${Qt6_VERSION}
")

##################################################
# Installation
##################################################
include(InstallRequiredSystemLibraries)

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY resources/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/resources
)