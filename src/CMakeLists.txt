
###################################
# GECK Mapper executable
###################################
add_executable (${PROJECT_NAME}

    main.cpp

    Application.h
    Application.cpp

    util/ResourceManager.h
    util/ResourceManager.cpp

    util/ProHelper.h
    util/ProHelper.cpp

    util/QtDialogs.h
    util/QtDialogs.cpp
    util/Types.h

    # Qt6 UI
    ui/MainWindow.h
    ui/MainWindow.cpp
    ui/SFMLWidget.h
    ui/SFMLWidget.cpp
    ui/EditorWidget.h
    ui/EditorWidget.cpp
    ui/LoadingWidget.h
    ui/LoadingWidget.cpp
    ui/SelectionPanel.h
    ui/SelectionPanel.cpp
    ui/MapInfoPanel.h
    ui/MapInfoPanel.cpp
    ui/TilePalettePanel.h
    ui/TilePalettePanel.cpp

    # Selection management
    selection/SelectionManager.h
    selection/SelectionManager.cpp
    selection/SelectionBridge.h
    selection/SelectionBridge.cpp

    # Loaders (kept from state system)
    state/loader/Loader.h
    state/loader/MapLoader.cpp
    state/loader/MapLoader.h

    # UI - COMMENTED OUT: Migrating to Qt6
    # ui/util.h
    # ui/IconsFontAwesome6.h

    # ui/panel/Panel.h
    # ui/panel/MainMenuPanel.h
    # ui/panel/MainMenuPanel.cpp
    # ui/panel/MapInfoPanel.h
    # ui/panel/MapInfoPanel.cpp
    # ui/panel/SelectedObjectPanel.h
    # ui/panel/SelectedObjectPanel.cpp    
    # ui/panel/TileSelectionPanel.h
    # ui/panel/TileSelectionPanel.cpp
    # ui/panel/Toolbar.h
    # ui/panel/Toolbar.cpp

    editor/Object.h
    editor/Object.cpp

    editor/helper/ObjectHelper.h

    editor/HexagonGrid.h
    editor/HexagonGrid.cpp

    editor/Hex.h
    editor/Hex.cpp

    # vfspp plugin
    vfs/Dat2File.hpp
    vfs/Dat2FileSystem.hpp
)

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
    format/map/Tile.h
    format/map/Tile.cpp

    # MSG
    format/msg/Msg.h
    format/msg/Msg.cpp

    # LST
    format/lst/Lst.h
    format/lst/Lst.cpp

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

    #
    # FILE LOADERS
    #

    # Parser superclass
    reader/FileParser.h

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

    reader/pro/ProReader.h
    reader/pro/ProReader.cpp

    reader/gam/GamReader.h
    reader/gam/GamReader.cpp

    reader/StreamBuffer.cpp
    reader/StreamBuffer.h
)

target_include_directories(vault PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vault PRIVATE
    spdlog::spdlog_header_only
    ZLIB::ZLIB
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    sfml-system sfml-graphics sfml-window
    Qt6::Core
    Qt6::Widgets
    spdlog::spdlog_header_only
    vfspp
    vault
)

target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/external/vfspp/include")

# copy shared external libraries to the build directory for Windows builds
if(WIN32)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND_EXPAND_LISTS
    )
endif()

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources $<TARGET_FILE_DIR:${PROJECT_NAME}>/resources
)

# Optionally run tests automatically after build
if(GECK_AUTO_TEST)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/post-build-test.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running automatic post-build tests..."
        VERBATIM
    )
endif()
