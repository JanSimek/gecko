# General tests (Catch2) - Pure C++ logic tests
add_executable(general_tests
    general/test_ammo_caliber_names.cpp
    general/test_fallout_engine_enums.cpp
    general/test_hexagon_grid.cpp
    general/test_message_enum_utils.cpp
    general/test_reading_gam.cpp
    general/test_ai_txt.cpp
    general/test_maps_txt.cpp
    general/test_maps_txt_roundtrip.cpp
    general/test_gam_roundtrip.cpp
    general/test_msg_roundtrip.cpp
    general/test_writable_data_root.cpp
    general/test_map_name_editor.cpp
    general/test_city_txt.cpp
    general/test_worldmap_txt.cpp
    general/test_quests_txt.cpp
    general/test_endgame_txt.cpp
    general/test_gvar_refs.cpp
    general/test_reading_msg.cpp
    general/test_reading_pro_drug.cpp
    general/test_pro_roundtrip.cpp
    general/test_pro_helper.cpp
    general/test_tile_utils.cpp
    general/test_reader_factory.cpp
    general/test_selection_system.cpp
    general/test_selection_state.cpp
    general/test_selection_manager.cpp
    general/test_tile_hex_conversion.cpp
    general/test_game_data_path_resolver.cpp
    general/test_map_object_defaults.cpp
    general/test_map_object_views.cpp
    general/test_frm_resolver.cpp
    general/test_viewport_controller.cpp
    general/test_map_factory.cpp
    general/test_map_roundtrip.cpp
    general/test_undo_stack.cpp
    general/test_object_command_controller.cpp
    general/test_animation_layout.cpp
    general/test_reading_lst.cpp
    general/test_reading_pal.cpp
    general/test_reading_frm.cpp
    general/test_coordinates.cpp
    general/test_spatial_index.cpp
    general/test_object_queries.cpp
    general/test_reading_dat.cpp
    general/test_reading_map.cpp
    general/test_hex_geometry.cpp
    general/test_hex_line.cpp
    general/test_exit_grid_direction.cpp
    general/test_pattern_stamper.cpp
    general/test_pattern_builder.cpp
    general/test_map_script_api.cpp
    general/test_resource_repository.cpp
    general/test_script_names.cpp
    general/test_object_visibility.cpp
    general/test_dat_concurrency.cpp
    general/test_map_renderer.cpp
)

# Tier-2 scripting integration tests: only built when the Luau runtime is compiled in.
if(GECK_ENABLE_SCRIPTING)
    target_sources(general_tests PRIVATE general/test_lua_script_runtime.cpp)
    # The headless map generator lives in gecko_cli and drives the Luau runtime, so its test
    # needs both the CLI library and scripting.
    if(TARGET gecko_cli)
        target_sources(general_tests PRIVATE general/test_map_generator.cpp)
        target_link_libraries(general_tests PRIVATE gecko_cli)
    endif()
endif()

# Pattern stamp (de)serialization round-trip — needs gecko_cli (cli::serializePattern/loadPattern),
# no scripting or game data. test_frm_inspect covers the FRM tools' non-GL logic (FID parse/decode,
# art-name resolution, glob listing) against a temp LST fixture.
if(TARGET gecko_cli)
    target_sources(general_tests PRIVATE general/test_pattern_stamp.cpp general/test_map_reachability.cpp general/test_map_analyzer_scripts.cpp general/test_frm_inspect.cpp)
    target_link_libraries(general_tests PRIVATE gecko_cli)
endif()

# MCP server dispatch test: only when the MCP server is built (gecko_mcp brings gecko_cli +
# nlohmann/json). The dispatch is pure, so this needs no scripting and no game data.
if(TARGET gecko_mcp)
    target_sources(general_tests PRIVATE general/test_mcp_server.cpp)
    target_link_libraries(general_tests PRIVATE gecko_mcp)
endif()

# Performance tests (Catch2 with benchmarking) - Reader performance tests
add_executable(performance_tests
    performance/test_reader_performance.cpp
)

add_executable(qt_tests
    qt/qt_test_main.cpp
    qt/test_data_path_resolution.cpp
    qt/test_ui_regressions.cpp
    qt/test_dat_archive.cpp
    # GameLauncher is an app-layer Qt class, so its ddraw.ini test links gecko::app.
    qt/test_ddraw_ini.cpp
    # PatternSerializer is the editor's Qt (QByteArray/QString) serializer.
    qt/test_pattern_serializer.cpp
    # InputHandler is Qt-free but lives in gecko_app; its dispatch test needs SFML.
    qt/test_input_handler.cpp
    # hintForContext is the pure status-bar key-hint provider (lives in gecko_app).
    qt/test_editor_hints.cpp
)

# Round-trip the headless pattern writer (gecko_cli) through the editor's Qt PatternSerializer.
# Only when the CLI lib is built (it owns cli::serializePattern).
if(TARGET gecko_cli)
    target_sources(qt_tests PRIVATE qt/test_pattern_extract_roundtrip.cpp)
    target_link_libraries(qt_tests PRIVATE gecko_cli)
endif()

# Shared, header-only test support (fixtures, temp files, builders, stub provider).
# Included as "support/<Header>.h"; fixtures resolve via GECK_TEST_DATA_DIR.
add_library(test_support INTERFACE)
target_include_directories(test_support INTERFACE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_support INTERFACE Catch2::Catch2)
target_compile_definitions(test_support INTERFACE
    GECK_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data"
    GECK_TEST_TMP_DIR="${CMAKE_BINARY_DIR}/test-tmp"
    GECK_SCRIPTS_DIR="${CMAKE_SOURCE_DIR}/scripts"
)

# general_tests is pure C++/SFML logic: link the top Qt-free layer (gecko::editing),
# which PUBLIC-chains gecko::core -> gecko::resource -> vault plus SFML/spdlog. It must
# NOT link gecko::app, so Qt/UI coupling can't sneak into a "logic" test — those cases
# belong in qt_tests.
target_link_libraries(general_tests PRIVATE
    Catch2::Catch2WithMain
    test_support
    gecko::editing
)

target_include_directories(general_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(performance_tests PRIVATE
    Catch2::Catch2WithMain
    test_support
    gecko::spdlog
    vault
    SFML::System SFML::Graphics
)

target_link_libraries(qt_tests PRIVATE
    Catch2::Catch2
    test_support
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


# Windows runtime DLLs for the test executables are staged by copy_runtime_dlls (top-level
# CMakeLists), which copies the union of every executable's DLLs once — doing it per-target here
# raced with gecko's identical copies into the shared output dir.

# general_tests / performance_tests resolve fixtures by absolute path
# (GECK_TEST_DATA_DIR via test_support), so no tests/data copy is needed for them.

# qt_tests needs resources/ staged next to it at runtime. Single-config builds share the one
# copy_runtime_resources target (so gecko + qt_tests don't race copying into the same dir);
# multi-config builds copy per-config here.
if(TARGET copy_runtime_resources)
    add_dependencies(qt_tests copy_runtime_resources)
else()
    add_custom_command(
        TARGET qt_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources $<TARGET_FILE_DIR:qt_tests>/resources
    )
endif()
