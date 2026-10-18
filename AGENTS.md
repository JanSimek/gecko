# AGENTS.md

Guidance for AI coding agents (Claude Code reads it through `CLAUDE.md`; Codex reads it
directly). Keep it to what an agent can't infer from the code, and fix it in the same PR when a
change makes a line here wrong.

**Gecko** is a cross-platform Fallout 2 map editor in C++20: Qt6 UI, SFML 2D rendering, a
headless CLI and an MCP server, an optional Luau scripting runtime, and an SSL script front end.

## Build and test

```bash
cmake -S . -B build-claude -DCMAKE_BUILD_TYPE=Release -DGECK_ENABLE_SCRIPTING=ON
cmake --build build-claude --target gecko gecko-cli general_tests qt_tests -j

./build-claude/general_tests                                  # all cases
./build-claude/general_tests "MAP round-trip keeps an object whose PID has no known type"
./build-claude/general_tests "[map]"                          # by tag
ctest --test-dir build-claude --output-on-failure             # everything ctest knows
```

- **Agents build in `build-claude/`**; `build/` belongs to the IDE (CLion). Use Release: Debug
  map loading is slow.
- `GECK_ENABLE_SCRIPTING` is **OFF** by default (it downloads Luau) but CI builds it ON; without
  it the Luau runtime and map-generator tests don't compile. `GECK_BUILD_CLI` /
  `GECK_BUILD_MCP` default ON.
- Executables: `gecko` (a `gecko.app` bundle on macOS), `gecko-cli`, `gecko-mcp`, and the Catch2
  v3 suites `general_tests` (formats, readers/writers, editor logic — **no Qt**),
  `performance_tests`, `qt_tests` (Qt UI, headless harness in `tests/qt/qt_test_main.cpp`).
- Tests need no game data. New test files go under `tests/{general,performance,qt}/test_<feature>.cpp`
  and must be added to `tests/CMakeLists.txt` by hand; fixtures live in `tests/data/`.
- Real game data for manual checks: `--data <dir-or-.dat>` (repeatable) on `gecko-cli` /
  `gecko-mcp`; the editor takes `-d/--data`, `-m/--map`, `--debug`, but ignores `--data` once a
  `settings.json` exists (data paths then come from Preferences).
- Engine and compiler sources to check behaviour against are sibling checkouts:
  `../fallout2-ce` (the engine) and `../sslc` (the SSL compiler — match its behaviour, never copy
  its code: it ships no licence).

### Formatting

clang-format, WebKit-based (`.clang-format`). Format only the lines you changed —
`git clang-format HEAD -- <files>` — not the whole tree: a newer local clang-format reformats
untouched code. CI does not check formatting.

### What CI enforces (`.github/workflows/ci.yml`)

- **No `#include "../…"` in `src/`** (the `include-hygiene` job). Include root-relative:
  `#include "ui/theme/ThemeManager.h"`.
- **MSVC builds with `/W4 /WX`**; Clang/GCC warn without `-Werror`. So a warning that is silent
  locally (unused variable, `class`/`struct` forward-declaration mismatch, shadowing) fails only
  the Windows job.
- Linux: Release + Debug with scripting, Release without, Debug with ASan+UBSan; tests under
  `xvfb-run` with `QT_QPA_PLATFORM=offscreen`. Windows: MSVC Release on PRs. macOS: not on PRs.
- Changes touching only `*.md` / `docs/` skip CI.

## Layout

CMake targets, lowest layer first (`src/CMakeLists.txt`):

| Target | Directories | Notes |
| --- | --- | --- |
| `vault` | `format/`, `reader/`, `writer/`, `ssl/`, parts of `util/` | Qt-free file formats and engine data parsing |
| `gecko_resource` | `resource/`, `vfs/` | Qt-free; `GameResources`, DAT archives via vfspp. See `src/resource/README.md` |
| `gecko_core` | `editor/`, `selection/`, `viewport/` | hex grid, `Object`, selection, coordinates |
| `gecko_editing` | `editing/`, `rendering/`, `pattern/`, `scripting/` | undo services, SFML `RenderingEngine`, stamps/fills, Luau |
| `gecko_app` | `ui/`, `state/` | Qt; linked by `gecko` and `qt_tests` |
| `gecko_cli`, `gecko_mcp` | `cli/`, `mcp/` | headless analysis/generation; MCP stdio server over it |

- Parse engine data in **vault** (a `format/…` type + a `reader/…`, like `MapsTxt`/`AiTxt`), never
  inline in `cli/`/`mcp/`/`ui/`, and keep it Qt-free.
- Asset access goes through an injected `resource::GameResources` (`files()`, `repository()`,
  `textures()`, `frmResolver()`) passed through constructors — no global accessors.
  `repository().load<T>` and `FrmResolver::resolve` **throw** on missing data; use
  `tryLoad<T>` / `tryLoadPro` when absence is expected. An uncaught throw in a Qt slot terminates.
- Build an object sprite with `pattern::buildSpriteObject` (`src/pattern/PatternSprite.h`). By
  hand, the order matters: `setMapObject` → `setDirection` → `setHexPosition` (see
  `MapSpriteLoader.cpp`).
- `scripts/` holds the bundled Luau scripts (`editor/`, `fills/`, `stamps/`) and documents the
  map script API (`scripts/README.md`); the build copies them into `resources/scripts`.

Docs: `PLAN.md` is the backlog (open work only — delete shipped items, don't mark them done);
`docs/` has format notes and `docs/feature-gap-audit.md`.

## Fallout 2 format rules

### Engine data fidelity

- Fallout 2 CE (`../fallout2-ce`) and the shipped data files are the source of truth for every
  editor-visible value and ID. When a format detail is ambiguous, match the engine's parsing and
  naming rather than adding an editor-side reinterpretation.
- Load labels from runtime data (`proto.msg`, `perk.msg`, `stat.msg`, …), don't duplicate tables
  in UI code. Map display labels to stored engine values; never assume a `QComboBox` index is the
  serialized value.
- No fallback label tables, placeholder enum names or substitute values when engine data is
  missing — surface the failure and fix the loader or data path.

### Two coordinate systems

| | Grid | Range | Constant | Used for |
| --- | --- | --- | --- | --- |
| Tile | 100×100 per elevation | 0–9,999 | `Map::TILES_PER_ELEVATION` | floor and roof tiles |
| Hex | 200×200 | 0–39,999 | `HexagonGrid::GRID_WIDTH * GRID_HEIGHT` | objects, movement |

Never validate a hex position against `TILES_PER_ELEVATION`. A placed object's `x`/`y` is a pixel
offset relative to its hex, not a position.

### Engine ID types

PIDs, FIDs and their enums mirror fallout2-ce's names so engine and editor code read side by side:

| gecko | fallout2-ce |
| --- | --- |
| `format/ObjTypes.h` `ObjectType`, `Rotation` | `obj_types.h` |
| `format/pro/ProtoTypes.h` `ProtoId`, `ItemType`, `SceneryType`, `*ProtoTypeId` | `proto_types.h` |
| `format/frm/FrmId.h` `FrmId`, `MiscFrameId`, `InterfaceFrameId` | `art.h`, `art_defs.h` |
| `format/map/Map.h` `MapHeaderFlags` | `map_defs.h` |

- Enumerators drop the engine prefix: `OBJ_TYPE_ITEM` → `ObjectType::Item`,
  `SCENERY_TYPE_LADDER_UP` → `SceneryType::LadderUp`.
- Decode a PID or FID through `ProtoId` / `FrmId` (or `MapObject::pid()` / `fid()`), never with
  `>> 24` or masks. A PID's type is its whole high byte; a FID's is the nibble under the rotation
  bits, and its frame id is 12 bits (`fid & 0xFFF`, engine `art.cc`).
- These hold the raw value losslessly; `valid()` is a query, so PIDs the engine would reject still
  round-trip.
- Name a well-known id with the engine's `*ProtoTypeId` / `*FrameId` enum (add the entry under its
  engine name), not a hex literal.

### Objects With No Known Type

An object record whose PID names no known type is **legal, not corruption**: the engine's
`objectDataRead` / `objectDataWrite` (fallout2-ce `proto.cc`) fall through `default:`, so the
record is the common block alone with no type-specific tail (RPU's `epamain1.map`/`epamain2.map`
carry 17 each, `pid == -1`). Reader and writer must agree, or every later object shifts. Guarded
by "MAP round-trip keeps an object whose PID has no known type".

### Script Index Bases

Three numbers name the same script, and mixing them up returns the *neighbouring* script, not an
error:

| Number | Base | Where it lives |
| --- | --- | --- |
| `programIndex` — what gecko speaks | **0-based** | the `scripts.lst` array index; an object's `MapScript::script_id` |
| map header `script_id` | 1-based | `Map::MapHeader::script_id` |
| SSL `SCRIPT_*` (`headers/scripts.h`) | 1-based | the `scripts.lst` *line* number; also sfall `set_script()` ids |

gecko uses the engine's internal 0-based index everywhere and normalises the 1-based inputs where
the engine does: the map header through `Map::MapHeader::programIndex()` (fallout2-ce `map.cc`),
sfall `set_script()` by decrementing before validating. Don't write `script_id - 1` by hand.
`SCRIPT_EPAC17 (1413)` is `programIndex` **1412**; passing the constant through names `epac18`.
Script-shaped CLI/MCP results echo `sslConstant` (== `programIndex + 1`) for cross-checking; see
`src/cli/ScriptIntrospect.h`.

### Storage

`Map` stores `MapObject`s as `shared_ptr`; inventory children are `unique_ptr`. `Object` is the
SFML sprite wrapper and may exist without a `MapObject` (previews), so it null-checks it — but
`Object::setDirection` does **not** null-check its FRM.

## Qt UI conventions

- **Theme:** every colour, spacing and layout dimension comes from `src/ui/theme/ThemeManager.h` —
  `ui::theme::colors::*` (`STATUS_ERROR`, not `ERROR`, which is a Windows macro),
  `ui::theme::spacing::{TIGHT,NORMAL,LOOSE}`, `ui::constants::*`, prebuilt
  `ui::theme::styles::*`. No hex colour literals in UI code.
- **Base classes:** dialogs derive from `BaseDialog`, palette panels from `BasePanel`
  (`createMainLayout()`, `createSearchControls()`) — both in `src/ui/common/`. Palettes are
  model/view: `ui::PaletteModel` + `ui::PaletteView` (`src/ui/palette/`).
- **Drag and drop** MIME types: `ui::mime::*` in `src/ui/dragdrop/MimeTypes.h`. Drag previews use
  `ui::constants::sfml::DRAG_PREVIEW_ALPHA`.
- **Dock width:** panels must fit `ui::constants::sizes::PANEL_PREFERRED_WIDTH` (360 px) in one
  column. Show read-only values with `ElidedLabel` (`ui/widgets/`), not read-only
  `QLineEdit`/`QSpinBox` (their minimum widths add up); fold long panels into
  `CollapsibleSection`s; let explanatory labels word-wrap. Guarded for the Selection panel by
  "Selection panel's object view fits the default dock width whatever it shows".
- **Parent every widget**; use `Q_EMIT`, modern `connect()` syntax, and `on*` names for slots.

### Keyboard shortcuts

Every shortcut is a row in `src/ui/input/ActionSpec.h` (`actionSpecs()`), read by the menus, the
toolbar, the canvas and the Preferences page. Never add a `QKeySequence` literal to `MainWindow`;
add a row and bind a sink:

```cpp
_keyBindings->bind(actions::PANEL_SELECTION, action);      // menu/toolbar QAction
auto* shortcut = new QShortcut(canvas);                    // canvas-only, in installCanvasShortcuts()
shortcut->setContext(Qt::WidgetWithChildrenShortcut);
_keyBindings->bind(actions::TOOL_ROTATE, shortcut);
```

- Single letters must be `ActionScope::Canvas`: window-scoped, they fire while a palette or tree has
  focus. `Application` actions need a modifier for letters, digits and Return (`isCanvasOnlyKey`).
- Ids are persisted in `settings.json` (`"keyBindings"`, overrides only) — never rename a shipped
  id.
- One key, one action across both scopes (`KeyBindingRegistry::conflictingActionId()`, guarded by
  `test_keybindings`); Qt fires neither when two share a key.
- Tool state-machine keys (`Esc`, `Space`, `Delete`/`Backspace`, the Draw-edge `Enter`, the stamp
  `R`) live in `InputHandler`, not the table. A canvas `QShortcut` consumes its key before
  `InputHandler` sees it, so those keys are unbindable (`isReservedKey`) and Return/R stand down in
  the modes that claim them (`syncCanvasShortcutsForMode`) — add any new mode key there too.

## Code style

Classes `PascalCase`, functions `camelCase`, constants `SCREAMING_SNAKE`, private members
`_memberName`, namespaces lowercase under `geck::`. `std::filesystem::path` for paths; RAII and
smart pointers.

## Git and PRs

- Work on a feature branch; `master` is protected (PR + green Linux and Windows checks).
- Stage explicit paths, never `git add -A`: `resources/` holds untracked DAT symlinks and `out/`
  holds build artifacts.
- Commit messages and PR titles describe the change in plain language — no plan/task codes
  (`WP-9`, `F11`, …).
- Open a PR when ready, wait for CI (`gh pr checks <num> --watch`), then read review comments
  (Copilot, humans) and SonarCloud findings, fix the valid ones and push. Keep explanatory comments
  brief.
- **Never merge a PR**; the maintainer merges. The repo squash-merges, so check merged work with
  `gh pr list --state merged`, not `git merge-base`.
