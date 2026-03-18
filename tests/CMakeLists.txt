# General tests (Catch2) - Pure C++ logic tests
add_executable(general_tests
    general/test_ammo_caliber_names.cpp
    general/test_fallout_engine_enums.cpp
    general/test_hexagon_grid.cpp
    general/test_message_enum_utils.cpp
    general/test_reading_gam.cpp
    general/test_reading_msg.cpp
    general/test_reading_pro_drug.cpp
    general/test_tile_utils.cpp
    general/test_reader_factory.cpp
    general/test_selection_system.cpp
    general/test_game_data_path_resolver.cpp

    # Selection system sources (needed for linking)
    ../src/selection/SelectionState.cpp
    ../src/editor/HexagonGrid.cpp
    ../src/editor/Hex.cpp
)

# Performance tests (Catch2 with benchmarking) - Reader performance tests
add_executable(performance_tests
    performance/test_reader_performance.cpp
)

add_executable(qt_tests
    qt/qt_test_main.cpp
    qt/test_data_path_resolution.cpp
    qt/test_ui_regressions.cpp
)

target_link_libraries(general_tests PRIVATE
    Catch2::Catch2WithMain
    gecko::spdlog
    vault
    SFML::System SFML::Graphics
)

target_include_directories(general_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(performance_tests PRIVATE
    Catch2::Catch2WithMain
    gecko::spdlog
    vault
    SFML::System SFML::Graphics
)

target_link_libraries(qt_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    gecko::app
)

target_include_directories(qt_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Enable testing
include(CTest)

# Catch2 test discovery for general tests
include(Catch)

# In CI environments, use simpler test registration to avoid library loading issues
if(DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS})
    # Register tests without discovery - this avoids running the executable during configure
    add_test(NAME general_tests COMMAND general_tests)
    add_test(NAME performance_tests COMMAND performance_tests)
else()
    # Use automatic test discovery for local development
    catch_discover_tests(general_tests)
    catch_discover_tests(performance_tests)
endif()

add_test(NAME qt_tests COMMAND qt_tests)


# copy shared external libraries to the build directory for Windows builds
if(WIN32)
    add_custom_command(
        TARGET general_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:general_tests> $<TARGET_FILE_DIR:general_tests>
        COMMAND_EXPAND_LISTS
    )
    add_custom_command(
        TARGET performance_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:performance_tests> $<TARGET_FILE_DIR:performance_tests>
        COMMAND_EXPAND_LISTS
    )
    add_custom_command(
        TARGET qt_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:qt_tests> $<TARGET_FILE_DIR:qt_tests>
        COMMAND_EXPAND_LISTS
    )
endif()

# Copy test data to both the executable directory and the test working directory
add_custom_command(
    TARGET general_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/tests/data $<TARGET_FILE_DIR:general_tests>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/tests/data ${CMAKE_CURRENT_BINARY_DIR}/data
)

add_custom_command(
    TARGET performance_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/tests/data $<TARGET_FILE_DIR:performance_tests>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/tests/data ${CMAKE_CURRENT_BINARY_DIR}/data
)

add_custom_command(
    TARGET qt_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources $<TARGET_FILE_DIR:qt_tests>/resources
)
