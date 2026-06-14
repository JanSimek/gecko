# Gecko - Fallout 2 map editor

[![CI](https://github.com/JanSimek/geck-map-editor/actions/workflows/ci.yml/badge.svg)](https://github.com/JanSimek/geck-map-editor/actions/workflows/ci.yml) [![Codacy Badge](https://app.codacy.com/project/badge/Grade/50b6611a3e2246c6b07282f87aa5940a)](https://www.codacy.com/gh/JanSimek/geck-map-editor/dashboard?utm_source=github.com&utm_medium=referral&utm_content=JanSimek/geck-map-editor&utm_campaign=Badge_Grade)

*Gecko* is a modern cross-platform Fallout 2 map editor.

![Screenshot](https://github.com/JanSimek/geck-map-editor/blob/master/screenshot.jpg "Screenshot")

## Building from source

Some dependencies are bundled as git submodules, so clone with `--recursive` — or, if you already cloned, fetch them with `git submodule update --init --recursive`.

**Important**: build in Release for performance — Debug builds are significantly slower at loading maps.

### Dependencies

- **Qt6** (Core, Widgets, Svg) — primary UI framework
- **SFML 3** — 2D graphics rendering
- **spdlog** — logging framework
- **ZLIB** — compression support
- **vfspp** — virtual file system for game archives (bundled as a git submodule)

`SFML`, `spdlog` and `ZLIB` are used from your system when a compatible version is
found, and otherwise downloaded and built automatically by CMake. Qt6 must be installed.

### Linux

```bash
# Build tools and dependencies (Ubuntu/Debian)
sudo apt install cmake g++ git \
                 qt6-base-dev qt6-base-dev-tools qt6-svg-dev \
                 libspdlog-dev zlib1g-dev

# Enter the cloned git repo folder
cd geck-map-editor
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> **SFML 3 is required.** If your distribution does not package it yet (current
> Debian/Ubuntu ship SFML 2), CMake downloads and builds SFML automatically. That
> source build additionally needs the X11/OpenGL/FreeType headers:
>
> ```bash
> sudo apt install libgl1-mesa-dev libxrandr-dev libxcursor-dev libxi-dev libudev-dev libfreetype6-dev
> ```

### Windows

The easiest way to build Gecko on Windows is to use [vcpkg](https://vcpkg.io/) for dependency management and the latest Visual Studio for compilation.

```bash
vcpkg.exe integrate install
vcpkg.exe install --triplet x64-windows sfml qt6
```

Now just open the project in Visual Studio (File > Open > CMake...) and then build it (Build > Build All).

### macOS

Install dependencies using Homebrew:

```bash
brew install sfml qt6 spdlog

# Point CMake at the Homebrew Qt6 installation
export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt6:$CMAKE_PREFIX_PATH"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Usage

### Setup

1. Copy `master.dat` and `critter.dat` from your Fallout 2 installation to the `resources` subdirectory (`gecko.app/Contents/Resources` on Mac)
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
- **Left mouse click**: Select the element at the cursor
- **Multiple clicks on the same position**: Cycle through overlapping elements (ALL mode)
- **Right mouse click**: Cancel the active placement mode (right-click + drag pans the view)
- **Esc**: Clear the selection

#### Multi-Selection
- **Click and Drag**: Area selection (replaces the current selection)
- **Alt+Click** / **Alt+Drag** (Option on macOS): Add the item / covered area to the existing selection
- **Ctrl+Click** / **Ctrl+Drag**: Remove items from the selection (deselect only — never adds). Items on hidden layers are left untouched.
- **Shift+Click**: Range selection for tiles (select the area between the first selected tile and the clicked position)
- **Ctrl+A**: Select all items of the current selection mode
- **Ctrl+D**: Deselect everything

#### Object Manipulation
- **R key** or **Ctrl+R**: Rotate selected object(s) - works with single or multiple selected objects

#### File Operations
- **Ctrl+N**: Create new map
- **Ctrl+O**: Open existing map
- **Ctrl+S**: Save current map
- **Ctrl+Q**: Quit application

## Development

### Code Formatting
```bash
./format.sh  # Formats all .cpp and .h files with clang-format
```

### Testing
```bash
# Run every suite through CTest (from the build directory)
ctest --output-on-failure

# …or run a suite directly
./general_tests       # formats, readers/writers, editor logic
./performance_tests   # reader benchmarks
./qt_tests            # Qt UI regressions
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
- [Fallout2-ce](https://github.com/fallout2-ce/fallout2-ce) - Fallout 2 Community Edition engine recreation, the reference for engine data and file formats
- [Falltergeist](https://github.com/falltergeist/falltergeist/) - Fallout engine reimplementation
- [Klamath](https://github.com/adamkewley/klamath) - Fallout file format library
- [FRM-Viewer](https://github.com/Primagen/Fallout-FRM-Viewer) - Fallout graphics format viewer
- [Dims Mapper](https://github.com/FakelsHub/F2_Mapper_Dims) - Legacy Fallout 2 map editor
- [darkfo](https://github.com/darkf/darkfo) - Web-based Fallout engine
- [Tabler icons](https://tabler-icons.io/)

## Useful Links

- [Complete Fallout 1 & 2 Artwork Collection](https://www.nma-fallout.com/threads/the-complete-fallout-1-2-artwork.191548/)
- [Fallout File Format Documentation](https://falloutmods.fandom.com/wiki/Category:Fallout_and_Fallout_2_file_formats)
- [Modding guide by Femic et al.](https://f3mic.github.io/)
