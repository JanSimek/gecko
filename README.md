# GECK::Mapper - Fallout 2 map editor

[![Build](https://github.com/JanSimek/geck-map-editor/workflows/Build/badge.svg)](https://github.com/JanSimek/geck-map-editor/actions) [![Codacy Badge](https://app.codacy.com/project/badge/Grade/50b6611a3e2246c6b07282f87aa5940a)](https://www.codacy.com/gh/JanSimek/geck-map-editor/dashboard?utm_source=github.com&utm_medium=referral&utm_content=JanSimek/geck-map-editor&utm_campaign=Badge_Grade)

GECK::Mapper is a modern Fallout 2 map editor written in C++20 using Qt6 Framework for the user interface and SFML for 2D graphics rendering. The application functions as both a map viewer and editor with ongoing development toward full-featured map editing capabilities.

![Screenshot](https://github.com/JanSimek/geck-map-editor/blob/master/screenshot.jpg "Screenshot")

## Features

- **Modern Qt6 Interface**: Native platform integration with menus, toolbars, and dockable panels
- **SFML Graphics Engine**: High-performance 2D rendering for game graphics
- **Advanced Selection System**: Multiple selection modes with multi-selection support and intelligent cycling
- **Map Format Support**: Full compatibility with Fallout 2 MAP files and DAT archives
- **Object Management**: View, select, and manipulate map objects with proper z-ordering
- **Tile Editing**: Support for both floor and roof tiles with visual highlighting
- **Cross-Platform**: Runs on Linux, Windows, and macOS

## Building from source

This repository contains dependencies as git submodules. When you clone the repository make sure to use the `--recursive` flag or once cloned download submodules with `git submodule update --init --recursive`

**Important**: Use Release build for performance - Debug builds are significantly slower for map loading.

### Dependencies

- **Qt6** (Core, Widgets) - Primary UI framework
- **SFML 2.5+** - 2D graphics rendering
- **spdlog** - Logging framework
- **Catch2** - Testing framework (optional)
- **vfspp** - Virtual file system for game archives
- **ZLIB** - Compression support

### Linux

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install qtbase6-dev qtbase6-dev-tools libsfml-dev libspdlog-dev

# Enter the cloned git repo folder
cd geck-map-editor
mkdir build && cd build
cmake ..
make
```

### Windows

The easiest way to build GECK on Windows is to use [vcpkg](https://vcpkg.io/) for dependency management and the latest Visual Studio for compilation.

```bash
vcpkg.exe integrate install
vcpkg.exe install --triplet x64-windows sfml qt6
```

Now just open the project in Visual Studio (File > Open > CMake...) and then build it (Build > Build All).

### macOS

Install dependencies using Homebrew:

```bash
brew install sfml qt6 spdlog

# Set CMAKE_PREFIX_PATH to Qt6 installation
export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt6:$CMAKE_PREFIX_PATH"

mkdir build && cd build
cmake ..
make
```

## Usage

### Setup

1. Copy `master.dat` and `critter.dat` from your Fallout 2 installation to the `resources` subdirectory
2. (Optional) Extract data files using [dat-unpacker](https://github.com/falltergeist/dat-unpacker) with the `--transform` option to convert filenames to lowercase

### Controls

#### Navigation
- **Arrow keys** or **Right-click + drag**: Pan the view
- **Mouse wheel**: Zoom in/out
- **Window resize**: View automatically adjusts

#### Selection Modes
The editor supports multiple selection modes accessible via the toolbar:

- **ALL**: Select any element type with intelligent cycling
  - **Priority**: Roof Tiles → Objects → Floor Tiles
  - **Cycling**: Click same position repeatedly to cycle through available elements
- **OBJECTS**: Select only objects
- **ROOF_TILES**: Select only roof tiles  
- **FLOOR_TILES**: Select only floor tiles

#### Selection Controls
- **Left mouse click**: Select element at cursor position
- **Multiple clicks on same position**: Cycle through overlapping elements (in ALL mode)
- **Right mouse click**: Clear all selections

#### Multi-Selection
- **Click and Drag**: Area selection (FLOOR_TILES, ROOF_TILES, or OBJECTS modes only)
- **Ctrl+Click**: Toggle item selection (add if not selected, remove if selected)
- **Alt+Click** (Option+Click on macOS): Add item to existing selection
- **Shift+Click**: Range selection for tiles (select area between first selected tile and clicked position)
- **Ctrl+A**: Select all items of current selection mode type
- **Ctrl+D**: Deselect all items

#### Object Manipulation
- **R key**: Rotate selected object
- **Ctrl+R**: Alternative rotation shortcut

#### File Operations
- **Ctrl+N**: Create new map
- **Ctrl+O**: Open existing map
- **Ctrl+S**: Save current map
- **Ctrl+Q**: Quit application

### Interface Elements

- **Map Info Panel**: Displays information about the currently loaded map
- **Selected Object Panel**: Shows details of the currently selected object
- **Main Toolbar**: Quick access to selection modes and common actions
- **Menu Bar**: Full access to all application features

## Architecture

The application follows a state machine pattern with clear separation of concerns:

### Core Components
- **Application**: Main application class managing Qt6 lifecycle and state machine
- **MainWindow**: Qt6 main window with menus, toolbars, and dock widgets
- **SFMLWidget**: Qt6 widget embedding SFML rendering for game graphics
- **StateMachine**: Manages application states using a stack-based approach
- **EditorState**: Main editing interface with Qt6 signal integration
- **ResourceManager**: Singleton managing game assets and VFS integration

### File Format Support
- **DAT**: Archive files containing game data
- **MAP**: Map files with tiles, objects, and scripts
- **FRM**: Sprite/animation files
- **PAL**: Color palette files
- **PRO**: Object prototype definitions
- **MSG**: Text/dialogue files

## Development

### Code Formatting
```bash
./format.sh  # Formats all .cpp and .h files with clang-format
```

### Testing
```bash
# From build directory
ctest
# Or run tests executable directly
./tests
```

### Architecture Notes
- Uses Qt6 for UI framework and SFML for 2D game rendering
- Follows modern C++20 best practices with RAII and smart pointers
- VFS integration allows reading from both loose files and DAT archives
- All file paths use `std::filesystem::path` for cross-platform compatibility

## Compatibility

The goal is to be compatible with vanilla Fallout 2, including loading maps created with the original Mapper.

## Contributing

Contributions are welcome! Please ensure your code follows the project's coding standards and includes appropriate tests.

## Credits

This project wouldn't be possible without the excellent work from:
- [Falltergeist](https://github.com/falltergeist/falltergeist/) - Fallout engine reimplementation
- [Klamath](https://github.com/adamkewley/klamath) - Fallout file format library
- [FRM-Viewer](https://github.com/Primagen/Fallout-FRM-Viewer) - Fallout graphics format viewer
- [Dims Mapper](https://github.com/FakelsHub/F2_Mapper_Dims) - Legacy Fallout 2 map editor
- [darkfo](https://github.com/darkf/darkfo) - Web-based Fallout engine

## Useful Links

- [Complete Fallout 1 & 2 Artwork Collection](https://www.nma-fallout.com/threads/the-complete-fallout-1-2-artwork.191548/)
- [Fallout File Format Documentation](https://falloutmods.fandom.com/wiki/Fallout_file_formats)
