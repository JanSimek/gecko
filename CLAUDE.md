# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Gecko** is a modern cross-platform Fallout 2 map editor written in C++20. It uses Qt6 for the UI framework and SFML for 2D game rendering, supporting vanilla Fallout 2 and original Mapper file formats.

## Build Commands

### Standard Build
```bash
# Configure (from project root)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build editor
cmake --build build --target gecko --config Release

# Or with make (from build directory)
make -j4
```

### Testing
```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run the current test executables directly
./build/general_tests
./build/performance_tests
./build/qt_tests
```

Tests are split into three executables, each registered with ctest: `general_tests`
(formats, readers/writers, editor logic), `performance_tests`, and `qt_tests` (Qt UI
regressions). There is no ctest label registration for filtering by category.

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
- `src/util/`: Utilities (Settings, Coordinates, helper modules)
- `src/vfs/`: Virtual file system for game archives

### Resource Management
- Injected `resource::GameResources` facade
- `DataFileSystem`, `ResourceRepository`, `FrmResolver`, and `TextureManager`
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

#### Objects With No Known Type
An object record whose PID names no known type is **legal, not corruption**. The engine's
`objectDataRead` / `objectDataWrite` (fallout2-ce `proto.cc`) both fall out of their type switch
through `default:`, so the record is the 22-field common block alone, with no type-specific tail.
RPU's `epamain1.map` and `epamain2.map` each carry 17 such records with `pid == -1`.

Reader and writer must agree here: rejecting them made both maps unreadable, and consuming the
wrong number of bytes shifts every object after one by the width of a tail that was never on disk.
Guarded by "MAP round-trip keeps an object whose PID has no known type".

#### Script Index Bases
**IMPORTANT**: Three different numbers name the same script, and mixing them up returns the
*neighbouring* script rather than an error:

| Number | Base | Where it lives |
| --- | --- | --- |
| `programIndex` — what gecko speaks | **0-based** | the `scripts.lst` array index, and an object's `MapScript::script_id` |
| map header `script_id` | 1-based | `Map::MapHeader::script_id` |
| SSL `SCRIPT_*` (FRP `headers/scripts.h`) | 1-based | the `scripts.lst` *line* number; also sfall `set_script()` ids |

gecko speaks the engine's internal 0-based index everywhere, because that is what map files actually
store and what the engine indexes `scripts.lst` with (fallout2-ce `scripts.cc`,
`scriptsGetFileName`). This is deliberate, and follows the engine-fidelity rule below. The 1-based
forms are the engine's own *inputs*, and the engine itself decrements them, so gecko normalises on
the way in at the same two places: `MapAnalyzer` resolves the header's `script_id - 1` (fallout2-ce
`map.cc`, `script->index = gMapHeader.scriptIndex - 1`), and sfall's `set_script()` opcode
decrements before validating (fallout2-ce `sfall_opcodes.cc`).

##### Common Mistake
- `SCRIPT_EPAC17 (1413)` from `headers/scripts.h` is `programIndex` **1412**. Passing the constant
  straight through names `epac18` — a plausible wrong answer, never an error.
- Prefer the `name` argument on `describe_script` / `find_script`; it has no index base to get wrong.
- Every script-shaped result echoes `sslConstant` (== `programIndex + 1`) so the two can be
  cross-checked at a glance. See `src/cli/ScriptIntrospect.h` for the full note.

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
- Ensure proper FRM loading using `resources.textures().get(frmPath)`
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
sf::Sprite sprite{ resources.textures().get(frmPath) };
object->setSprite(std::move(sprite));
object->setDirection(static_cast<ObjectDirection>(direction));
object->setHexPosition(hex);
```

## Code Architecture Notes

### Resource Management
- Pass `resource::GameResources` explicitly through constructors
- FRM files are stitched into sprite sheets by `TextureManager`
- Texture rectangles are set by `Object::setDirection()` to show single frames

### Engine Data Fidelity
- Treat Fallout 2 CE and the shipped game data files as the source of truth for editor-visible values and IDs.
- Prefer loading values from runtime data such as `proto.msg`, `perk.msg`, `stat.msg`, and related assets instead of duplicating label/value tables in UI code.
- Preserve engine IDs exactly when reading or writing formats. UI widgets should map display labels to stored engine values; do not assume `QComboBox` index is the serialized value unless the format explicitly works that way.
- Do not add fallback label tables, placeholder enum names, or substitute values when required engine data is missing or incomplete. Surface the failure explicitly and fix the loader or data path.
- When a format detail is ambiguous, check `/Users/jansimek/Development/fallout2-ce` and match the engine's parsing and naming behavior before adding editor-side constants or reinterpretations.

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

All colours, spacing and layout dimensions are centralized in `src/ui/theme/ThemeManager.h` - the single header for these. Use its constants instead of hardcoded values:

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

// Layout dimensions - use ui::constants::* (same header)
ui::constants::LIST_MIN_HEIGHT        // list/table minimum heights
ui::constants::sizes::ICON_BUTTON     // button and icon sizes
ui::constants::dock::MIN_WIDTH        // dock and dialog geometry

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

### Keyboard Shortcuts

Every shortcut is a row in one table — `src/ui/input/ActionSpec.h` (`actionSpecs()`) — read by the
menus, the toolbar, the canvas and the Preferences page. Do **not** add a `QKeySequence` literal to
`MainWindow`; add a row and bind a sink:

```cpp
#include "ui/input/ActionSpec.h"

// A menu/toolbar action (window scope): the registry sets the key and re-keys it on rebind.
_keyBindings->bind(actions::PANEL_SELECTION, action);

// A canvas shortcut (only while the map view has focus) — see installCanvasShortcuts().
auto* shortcut = new QShortcut(canvas);
shortcut->setContext(Qt::WidgetWithChildrenShortcut);
_keyBindings->bind(actions::TOOL_ROTATE, shortcut);
```

- **Scope**: single letters must be `ActionScope::Canvas`. Window-scoped, they fire while a palette
  grid or a tree has focus (typing "b" in one would place scroll blockers). Commands with modifiers
  can be `Application`.
- **Ids are persisted** in `settings.json` (`"keyBindings"`, overrides only), so never rename one
  that has shipped. Only entries differing from the default are written, so a later release can
  still move a default for users who never rebound it.
- **One key, one action** — across both scopes, since a window-scoped shortcut also fires while the
  canvas has focus. `KeyBindingRegistry::conflictingActionId()` enforces this and `test_keybindings`
  guards the shipped table; two actions on one key makes Qt fire neither.
- **Tool state-machine keys stay out of the table**: `Esc`, `Space`, `Delete`/`Backspace`, the
  Draw-edge `Enter` and the stamp `R` live in `InputHandler`, are dispatched as SFML key codes, and
  are meaningful only inside their mode. A canvas `QShortcut` *consumes* the key before
  `InputHandler` sees it, so any shortcut sharing one of those keys must be disabled for that mode
  (see `syncToolModeActions`).

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

## Git & PR Workflow

On feature branches, follow this workflow:

1. **Clean commit messages.** Describe the change in plain language. Do not include internal
   identifiers such as `WP-9`, `F11`, or other plan/task codes in commit messages or PR titles.
2. **Open a PR** for the branch once the work is ready for review.
3. **Wait for CI to finish.** Use `gh pr checks <num> --watch` (or poll `gh run list`) until all
   workflow runs complete.
4. **Read and address review feedback.** After the build finishes, read any code-review comments
   (Copilot, human reviewers) and SonarCloud/SonarQube warnings on the PR, then fix the valid ones
   and push the updates. Keep any explanatory comments you add relevant and brief — do not
   overcomment.

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

*Last updated: 2026-08-28*
*This file should be updated whenever significant architectural decisions or fixes are made.*
