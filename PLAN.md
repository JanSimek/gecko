# Improvement Backlog

- Refactor build system to split `src/CMakeLists.txt` into composable libraries (core logic, UI, widgets/pro, vault). Publish include dirs per target and remove `../../` include patterns. Downshift `cmake_minimum_required` to 3.24-ish and extract third-party FetchContent setup into `cmake/dependencies.cmake` so both CI and IDE users get consistent toolchains.

- Replace global singletons (`ResourceManager`, `Settings`, `EventBus`) with injectable services. Introduce a lightweight `ResourceContext` that loaders/widgets receive explicitly and a dedicated VFS service. This reduces coupling, simplifies tests, and enables headless or mocked runs.

- Break up `src/ui/dialogs/ProEditorDialog.cpp` (4k+ lines) into per-tab widgets and presenter-style controllers. Move shared data mapping into reusable helpers so UI code becomes maintainable and testable.

- Tame the macOS-specific `scripts/post-build-test.sh`: guard execution by platform, disable `GECK_AUTO_TEST` by default, and surface a CLI flag so Linux/CI builds don't fail. Consider replacing hard-coded paths with configurable environment variables.

- Reorganize the catch-all `util/` directory. Group UI-specific helpers under `ui/`, resource utilities under `resources/`, and platform helpers separately to improve discoverability and reduce accidental dependencies.

- Add coverage-focused tests for loaders and resource caching once dependency injection is in place, ensuring core logic is verifiable without spinning up the Qt event loop.

## Known tool-mode (EditorMode) UI gaps

These surfaced while building the `EditorMode` state machine (`EditorWidget::setMode`,
`src/ui/core/EditorMode.h`). The state machine itself is correct — these are missing
UI affordances, not regressions:

- **No "Select tool" affordance to exit a tool mode.** Once in tile painting (or
  Mark-Exits / Set-Player-Position), the only way back to plain Select is Escape /
  right-click cancel (or deselecting the tile in the palette). The "Selection mode"
  toolbar button (`_selectionModeAction`) only changes the *selection type*
  (`SelectionMode`: All/Objects/Tiles/…), **not** the tool mode (`EditorMode`), so
  clicking it does not exit painting. Fix: add a dedicated Select-tool toolbar/menu
  action that calls `EditorWidget::setMode(EditorMode::Select)` (and consider having
  the selection-type dropdown also drop the active tool mode). Pre-existing.

- **Exit-grid *placement* mode has no UI trigger.** `EditorMode::PlaceExitGrid` /
  `setExitGridPlacementMode(true)` is fully wired internally (InputHandler's
  `onExitGridPlacement` callback at `EditorWidget.cpp` → `ExitGridPlacementManager`),
  but nothing in the UI ever enters the mode, so exit-grid placement is unreachable.
  Only Mark-Exits (editing existing exit grids) has a button. Fix: add a toolbar/menu
  action to enter `PlaceExitGrid`. Pre-existing / incomplete feature.
