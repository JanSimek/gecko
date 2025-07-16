
add_executable(tests
    test_reading_gam.cpp
    test_reading_msg.cpp
    test_selection_system.cpp
    test_tile_utils.cpp
)
target_link_libraries(tests PRIVATE
    Catch2::Catch2WithMain
    spdlog::spdlog_header_only
    vault
    sfml-graphics
    sfml-system
)

include(CTest)
include(Catch)
catch_discover_tests(tests)

# copy shared external libraries to the build directory for Windows builds
if(WIN32)
    add_custom_command(
        TARGET tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:tests> $<TARGET_FILE_DIR:tests>
        COMMAND_EXPAND_LISTS
    )
endif()

add_custom_command(
    TARGET tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/tests/data $<TARGET_FILE_DIR:tests>/data
)
