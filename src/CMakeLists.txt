
###################################
# Gecko executable
###################################

# Define source files
set(GECK_APP_SOURCES
    Application.h
    Application.cpp

    util/FalloutEngineEnums.h
    ui/ResourceInitializer.h
    ui/ResourceInitializer.cpp

    ui/QtDialogs.h
    ui/QtDialogs.cpp

    ui/Settings.h
    ui/Settings.cpp

    pattern/PatternSerializer.h
    pattern/PatternSerializer.cpp
    pattern/PatternLibrary.h
    pattern/PatternLibrary.cpp
    pattern/ThumbnailComposer.h
    pattern/ThumbnailComposer.cpp
    pattern/PatternThumbnail.h
    pattern/PatternThumbnail.cpp

    ui/ExternalEditorLauncher.h
    ui/ExternalEditorLauncher.cpp

    ui/FrmThumbnailGenerator.h
    ui/FrmThumbnailGenerator.cpp

    util/Types.h
    util/Exceptions.h

    # Qt6 UI Common
    ui/common/BaseWidget.h
    ui/common/BaseWidget.cpp
    ui/common/BaseDialog.h
    ui/common/BaseDialog.cpp
    ui/common/BasePaletteWidget.h
    ui/common/BasePanel.h
    ui/common/GridPalettePanel.h
    ui/common/GridPalettePanel.cpp
    ui/common/InventoryItemUiHelper.h
    ui/common/InventoryItemUiHelper.cpp

    # Qt6 UI Core
    ui/core/MainWindow.h
    ui/core/MainWindow.cpp
    editor/TileChange.h
    ui/core/VisibilitySettings.h
    ui/core/EditorWidget.h
    ui/core/EditorWidget.cpp

    # Qt6 UI Panels
    ui/panels/TilePalettePanel.h
    ui/panels/TilePalettePanel.cpp
    ui/panels/ObjectPalettePanel.h
    ui/panels/ObjectPalettePanel.cpp
    ui/panels/MapInfoPanel.h
    ui/panels/MapInfoPanel.cpp
    ui/panels/SelectionPanel.h
    ui/panels/SelectionPanel.cpp
    ui/panels/FileBrowserPanel.h
    ui/panels/FileBrowserPanel.cpp

    # Qt6 UI Dialogs
    ui/dialogs/FrmSelectorDialog.h
    ui/dialogs/FrmSelectorDialog.cpp
    ui/dialogs/SettingsDialog.h
    ui/dialogs/SettingsDialog.cpp
    ui/dialogs/ProEditorDialog.h
    ui/dialogs/ProEditorDialog.cpp
    ui/dialogs/InventoryViewerDialog.h
    ui/dialogs/InventoryViewerDialog.cpp
    ui/dialogs/MessageSelectorDialog.h
    ui/dialogs/MessageSelectorDialog.cpp
    ui/dialogs/ExitGridPropertiesDialog.h
    ui/dialogs/ExitGridPropertiesDialog.cpp
    ui/dialogs/ObjectFlagsDialog.h
    ui/dialogs/ObjectFlagsDialog.cpp
    ui/dialogs/LightPropertiesDialog.h
    ui/dialogs/LightPropertiesDialog.cpp
    ui/dialogs/SceneryDestinationDialog.h
    ui/dialogs/SceneryDestinationDialog.cpp
    ui/dialogs/InstancePropertiesDialog.h
    ui/dialogs/InstancePropertiesDialog.cpp
    ui/dialogs/CritterPropertiesDialog.h
    ui/dialogs/CritterPropertiesDialog.cpp
    ui/dialogs/ScriptSelectorDialog.h
    ui/dialogs/ScriptSelectorDialog.cpp
    ui/dialogs/SpatialScriptDialog.h
    ui/dialogs/SpatialScriptDialog.cpp
    ui/dialogs/AboutDialog.h
    ui/dialogs/AboutDialog.cpp
    ui/dialogs/PatternBrowserDialog.h
    ui/dialogs/PatternBrowserDialog.cpp
    ui/dialogs/MapBrowserDialog.h
    ui/dialogs/MapBrowserDialog.cpp

    # Qt6 UI Widgets
    ui/widgets/SFMLWidget.h
    ui/widgets/SFMLWidget.cpp
    ui/widgets/LoadingWidget.h
    ui/widgets/LoadingWidget.cpp
    ui/widgets/PaginationWidget.h
    ui/widgets/PaginationWidget.cpp
    ui/widgets/DataPathsWidget.h
    ui/widgets/DataPathsWidget.cpp
    ui/widgets/GameLocationWidget.h
    ui/widgets/GameLocationWidget.cpp
    ui/widgets/TextEditorWidget.h
    ui/widgets/TextEditorWidget.cpp
    ui/widgets/ObjectPreviewWidget.h
    ui/widgets/ObjectPreviewWidget.cpp
    ui/widgets/ProInfoPanelWidget.h
    ui/widgets/ProInfoPanelWidget.cpp
    ui/widgets/ProPreviewPanelWidget.h
    ui/widgets/ProPreviewPanelWidget.cpp
    ui/widgets/AnimationController.h
    ui/widgets/AnimationController.cpp
    ui/widgets/WelcomeWidget.h
    ui/widgets/WelcomeWidget.cpp
    ui/widgets/ProCommonFieldsWidget.h
    ui/widgets/ProCommonFieldsWidget.cpp
    
    # PRO Tab Widgets
    ui/widgets/pro/ProTabWidget.h
    ui/widgets/pro/ProTabWidget.cpp
    ui/widgets/pro/ProWallWidget.h
    ui/widgets/pro/ProWallWidget.cpp
    ui/widgets/pro/ProCritterWidget.h
    ui/widgets/pro/ProCritterWidget.cpp
    ui/widgets/pro/ProSceneryWidget.h
    ui/widgets/pro/ProSceneryWidget.cpp
    ui/widgets/pro/ProTileWidget.h
    ui/widgets/pro/ProTileWidget.cpp
    ui/widgets/pro/ProArmorWidget.h
    ui/widgets/pro/ProArmorWidget.cpp
    ui/widgets/pro/ProWeaponWidget.h
    ui/widgets/pro/ProWeaponWidget.cpp
    ui/widgets/pro/ProDrugWidget.h
    ui/widgets/pro/ProDrugWidget.cpp
    ui/widgets/pro/ProContainerKeyWidget.h
    ui/widgets/pro/ProContainerKeyWidget.cpp
    ui/widgets/pro/ProAmmoWidget.h
    ui/widgets/pro/ProAmmoWidget.cpp
    ui/widgets/pro/ProMiscItemWidget.h
    ui/widgets/pro/ProMiscItemWidget.cpp

    # Qt6 UI Constants
    ui/GameEnums.h
    ui/GameEnums.cpp
    ui/UIConstants.h
    ui/rendering/ThumbnailRenderer.h
    ui/rendering/ThumbnailRenderer.cpp
    ui/rendering/MapThumbnail.h
    ui/rendering/MapThumbnail.cpp

    ui/input/InputHandler.h
    ui/input/InputHandler.cpp
    ui/dragdrop/DragDropContext.h
    ui/dragdrop/DragDropManager.h
    ui/dragdrop/DragDropManager.cpp
    ui/tiles/TilePlacementContext.h
    ui/tiles/TilePlacementManager.h
    ui/tiles/TilePlacementManager.cpp
    ui/tools/ExitGridContext.h
    ui/tools/ExitGridPlacementManager.h
    ui/tools/ExitGridPlacementManager.cpp
    # Loaders (kept from state system)
    state/loader/Loader.h
    state/loader/MapLoader.cpp
    state/loader/MapLoader.h
    state/loader/DataPathLoader.cpp
    state/loader/DataPathLoader.h

    # Game launch service
    state/GameLauncher.cpp
    state/GameLauncher.h

    # Qt Resource files
    ${CMAKE_SOURCE_DIR}/resources/icons.qrc
)

# Optional scripting console UI (Qt). The Luau runtime itself lives in gecko_editing; this
# is just the dock widget that drives it, so it stays with the rest of the Qt UI.
if(GECK_ENABLE_SCRIPTING)
    list(APPEND GECK_APP_SOURCES
        ui/panels/ScriptConsoleWidget.h
        ui/panels/ScriptConsoleWidget.cpp)
endif()

# Reusable application library for the executable and UI tests
add_library(gecko_app STATIC ${GECK_APP_SOURCES})
add_library(gecko::app ALIAS gecko_app)

# Platform-specific executable settings
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 main.cpp)
    # Add Windows resource file for icon
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/resources/gecko.rc)
elseif(APPLE)
    # Add icon to bundle
    set(MACOSX_BUNDLE_ICON_FILE gecko.icns)
    set(APP_ICON_MACOSX ${CMAKE_SOURCE_DIR}/resources/gecko.icns)
    set_source_files_properties(${APP_ICON_MACOSX} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE main.cpp ${APP_ICON_MACOSX})
    
    # Set bundle properties
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist
        MACOSX_BUNDLE_BUNDLE_NAME "Gecko"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.gecko.editor"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
    )
else()
    add_executable(${PROJECT_NAME} main.cpp)
endif()

###################################
# GECK Library "Vault"
###################################
add_library(vault STATIC

    #
    # FILE FORMATS
    #

    format/IFile.h

    # DAT
    format/dat/Dat.h
    format/dat/Dat.cpp
    format/dat/DatEntry.h
    format/dat/DatEntry.cpp

    # FRM
    format/frm/Frm.h
    format/frm/Frm.cpp
    format/frm/Direction.h
    format/frm/Direction.cpp
    format/frm/Frame.h
    format/frm/Frame.cpp

    # PAL
    format/pal/Pal.h
    format/pal/Pal.cpp

    # MAP
    format/map/Map.h
    format/map/Map.cpp
    format/map/MapScript.h
    format/map/MapObject.h
    format/map/MapObject.cpp
    format/map/Tile.h
    format/map/Tile.cpp

    # MSG
    format/msg/Msg.h
    format/msg/Msg.cpp

    # LST
    format/lst/Lst.h
    format/lst/Lst.cpp

    # AI (ai.txt — critter AI packets)
    format/ai/AiPacket.h

    # PRO
    format/pro/Pro.h
    format/pro/Pro.cpp

    # GAM
    format/gam/Gam.h
    format/gam/Gam.cpp

    #
    # FILE WRITERS
    #

    # Writer superclass
    writer/FileWriter.h

    writer/map/MapWriter.h
    writer/map/MapWriter.cpp
    
    writer/pro/ProWriter.h
    writer/pro/ProWriter.cpp

    #
    # FILE LOADERS
    #

    # Parser superclass
    reader/FileParser.h
    reader/ReaderFactory.h
    reader/ReaderFactory.cpp

    reader/dat/DatReader.h
    reader/dat/DatReader.cpp

#    writer/dat/DatWriter.h
#    writer/dat/DatWriter.cpp

    reader/frm/FrmReader.h
    reader/frm/FrmReader.cpp

    reader/pal/PalReader.h
    reader/pal/PalReader.cpp

    reader/map/MapReader.h
    reader/map/MapReader.cpp

    reader/msg/MsgReader.h
    reader/msg/MsgReader.cpp

    reader/lst/LstReader.h
    reader/lst/LstReader.cpp

    reader/ai/AiTxtReader.h
    reader/ai/AiTxtReader.cpp

    reader/pro/ProReader.h
    reader/pro/ProReader.cpp

    reader/gam/GamReader.h
    reader/gam/GamReader.cpp

    reader/StreamBuffer.cpp
    reader/StreamBuffer.h

    util/AmmoCaliberNames.h
    util/AmmoCaliberNames.cpp
    util/MessageEnumUtils.h
    util/MessageEnumUtils.cpp

    util/GameDataPathResolver.h
    util/GameDataPathResolver.cpp
)

target_include_directories(vault PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vault PUBLIC
    ZLIB::ZLIB
PRIVATE
    gecko::spdlog
)

###################################
# GECK Resource layer (Qt-free)
###################################
# DAT/VFS, the resource repository, FRM resolution and texture loading. Depends
# only on vault, SFML and vfspp - no Qt, no editor/UI - so it can sit below the
# app layer and be linked by headless tools and tests.
add_library(gecko_resource STATIC
    vfs/Dat2File.hpp
    vfs/Dat2FileSystem.hpp

    resource/PathUtils.h
    resource/CritterFrmResolver.h
    resource/CritterFrmResolver.cpp

    resource/DataFileSystem.h
    resource/DataFileSystem.cpp
    resource/ResourceRepository.h
    resource/ResourceRepository.cpp
    resource/FrmResolver.h
    resource/FrmResolver.cpp
    resource/TextureManager.h
    resource/TextureManager.cpp
    resource/GameResources.h
    resource/GameResources.cpp
)
add_library(gecko::resource ALIAS gecko_resource)

target_include_directories(gecko_resource PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(gecko_resource PUBLIC
    SFML::System SFML::Graphics SFML::Window
    gecko::spdlog
    vfspp
    vault
)

###################################
# GECK Core layer (Qt-free)
###################################
# Editor model (objects, hex grid), selection, viewport coordinate math and the
# Qt-free geometry utilities. Depends only on gecko_resource, vault and SFML - no
# Qt - so editor/selection logic stays headless-testable.
add_library(gecko_core STATIC
    util/ProHelper.h
    util/ProHelper.cpp
    util/Coordinates.h
    util/Coordinates.cpp
    util/SpatialIndex.h
    util/SpatialIndex.cpp

    ui/viewport/ViewportController.h
    ui/viewport/ViewportController.cpp

    selection/SelectionDataProvider.h
    selection/SelectionState.h
    selection/SelectionState.cpp
    selection/SelectionManager.h
    selection/SelectionManager.cpp

    editor/Object.h
    editor/Object.cpp
    editor/HexagonGrid.h
    editor/HexagonGrid.cpp
    editor/HexGeometry.h
    editor/Hex.h
    editor/Hex.cpp
    editor/helper/ObjectQueries.h
    editor/helper/ObjectQueries.cpp
)
add_library(gecko::core ALIAS gecko_core)

target_include_directories(gecko_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(gecko_core PUBLIC
    SFML::System SFML::Graphics SFML::Window
    gecko::spdlog
    vault
    gecko_resource
)

###################################
# GECK Editing layer (Qt-free)
###################################
# Map mutation that is heavier than the core model but still Qt-free: the undoable command
# controller, the sprite/object builders and the scripting host API (plus the optional Luau
# runtime). Sits on gecko_core so the Qt UI (gecko_app) and headless frontends (gecko-cli,
# a future MCP server) drive the same editing code without any of them pulling in Qt.
add_library(gecko_editing STATIC
    util/SpriteFactory.h
    util/SpriteFactory.cpp
    pattern/PatternSprite.h
    pattern/PatternSprite.cpp
    # Qt-free pattern (prefab/stamp) core: the data model, capture (PatternBuilder) and placement
    # (PatternStamper). The editor's PatternSerializer/PatternLibrary stay in gecko_app (they use Qt);
    # headless extract/place use these plus a Qt-free JSON writer in gecko_cli.
    pattern/Pattern.h
    pattern/PatternBuilder.h
    pattern/PatternBuilder.cpp
    pattern/PatternStamper.h
    pattern/PatternStamper.cpp
    ui/rendering/MapSpriteLoader.h
    ui/rendering/MapSpriteLoader.cpp
    # SFML map renderer (Qt-free): drives the same drawing for the Qt editor and headless
    # PNG export. ThumbnailRenderer/MapThumbnail stay in gecko_app — they use QImage/QPainter.
    ui/rendering/HexRenderer.h
    ui/rendering/HexRenderer.cpp
    ui/rendering/RenderingEngine.h
    ui/rendering/RenderingEngine.cpp
    ui/rendering/MapRenderer.h
    ui/rendering/MapRenderer.cpp
    ui/editing/ObjectCommandController.h
    ui/editing/ObjectCommandController.cpp
    ui/editing/UndoBatcher.h
    ui/editing/UndoBatcher.cpp
    ui/editing/TileEditService.h
    ui/editing/TileEditService.cpp
    ui/editing/InventoryEditService.h
    ui/editing/InventoryEditService.cpp
    scripting/MapScriptApi.h
    scripting/MapScriptApi.cpp
    scripting/ScriptApiReference.h
    scripting/ScriptApiReference.cpp
)
add_library(gecko::editing ALIAS gecko_editing)

target_link_libraries(gecko_editing PUBLIC gecko_core)

# Optional Luau scripting runtime, compiled into the editing layer so every frontend that
# links it (the UI and the CLI) can run scripts. GECK_SCRIPTING_ENABLED is PUBLIC so the Qt
# UI's #ifdef'd console wiring sees it transitively through gecko_app's link to gecko_editing.
if(GECK_ENABLE_SCRIPTING)
    target_sources(gecko_editing PRIVATE
        scripting/LuaScriptRuntime.h
        scripting/LuaScriptRuntime.cpp)
    target_link_libraries(gecko_editing PUBLIC
        Luau.VM Luau.Compiler Luau.Ast Luau.Bytecode Luau.Common)
    # SYSTEM so the LuaBridge3 headers are external (suppresses their parse-time warnings).
    target_include_directories(gecko_editing SYSTEM PRIVATE ${luabridge3_SOURCE_DIR}/Source)
    target_compile_definitions(gecko_editing PUBLIC GECK_SCRIPTING_ENABLED)
    if(MSVC)
        # LuaBridge3's templates trip C4702 (unreachable code) at instantiation — an
        # optimizer warning emitted in our TU, which /external can't suppress. Allow it
        # for just this binding file rather than weakening /WX globally.
        set_source_files_properties(scripting/LuaScriptRuntime.cpp
            PROPERTIES COMPILE_OPTIONS "/wd4702")
    endif()
endif()

target_link_libraries(gecko_app PUBLIC
    SFML::System SFML::Graphics SFML::Window
    Qt6::Core
    Qt6::Widgets
    Qt6::Svg
    gecko::spdlog
    vfspp
    vault
    gecko_resource
    gecko_editing
)

# Headless gecko-cli (no Qt). The analyze/generate/render logic lives in the gecko_cli library so
# the MCP server can reuse it; gecko-cli is just a thin frontend. gecko::editing brings the editing
# core + scripting runtime (and transitively gecko_core/resource/vault, SFML, the src/ include dir
# and spdlog) — all Qt-free. `map render` needs an off-screen GL context at runtime (sf::RenderTexture);
# it reports an error instead of crashing when none is available (headless without a display).
if(GECK_BUILD_CLI)
    add_library(gecko_cli STATIC
        cli/BundledResources.h
        cli/BundledResources.cpp
        cli/MapLoad.h
        cli/MapLoad.cpp
        cli/MapAnalyzer.h
        cli/MapAnalyzer.cpp
        cli/MapGenerator.h
        cli/MapGenerator.cpp
        cli/MapRender.h
        cli/MapRender.cpp
        cli/PatternJson.h
        cli/PatternJson.cpp
        cli/PatternExtract.h
        cli/PatternExtract.cpp
        cli/ScriptIntrospect.h
        cli/ScriptIntrospect.cpp
        cli/MapReachability.h
        cli/MapReachability.cpp)
    target_link_libraries(gecko_cli PUBLIC gecko::editing nlohmann_json::nlohmann_json)

    add_executable(gecko-cli cli/main.cpp)
    target_link_libraries(gecko-cli PRIVATE gecko_cli)

    # MCP server: a JSON-RPC stdio wrapper over gecko_cli's analyze/generate (so an AI agent can
    # inspect maps and generate them conversationally). The dispatch lives in a gecko_mcp library
    # (handleMessage is unit-testable); gecko-mcp is the thin stdio frontend.
    if(GECK_BUILD_MCP)
        add_library(gecko_mcp STATIC
            mcp/McpServer.h
            mcp/McpServer.cpp)
        target_link_libraries(gecko_mcp PUBLIC gecko_cli nlohmann_json::nlohmann_json)
        add_library(gecko::mcp ALIAS gecko_mcp)

        add_executable(gecko-mcp mcp/main.cpp)
        target_link_libraries(gecko-mcp PRIVATE gecko_mcp)
    endif()
endif()

target_include_directories(gecko_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(${PROJECT_NAME} PRIVATE gecko_app)

# copy shared external libraries to the build directory for Windows builds
if(WIN32)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND_EXPAND_LISTS
    )
endif()

# Copy resources to appropriate location based on platform. The example generation scripts ship
# under resources/scripts so the running app can find them next to the rest of the resources.
if(APPLE)
    # For macOS bundles, copy to Resources directory
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources $<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources/resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/scripts $<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources/resources/scripts
    )
    # Also copy the main icon.png to Resources root for easy access
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/resources/icon.png $<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources/icon.png
    )
else()
    # For other platforms, copy to executable directory
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources $<TARGET_FILE_DIR:${PROJECT_NAME}>/resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/scripts $<TARGET_FILE_DIR:${PROJECT_NAME}>/resources/scripts
    )
endif()
