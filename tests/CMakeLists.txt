# General tests (Catch2) - Pure C++ logic tests
add_executable(general_tests
    general/test_reading_gam.cpp
    general/test_reading_msg.cpp
    general/test_tile_utils.cpp
    general/test_selection_system.cpp
)

target_link_libraries(general_tests PRIVATE
    Catch2::Catch2WithMain
    spdlog::spdlog_header_only
    vault
    sfml-graphics
    sfml-system
)

# Qt tests - UI component and integration tests
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(qt_tests
    qt/test_ui_components.cpp
)

target_link_libraries(qt_tests PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Widgets
    spdlog::spdlog_header_only
    vault
    sfml-graphics
    sfml-system
)

# Enable testing
include(CTest)

# Catch2 test discovery for general tests
include(Catch)
catch_discover_tests(general_tests)

# Qt test discovery for Qt tests
add_test(NAME qt_tests COMMAND qt_tests)

# copy shared external libraries to the build directory for Windows builds
if(WIN32)
    add_custom_command(
        TARGET general_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:general_tests> $<TARGET_FILE_DIR:general_tests>
        COMMAND_EXPAND_LISTS
    )
    add_custom_command(
        TARGET qt_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:qt_tests> $<TARGET_FILE_DIR:qt_tests>
        COMMAND_EXPAND_LISTS
    )
endif()

# Copy test data
add_custom_command(
    TARGET general_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/tests/data $<TARGET_FILE_DIR:general_tests>/data
)

add_custom_command(
    TARGET qt_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/tests/data $<TARGET_FILE_DIR:qt_tests>/data
)