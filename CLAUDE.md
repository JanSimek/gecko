# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Gecko** is a modern cross-platform Fallout 2 map editor written in C++20. It uses Qt6 for the UI framework and SFML for 2D game rendering, supporting vanilla Fallout 2 and original Mapper file formats.

## Build Commands

### Standard Build
```bash
# Configure (from project root)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Or with make (from build directory)
make -j4
```

### Testing
```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test categories
ctest --test-dir build -L general      # Core logic tests
ctest --test-dir build -L performance  # Performance benchmarks
ctest --test-dir build -L qt           # UI tests
```

### Code Formatting
```bash
# Format all source files (uses clang-format with WebKit style)
./format.sh
```

## Architecture Overview

### Two-Library Structure
- **gecko** (executable): UI and editor functionality
- **vault** (static library): File format handling and I/O operations

### Key Components
- `src/format/`: File format data structures (DAT, FRM, MAP, PRO, MSG, PAL)
- `src/reader/`: File format readers/parsers
- `src/writer/`: File format writers
- `src/ui/`: Qt6 interface components (dialogs, panels, widgets)
- `src/ui/rendering/`: SFML rendering engine with viewport culling
- `src/editor/`: Core editing logic (Object, HexagonGrid, Hex)
- `src/selection/`: Selection management system
- `src/util/`: Utilities (ResourceManager, Settings, Coordinates)
- `src/vfs/`: Virtual file system for game archives

### Resource Management
- Singleton `ResourceManager` with VFS integration
- Texture caching and FRM-to-sprite conversion
- DAT archive support via vfspp library

## Map Format References

### Primary Reference
- **URL**: https://falloutmods.fandom.com/wiki/MAP_File_Format
- **Description**: Comprehensive documentation of Fallout 2 MAP file format

### Key Map Structure Information

#### Coordinate Systems
**IMPORTANT**: There are two different coordinate systems in Fallout 2 maps:

1. **Tile Coordinates** (Floor/Roof tiles):
   - Grid size: 100×100 = 10,000 tiles per elevation
   - Constant: `Map::TILES_PER_ELEVATION = 10000`
   - Used for: Floor tiles, roof tiles

2. **Hex Coordinates** (Objects, movement):
   - Grid size: 200×200 = 40,000 hexes total
   - Constants: `HexagonGrid::GRID_WIDTH = 200`, `HexagonGrid::GRID_HEIGHT = 200`
   - Total hexes: `GRID_WIDTH × GRID_HEIGHT = 40,000`
   - Used for: Object placement, character movement, hex-based interactions

#### Common Mistake
- **Never validate hex positions against `TILES_PER_ELEVATION`** - this is wrong!
- Hex positions can be 0-39,999 (valid range)
- Tile positions can be 0-9,999 (valid range)

## Drag and Drop Implementation

### Object Positioning
- Objects are positioned using hex coordinates (0-39,999)
- Use `worldPosToHexPosition()` for hex coordinate conversion
- Use `_hexgrid.getHexByPosition()` to get hex data
- Objects store `position` as hex index in MapObject

### Visual Feedback
- Preview objects should use `setDirection(ObjectDirection(0))` to show single frame
- Apply semi-transparency: `setColor(sf::Color(255, 255, 255, 180))`
- Objects without MapObject need null checks in `Object::setDirection()`

## Common Issues and Solutions

### 1. Sprite Size Issues
- Ensure proper FRM loading using `ResourceManager::texture(frmPath)`
- Call `setDirection()` after `setSprite()` to set correct texture rectangle
- Check for null FRM before calling direction methods

### 2. Coordinate Validation
- Always use correct ranges for validation:
  - Hex positions: `< (HexagonGrid::GRID_WIDTH * HexagonGrid::GRID_HEIGHT)`
  - Tile positions: `< Map::TILES_PER_ELEVATION`

### 3. Object Creation Pattern
```cpp
// Standard object creation pattern (existing objects)
auto object = std::make_shared<Object>(frm);
sf::Sprite sprite{ ResourceManager::getInstance().texture(frmPath) };
object->setSprite(std::move(sprite));
object->setDirection(static_cast<ObjectDirection>(direction));
object->setHexPosition(hex);
```

## Code Architecture Notes

### Resource Management
- Use `ResourceManager::getInstance()` for loading assets
- FRM files are stitched into sprite sheets by ResourceManager
- Texture rectangles are set by `Object::setDirection()` to show single frames

### Object Hierarchy
- `MapObject`: Data structure for saving (shared_ptr in Map storage, unique_ptr only during parsing and for inventory children)
- `Object`: Visual representation with SFML sprite (shared_ptr)
- Objects can exist without MapObject for preview purposes

### Event Flow
- Qt drag events → SFMLWidget → EditorWidget
- Coordinate conversion: Qt screen → SFML window → World coordinates
- World coordinates → Hex position via `worldPosToHexPosition()`

## Code Style

### Naming Conventions
- Classes: PascalCase (`LoadingWidget`)
- Functions: camelCase (`loadMap`)
- Constants: SCREAMING_SNAKE (`TILES_PER_ELEVATION`)
- Private members: `_memberName`
- Slots: Use `onXxx()` prefix for signal-connected slots (e.g., `onSearchTextChanged`)

### C++ Standards
- C++20 required
- RAII and smart pointers throughout
- `std::filesystem::path` for cross-platform file handling

---

## Qt UI Conventions

### Theme and Styling

All colors and styles are centralized in `src/ui/theme/ThemeManager.h`. Use theme constants instead of hardcoded values:

```cpp
#include "../theme/ThemeManager.h"

// Colors - use ui::theme::colors::*
ui::theme::colors::PRIMARY        // #4A90E2 - selection, focus
ui::theme::colors::PRIMARY_LIGHT  // #E6F2FF - selected backgrounds
ui::theme::colors::SURFACE_DARK   // #F0F0F0 - preview backgrounds
ui::theme::colors::ERROR          // #D32F2F - error text
ui::theme::colors::WARNING        // #F57C00 - warning text

// Spacing - use ui::theme::spacing::*
ui::theme::spacing::TIGHT   // 4px - compact/nested layouts
ui::theme::spacing::NORMAL  // 8px - standard widget spacing
ui::theme::spacing::LOOSE   // 12px - dialog/group spacing

// Pre-built styles - use ui::theme::styles::*
setStyleSheet(ui::theme::styles::selectedWidget());
setStyleSheet(ui::theme::styles::previewArea());
setStyleSheet(ui::theme::styles::statusError());
```

### Widget Base Classes

Use the established base class hierarchy for consistency:

| Base Class | Purpose | Key Methods |
|------------|---------|-------------|
| `BaseWidget` | All custom widgets | `setupStandardVBoxLayout()`, `applySelectionStyle()` |
| `BasePanel` | Palette/browser panels | `createSearchControls()`, `createPaginationControls()` |
| `BasePaletteWidget` | Grid items (tiles, objects) | Selection painting, drag handling |

### MIME Types for Drag and Drop

Use constants from `src/ui/dragdrop/MimeTypes.h`:

```cpp
#include "../dragdrop/MimeTypes.h"

// Setting MIME data
mimeData->setData(ui::mime::GECK_OBJECT, data);

// Checking MIME format
if (mimeData->hasFormat(ui::mime::GECK_OBJECT)) { ... }
```

### Layout Best Practices

1. **Use theme spacing constants** instead of hardcoded values
2. **Parent all widgets** to ensure proper cleanup
3. **Use `BasePanel::createMainLayout()`** for consistent panel layouts
4. **Stretch factors**: Add stretch to push content (e.g., `layout->addStretch()`)

### Signal/Slot Conventions

1. Use `on*` prefix for slots connected to signals: `onSearchTextChanged()`
2. Use modern `connect()` syntax with lambdas or method pointers
3. Emit signals with `Q_EMIT` for clarity

## Development Setup

### Essential Setup
1. Copy `master.dat` and `critter.dat` from Fallout 2 to `resources/`
2. Use Release builds for performance (Debug builds are slow for map loading)
3. Run `./format.sh` before committing

### CMake Options
- `GECK_USE_SYSTEM_LIBS=ON`: Use system libraries when available
- `GECK_BUILD_TESTS=ON`: Build tests (default)
- `GECK_ENABLE_SANITIZERS=OFF`: Enable sanitizers for debugging

---

*Last updated: 2026-03-06*
*This file should be updated whenever significant architectural decisions or fixes are made.*