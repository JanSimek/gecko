# General tests (Catch2) - Pure C++ logic tests
add_executable(general_tests
    general/test_engine_ids.cpp
    general/test_hexagon_grid.cpp
    general/test_message_enum_utils.cpp
    general/test_ai_txt.cpp
    general/test_maps_txt.cpp
    general/test_gam.cpp
    general/test_msg.cpp
    general/test_writable_data_root.cpp
    general/test_map_name_editor.cpp
    general/test_map_name_resolver.cpp
    general/test_city_txt.cpp
    general/test_worldmap_txt.cpp
    general/test_world_encounters.cpp
    general/test_palette_blend.cpp
    general/test_worldmap_scene.cpp
    general/test_quests_txt.cpp
    general/test_endgame_txt.cpp
    general/test_gvar_tables_txt.cpp
    general/test_gvar_refs.cpp
    general/test_pro.cpp
    general/test_pro_helper.cpp
    general/test_tile_utils.cpp
    general/test_reader_factory.cpp
    general/test_selection_state.cpp
    general/test_selection_manager.cpp
    general/test_game_data_path_resolver.cpp
    general/test_data_path_resolution.cpp
    general/test_map_object.cpp
    general/test_map_sprite_loader.cpp
    general/test_frm_resolver.cpp
    general/test_viewport_controller.cpp
    general/test_edge_scroll.cpp
    general/test_map_factory.cpp
    general/test_map.cpp
    general/test_map_edge.cpp
    general/test_undo_stack.cpp
    general/test_object_command_controller.cpp
    general/test_edge_edit_service.cpp
    general/test_input_handler.cpp
    general/test_tool_registry.cpp
    general/test_fill_brush_tool.cpp
    general/test_animation_layout.cpp
    general/test_reading_lst.cpp
    general/test_reading_pal.cpp
    general/test_reading_frm.cpp
    general/test_coordinates.cpp
    general/test_spatial_index.cpp
    general/test_object_queries.cpp
    general/test_reading_dat.cpp
    general/test_hex_geometry.cpp
    general/test_hex_line.cpp
    general/test_exit_grid_direction.cpp
    general/test_pattern_stamper.cpp
    general/test_pattern_builder.cpp
    general/test_fill_plan.cpp
    general/test_floor_synth.cpp
    general/test_map_script_api.cpp
    general/test_map_completeness.cpp
    general/test_resource_repository.cpp
    general/test_script_names.cpp
    general/test_script_source_locator.cpp
    general/test_ssl_script_facts.cpp
    general/test_ssl_preprocessor.cpp
    general/test_ssl_parser.cpp
    general/test_ssl_analysis.cpp
    general/test_object_visibility.cpp
    general/test_dat_archive.cpp
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

# Tests of the headless CLI library (gecko_cli): the analyze/describe/export/find-examples/lint/frm/
# resource tools, pattern JSON, reachability, the map graph and saved-map loading. None needs scripting or game data.
if(TARGET gecko_cli)
    target_sources(general_tests PRIVATE
        general/test_cli_tools.cpp
        general/test_engine_tile.cpp
        general/test_find_examples.cpp
        general/test_frm_inspect.cpp
        general/test_int_disassembler.cpp
        general/test_map_analyzer.cpp
        general/test_map_dump_grid.cpp
        general/test_map_graph.cpp
        general/test_map_load_saved.cpp
        general/test_map_reachability.cpp
        general/test_pattern_json.cpp
        general/test_proto_export.cpp
        general/test_resource_inspect.cpp
        general/test_save_inspect.cpp
        general/test_script_lint.cpp
        general/test_ssl_corpus.cpp
    )
    target_link_libraries(general_tests PRIVATE gecko_cli)
endif()

# MCP server dispatch test: only when the MCP server is built (gecko_mcp brings gecko_cli +
# nlohmann/json). The dispatch is pure, so this needs no scripting and no game data.
if(TARGET gecko_mcp)
    target_sources(general_tests PRIVATE general/test_mcp_server.cpp general/test_mcp_tools.cpp)
    target_link_libraries(general_tests PRIVATE gecko_mcp)
endif()

add_executable(qt_tests
    qt/qt_test_main.cpp
    qt/test_pro_editor_widgets.cpp
    qt/test_main_window.cpp
    qt/test_map_info_panel.cpp
    qt/test_scripts_panel.cpp
    qt/test_exit_grids.cpp
    qt/test_dialogs.cpp
    qt/test_editor_widget.cpp
    qt/test_selection_panel.cpp
    qt/test_game_launcher.cpp
    # Preferences: the game-location (a macOS .app fixes the game root) and data-path widgets.
    qt/test_preferences.cpp
    qt/test_palette_model.cpp
    qt/test_object_palette_indexing.cpp
    # PatternSerializer is the editor's Qt (QByteArray/QString) serializer.
    qt/test_pattern_serializer.cpp
    qt/test_keybindings.cpp
    # hintForContext is the pure status-bar key-hint provider (lives in gecko_app).
    qt/test_editor_hints.cpp
    # Log panel plumbing: the LogModel store (cap, threading), the spdlog sink, the filter proxy.
    qt/test_log_panel.cpp
    # The Log dock's "Map" tab: report -> grouped tree, empty states, refresh signal.
    qt/test_completeness_view.cpp
    # File browser population: worker-computed metadata -> chunked tree build.
    qt/test_file_browser_population.cpp
    # Explicit save-location marker on the data paths: resolution, never-dangles invariant, JSON round-trip.
    qt/test_settings_writable_path.cpp
    # CI-failing ceilings on operations that have regressed catastrophically before.
    qt/test_performance_guards.cpp
)

# The Luau console dock exists only when the runtime is compiled in.
if(GECK_ENABLE_SCRIPTING)
    target_sources(qt_tests PRIVATE qt/test_script_console.cpp)
endif()

# test_pattern_serializer also round-trips the headless pattern writer (gecko_cli) through the
# editor's PatternSerializer; that case compiles only when the CLI lib is built (it owns
# cli::serializePattern).
if(TARGET gecko_cli)
    target_compile_definitions(qt_tests PRIVATE GECK_TEST_WITH_CLI)
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

# The Qt-only helpers (support/Qt*.h: widget lookup, event pumping, dialog answering, MainWindow
# fixture). Only qt_tests links it, so general_tests stays free of Qt.
add_library(test_support_qt INTERFACE)
target_link_libraries(test_support_qt INTERFACE test_support Qt6::Widgets Qt6::Test)

target_link_libraries(qt_tests PRIVATE
    Catch2::Catch2
    test_support_qt
    gecko::app
)

target_include_directories(qt_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# The editor's bundled art (art/misc/HEX.frm, ...), mounted by support/EditorResources.h.
target_compile_definitions(qt_tests PRIVATE
    GECK_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources"
)

# Catch2 test discovery for general tests (testing is enabled by the top-level CMakeLists)
include(Catch)

# In CI environments, use simpler test registration to avoid library loading issues
if(DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS})
    # Register tests without discovery - this avoids running the executable during configure
    add_test(NAME general_tests COMMAND general_tests)
else()
    # Use automatic test discovery for local development
    catch_discover_tests(general_tests)
endif()

add_test(NAME qt_tests COMMAND qt_tests)


# Windows runtime DLLs for the test executables are staged by copy_runtime_dlls (top-level
# CMakeLists), which copies the union of every executable's DLLs once — doing it per-target here
# raced with gecko's identical copies into the shared output dir.

# Both suites resolve fixtures (GECK_TEST_DATA_DIR) and the bundled resources/
# (GECK_RESOURCES_DIR) by absolute path, so nothing is staged next to them.
