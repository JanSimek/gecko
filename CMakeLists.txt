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
include(${CMAKE_CURRENT_LIST_DIR}/cmake/dependencies.cmake)

##################################################
# Compiler settings
##################################################

# Enable verbose compiler warnings
if(MSVC)
    add_compile_options(/W4) # TEMP: /WX dropped to enumerate all Windows warnings
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
