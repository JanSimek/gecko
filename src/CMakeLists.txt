
###################################
# Gecko executable
###################################

# Define source files
set(GECK_SOURCES
    main.cpp

    Application.h
    Application.cpp

    util/ResourceManager.h
    util/ResourceManager.cpp

    util/FalloutEngineEnums.h
    util/MessageEnumUtils.h
    util/ResourceInitializer.h
    util/ResourceInitializer.cpp

    util/SpriteFactory.h
    util/SpriteFactory.cpp

    util/AmmoCaliberNames.h
    util/ProHelper.h
    util/ProHelper.cpp

    util/QtDialogs.h
    util/QtDialogs.cpp

    util/Settings.h
    util/Settings.cpp

    util/CritterFrmResolver.h
    util/CritterFrmResolver.cpp

    util/FrmThumbnailGenerator.h
    util/FrmThumbnailGenerator.cpp

    util/Types.h
    util/Exceptions.h
    util/Coordinates.h
    util/Coordinates.cpp
    util/EventBus.h
    util/SpatialIndex.h
    util/SpatialIndex.cpp

    # Qt6 UI Common
    ui/common/BaseWidget.h
    ui/common/BaseWidget.cpp
    ui/common/BasePaletteWidget.h
    ui/common/BasePanel.h
    ui/common/GridPalettePanel.h
    ui/common/GridPalettePanel.cpp

    # Qt6 UI Core
    ui/core/MainWindow.h
    ui/core/MainWindow.cpp
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
    ui/panels/InventoryPanel.h
    ui/panels/InventoryPanel.cpp

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
    ui/dialogs/AboutDialog.h
    ui/dialogs/AboutDialog.cpp

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
    ui/rendering/RenderingEngine.h
    ui/rendering/RenderingEngine.cpp
    ui/input/InputHandler.h
    ui/input/InputHandler.cpp
    ui/dragdrop/DragDropManager.h
    ui/dragdrop/DragDropManager.cpp
    ui/tiles/TilePlacementManager.h
    ui/tiles/TilePlacementManager.cpp
    ui/tools/ExitGridPlacementManager.h
    ui/tools/ExitGridPlacementManager.cpp
    ui/viewport/ViewportController.h
    ui/viewport/ViewportController.cpp

    # Selection management
    selection/SelectionState.h
    selection/SelectionState.cpp
    selection/SelectionManager.h
    selection/SelectionManager.cpp

    # Loaders (kept from state system)
    state/loader/Loader.h
    state/loader/MapLoader.cpp
    state/loader/MapLoader.h
    state/loader/DataPathLoader.cpp
    state/loader/DataPathLoader.h

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
    
    # Path utilities
    util/PathUtils.h
    
    # MapObject implementation (needs ResourceManager access)
    format/map/MapObject.cpp
    
    # Qt Resource files
    ${CMAKE_SOURCE_DIR}/resources/icons.qrc
)

# Platform-specific executable settings
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${GECK_SOURCES})
    # Add Windows resource file for icon
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/resources/gecko.rc)
elseif(APPLE)
    # Add icon to bundle
    set(MACOSX_BUNDLE_ICON_FILE gecko.icns)
    set(APP_ICON_MACOSX ${CMAKE_SOURCE_DIR}/resources/gecko.icns)
    set_source_files_properties(${APP_ICON_MACOSX} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${GECK_SOURCES} ${APP_ICON_MACOSX})
    
    # Set bundle properties
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist
        MACOSX_BUNDLE_BUNDLE_NAME "Gecko"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.gecko.editor"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
    )
else()
    add_executable(${PROJECT_NAME} ${GECK_SOURCES})
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
)

target_include_directories(vault PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vault PUBLIC
    ZLIB::ZLIB
PRIVATE
    gecko::spdlog
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SFML::System SFML::Graphics SFML::Window
    Qt6::Core
    Qt6::Widgets
    Qt6::Svg
    gecko::spdlog
    vfspp
    vault
)

target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/vendor/vfspp/include"
        "${CMAKE_BINARY_DIR}/generated")

# copy shared external libraries to the build directory for Windows builds
if(WIN32)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND_EXPAND_LISTS
    )
endif()

# Copy resources to appropriate location based on platform
if(APPLE)
    # For macOS bundles, copy to Resources directory
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources $<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources/resources
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
    )
endif()
