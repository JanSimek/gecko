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

> **Shift is modal.** It means three different things depending on what you are doing: hold
> **Shift** while placing a tile to paint on the **roof**, while clicking with tiles already
> selected to **range-select**, and while drawing an exit-grid edge to **snap to a clean angle**.

#### Navigation
- **Right-click + drag**: Pan the view
- **Mouse wheel**: Zoom in / out
- **Window resize**: View adjusts automatically

#### Selection & modes
The active mode is chosen from the toolbar; the status bar shows the keys that act in the current mode.

- **Left-click**: Select the element under the cursor. Clicking the same spot again **cycles** through overlapping elements (roof tile → object → floor tile).
- **Right-click**: Cancel the active tool / placement mode (when no tool is active, right-click + drag pans).
- **Esc**: Clear the selection (or exit the active placement / stamp tool).
- The **Selection** toolbar dropdown picks which layers participate: combine **Floor Tiles**, **Roof Tiles**, and **Objects** (all on = classic "All"), or switch to an exclusive tool (**Roof Tiles**, **Hexes**, **Scroll Blocker Rectangle**).

#### Multi-selection
- **Click + drag**: Area select (replaces the current selection).
- **Alt+Click** / **Alt+Drag** (Option on macOS): Add the item / covered area to the selection.
- **Ctrl+Click** / **Ctrl+Drag**: Remove from the selection (deselect only — never adds; items on hidden layers are left untouched).
- **Shift+Click**: Range-select tiles — selects the rectangle between the first selected tile and the clicked tile.
- **Ctrl+A**: Select all items of the current selection mode.
- **Ctrl+D**: Deselect everything.

#### Object manipulation
- **R**: Rotate the selected object(s) — works on single or multiple selections.
- **Delete** / **Backspace**: Remove the selected object(s).

#### Placement tools
- **Eyedropper — pick under cursor** (**P**): hover over the map and press **P** to sample the topmost thing under the cursor. A **tile** is loaded into the tile palette and tile painting is armed. An **object** raises the object palette and enters placement mode with a ghost that follows the cursor — **left-click** to drop a copy (keeps placing), **R** to rotate the ghost's facing, **Esc** or **right-click** to stop.
- **Tile placement** (pick a tile in the palette): **Left-click** or **drag** to paint, hold **Shift** to paint on the **roof**, **Esc** or **right-click** to exit.
- **Stamp / pattern** (Edit → Stamp Pattern): **Left-click** to place, **R** to cycle the prefab's orientation variant, **Esc** or **right-click** to cancel.
- **Set player position** (Map Info panel): **Left-click** to set the player start hex, **Esc** to cancel.
- **Exit grid — single hex** (Exit Grids tool → *Place single hex*): **Left-click** to drop one marker, **Esc** or **right-click** to exit.
- **Exit grid — Draw edge** (Exit Grids tool → *Draw edge*): **Left-click** to add a line vertex, **Space** to flip which side the bars sit on, hold **Shift** to snap the live segment to a clean exit-grid angle, **Enter** or **double-click** to finish the edge, **Esc** or **right-click** to cancel.
- **Scroll Blocker Rectangle** (**B**): Drag a rectangle to place scroll blockers along its border.

#### View / layers
- **Ctrl+E**: Toggle the "Show Exit Grids" overlay.
- **F5**: Save and play the current map in Fallout 2.
- The **View** menu (and matching toolbar buttons) toggle Objects, Critters, Walls, Roofs, Scroll Blockers, Wall Blockers, Hex Grid, and Light Overlays (no default keys).

#### File
- **Ctrl+N**: New map
- **Ctrl+O**: Open map
- **Ctrl+B**: Browse maps as thumbnails
- **Ctrl+S**: Save map
- **Ctrl+Shift+S**: Save map as…
- **Ctrl+Z** / **Ctrl+Y**: Undo / Redo
- **Ctrl+Q**: Quit

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
- [Fallout2-ce](https://github.com/fallout2-ce/fallout2-ce) - Fallout 2 Community Edition engine recreation, the reference for engine data and file formats (originally created by [@alexbatalov](https://github.com/alexbatalov))
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
