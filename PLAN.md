# Improvement Backlog

## Test-suite audit & refactor plan

Audit of `tests/` (22 source files, ~117 `TEST_CASE`s across `general_tests`,
`performance_tests`, `qt_tests`). The format round-trip coverage (PRO all-types,
MAP all-object-types + engine-framing regressions) is genuinely strong; the gaps
are in (a) the other binary formats, (b) editor/coordinate invariants, and (c)
duplicated scaffolding plus a fragile fixture-path setup. Items are ordered by
value/effort.

### P0 — Shared test-support header (highest leverage, unblocks everything below)

Create `tests/support/` with a small, header-only support library linked into all
three executables. The two round-trip suites already re-derive the same helpers;
new format tests would copy them a third and fourth time.

- **`tests/support/Fixtures.h`** — one function `geck::test::dataDir()` /
  `dataPath(name)` that resolves fixtures from a compile-time
  `GECK_TEST_DATA_DIR` (see P1). Replaces the bare `"data/test.gam"` string
  literal repeated in `test_reading_gam.cpp`, `test_reading_msg.cpp`,
  `test_reading_pro_drug.cpp`, `test_pro_roundtrip.cpp`, and the `"data/…"` calls
  inside `test_reader_performance.cpp`.
- **`tests/support/TempFile.h`** — RAII `TempFile{stem, ".pro"}` that picks a
  unique path under `temp_directory_path()`, removes any stale copy on
  construction, and unlinks on destruction. This collapses the near-identical
  `makeTempProPath()` (test_pro_roundtrip.cpp:24) and `tempMapPath()`
  (test_map_roundtrip.cpp:44) plus the hand-written `std::error_code ec;
  std::filesystem::remove(...)` cleanup that appears at the end of **every**
  round-trip `TEST_CASE` (10+ copies). Also folds in `readAllBytes()`
  (test_pro_roundtrip.cpp:15) for byte-for-byte comparisons.
- **`tests/support/ProStubProvider.h`** — promote `StubProvider` from
  test_map_roundtrip.cpp (the in-memory `pid -> Pro` map with `addItem` /
  `addScenery` / `set` / `load`) and the free `pidOf(OBJECT_TYPE, index)` helper.
  These are exactly the provider callbacks `MapReader`/`MapWriter` take, so any
  future MAP or object-query test needs them. Keep `StubProvider` returning raw
  subtype-only `Pro`s (the only thing (de)serialization dereferences).
- **`tests/support/MapObjectBuilder.h`** — promote `fillBase()` / `checkBase()`
  (test_map_roundtrip.cpp:54/76) and the local `add(proPid, seed)` lambda into a
  reusable builder/asserter over the 22-field base block. This is the single
  biggest duplicated block and is needed verbatim by the MAP fixture test (P2)
  and any editor/selection test that fabricates objects.
- **`tests/support/ProBuilder.h`** — the `roundTrip(Pro, path)` helper
  (test_pro_roundtrip.cpp:183) plus thin constructors that set a PID with the
  correct type nibble (`(type<<24)|index`), currently re-spelled in every PRO
  `TEST_CASE`.

Wire it up in `tests/CMakeLists.txt` as
`add_library(test_support INTERFACE)` with
`target_include_directories(test_support INTERFACE ${CMAKE_SOURCE_DIR}/tests)`,
and link it into `general_tests`, `performance_tests`, `qt_tests`. Migrate the two
existing round-trip suites onto it in the same change to prove the API.

### P1 — Make fixture loading robust (fixes the ctest-only gotcha)

**Problem:** `general_tests` / `performance_tests` load fixtures via bare relative
paths (`reader.openFile("data/test.gam")`). These only resolve because
`catch_discover_tests` runs each test with its working directory defaulting to
`$<TARGET_FILE_DIR>` (the build dir, where the post-build `copy_directory` rule
drops `data/`). Running the executable directly from any other cwd
(`./build/general_tests`, a debugger, an IDE) silently fails to find fixtures.
Only `qt_tests` does this correctly, via the `GECK_TEST_DATA_DIR` compile
definition (tests/CMakeLists.txt:60-62).

**Fix:**
1. Add `target_compile_definitions(general_tests PRIVATE
   GECK_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")` (and the same for
   `performance_tests`). Point fixtures at the source tree so no copy step is
   needed for *reading*.
2. Route all fixture access through `test::dataPath()` (P0). Delete the bare
   `"data/…"` literals.
3. Once fixtures resolve by absolute path, the per-target
   `copy_directory tests/data` POST_BUILD commands (tests/CMakeLists.txt:108-118)
   become dead weight — drop them (keep only what `qt_tests` still needs for the
   `resources/` copy).
4. Add a `set_tests_properties(... PROPERTIES WORKING_DIRECTORY ...)` is then
   unnecessary; document in `AGENTS.md` that fixtures are absolute and tests are
   cwd-independent.

### P2 — Highest-value missing tests (format round-trips & editor invariants)

Format fidelity is the project's core promise; these are the untested formats and
the load-bearing editor math.

**Formats / readers / writers (priority order):**

1. **FRM round-trip + correctness** — `FrmReader` is exercised only for *timing*
   in `test_reader_performance.cpp` (in-memory buffer, no assertions on decoded
   frames). No correctness test exists, and there is **no `FrmWriter`** — confirm
   whether FRM is read-only; if so, add a read-correctness test against a small
   real `.frm` fixture (frame count, dimensions, per-direction offsets, pixel-0
   sanity). `new tests/general/test_reading_frm.cpp`.
2. **DAT reader correctness in `general_tests`** — `DatReader` is only touched by
   `test_dat_archive.cpp` in the **qt** suite (because it pulls the resource
   stack), exercising the vfspp path. Add a pure-`vault` test for the raw
   `DatReader` (entry table parse, compressed vs stored entries, file extraction
   bytes) using the existing `tests/data/f2_res.dat` fixture. Belongs in
   `general_tests` (no Qt needed). `new tests/general/test_reading_dat.cpp`.
3. **LST reader** — `LstReader` (used everywhere for proto/script list lookups)
   has zero coverage. Add a trivial fixture test: line parsing, comment/whitespace
   trimming, 1-based vs 0-based indexing, CRLF handling.
   `new tests/general/test_reading_lst.cpp`.
4. **PAL reader** — `PalReader` untested. Small, deterministic: 256-entry palette,
   6-bit→8-bit channel scaling, the cycling/animation indices. Pairs well with a
   `ColorUtils` test. `new tests/general/test_reading_pal.cpp`.
5. **MAP read from a real fixture** — the MAP round-trip is fully synthetic (no
   `.map` on disk). Add one small real `.map` fixture and assert header +
   representative objects/tiles parse, to catch reader assumptions the synthetic
   path can't (real elevation flags, real script sections, tile blocks).
   Reuses the P0 `StubProvider`. Extend `test_map_roundtrip.cpp` or
   `new tests/general/test_reading_map.cpp`.
6. **MSG / GAM negative + edge cases** — current tests assert a few happy-path
   lookups. Add: missing message id, the documented unclosed-`}` recovery
   (test_reading_msg.cpp has a `FIXME` for messages #1382/#32020 — turn it into a
   real expectation or an xfail), empty file, duplicate ids.

**Editor / selection / util invariants (priority order):**

7. **`Coordinates.h` round-trips & clamping** — `HexPosition` (0–39999),
   `TileIndex` (0–9999), and the world/screen/hex conversions are the project's
   most safety-critical math (the CLAUDE.md "never validate hex against
   TILES_PER_ELEVATION" rule lives or dies here). Test `isValid()` boundaries,
   `operator+/-` clamping at the 39999/9999 edges, and world→hex→world stability.
   `new tests/general/test_coordinates.cpp`.
8. **`HexagonGrid` invariants** — only 2 `TEST_CASE`s today. Add:
   `positionForCoordinates`↔`coordinatesForPosition` round-trip over the whole
   grid, `containsPosition` boundary, `tileIndexForPosition` mapping
   (200×200 hex ↔ 100×100 tile), and `rectangleBorderPositions` for a known rect.
9. **`UndoStack`** — pure, deterministic, zero deps, currently untested. Test
   push/undo/redo ordering, redo-stack invalidation on new push, the
   `_maxCommands` eviction (oldest dropped), `lastUndo/RedoLabel`, and the
   reject-on-missing-undo/redo guard (UndoStack.h:26). High value because every
   instance-edit feature in this very PLAN routes through it.
10. **`object_query` (ObjectQueries.h)** — `blocksMovement` /
    `isWallBlocker` / `isShootThroughWallBlocker` read PRO flags and gate
    movement/rendering. Testable with the P0 `StubProvider` supplying flag bits;
    no Qt needed (but needs `GameResources`, so likely a `qt_tests` resident).
11. **`SpatialIndex` / `TileSpatialIndex`** — query-correctness (point/area/radius
    membership, no duplicates across cells, update/remove consistency). Pure
    geometry over SFML rects; belongs in `general_tests`.

### P3 — Structure & categorization cleanup

The 3-executable split (general = pure C++ logic, performance = Catch2 benchmarks,
qt = needs `gecko::app` + `Qt6::Test`) is sound and the boundary is mostly clean.
Issues:

- **Naming is inconsistent.** Two conventions coexist: `test_reading_<fmt>.cpp`
  (gam/msg/pro_drug) vs `test_<fmt>_roundtrip.cpp` (pro/map) vs
  `test_<subject>.cpp` (everything else). Pick one. Recommend
  `test_<subject>.cpp` for unit/logic and reserve `_roundtrip` as a meaningful
  suffix; rename `test_reading_gam/msg/pro_drug` → `test_gam/test_msg/test_pro`
  (and fold `test_reading_pro_drug` into the PRO suite, since `test_pro_roundtrip`
  already owns the drug fixture). This also de-confuses "reading" tests that
  actually only read vs the round-trip ones.
- **`test_reading_pro_drug.cpp` is redundant.** Its single happy-path drug parse
  is a strict subset of `test_pro_roundtrip.cpp`'s Level-1 case (same fixture).
  Merge and delete.
- **No tag-based categorization.** Catch2 tags exist (`[map]`, `[pro]`,
  `[selection_state]`, …) but `ctest` can't filter by them (AGENTS/CLAUDE both
  note "no ctest label registration"). Low priority, but `catch_discover_tests`
  supports `TEST_PREFIX`/`PROPERTIES LABELS` — consider emitting one ctest label
  per top-level area so `ctest -L format` works.
- **`test_selection_system.cpp` is a 1631-line, 24-`TEST_CASE` monolith** (a third
  of all test LOC). It mixes `SelectionState`, tile/hex coordinate conversions,
  selection-mode enums, and "original" position-conversion cases. Split into
  `test_selection_state.cpp`, `test_tile_hex_conversion.cpp` (overlaps
  `test_tile_utils.cpp` — consolidate), and fold the coordinate cases into the new
  `test_coordinates.cpp` (P2#7). Several of its coordinate `SECTION`s duplicate
  `test_tile_utils.cpp` and `test_selection_system.cpp` itself.
- **Misplacement check:** `test_data_path_resolution.cpp` and
  `test_game_data_path_resolver.cpp` are near-siblings split across qt/general by
  whether they touch `DataFileSystem`. That's defensible, but verify the general
  one doesn't transitively need Qt; if it links cleanly it's correctly placed.

### P4 — Other duplication to retire (after P0 lands)

- The **writer-then-reader block** `{ Writer w; w.openFile(p); REQUIRE(w.write(x)); }`
  immediately followed by `Reader r; auto back = r.openFile(p);` appears in every
  round-trip case in both PRO and MAP suites. The P0 `roundTrip()` /
  `MapRoundTrip()` helpers cover PRO and inline MAP; add a `mapRoundTrip(MapFile,
  StubProvider&)` to fully retire it.
- The **`pidOf` / type-nibble PID construction**
  (`(static_cast<uint32_t>(type) << 24) | index`) is hand-inlined in ~8 places
  across both suites with slightly different casts — centralize in P0.
- **Per-test temp-path + `std::error_code ec; remove(...)` cleanup** — fully
  removed by `TempFile` RAII (P0).

### Suggested sequencing

1. **P0 + P1 together** (shared support header + robust fixtures; migrate the two
   existing round-trip suites onto them as the proof). Net LOC should *drop*.
2. **P2 formats** (FRM, DAT-in-general, LST, PAL, real-MAP fixture) — each is a
   small file that reuses P0.
3. **P2 invariants** (Coordinates, HexagonGrid, UndoStack first — pure & zero-dep;
   then object_query, SpatialIndex).
4. **P3 restructure** (renames, split the selection monolith, merge the redundant
   drug test) — purely mechanical once P0 helpers exist.

## Architecture backlog (remaining)

- **Replace the `Settings` singleton with an injected service.** `ResourceManager` and
  `EventBus` are already gone (resource access is the injected `GameResources` facade), but
  `Settings::getInstance()` remains a global. Introduce a dedicated VFS service alongside it
  so loaders/widgets receive dependencies explicitly — simplifies tests and enables
  headless/mocked runs.
- **Reorganize the catch-all `src/util/` directory** (still ~40 mixed files spanning UI
  helpers, resource utilities and platform code). Group UI-specific helpers under `ui/`,
  resource utilities under `resources/`, and platform helpers separately to improve
  discoverability and reduce accidental dependencies.
- **Loader/resource-caching tests** — the DI precondition is now met (`gecko_resource` and
  `gecko_core` are Qt-free, headless-linkable), but no dedicated loader/cache tests exist
  yet. Tracked under P2 of the test-suite plan above.

*(Done since this backlog was first written: the four-library CMake split with per-target
includes + `cmake/dependencies.cmake`; the `ProEditorDialog` breakup into `ui/widgets/pro/`
— now 367 lines; and removal of the macOS `post-build-test.sh`.)*

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

---

## Done — engine MAP compatibility & instance/map editing

**MAP format compatibility (fixed; regression tests #89/#90 in `test_map_roundtrip.cpp`):**
object section is always 3 elevations (`objectSaveAll`/`objectLoadAll` framing), tiles keyed
by true elevation gated on the per-elevation flag bit, and MISC exit-grid trailing data only
for exit-grid PIDs (ids 16–23). *Intentional non-goal:* we deliberately do not recompute /
auto-prune the per-elevation enable flags at save time (the engine does in `_map_save_file`) —
our output is always internally consistent and engine-loadable, and pruning risks silently
dropping an elevation the user wants. Revisit only if exact byte-parity with engine-saved maps
becomes a requirement.

**Instance & map editing (F10–F17 implemented; 98 tests pass):** object flags
(`ObjectFlagsDialog`), light (`LightPropertiesDialog`), scenery destination
(`SceneryDestinationDialog`), door/container locked-jammed (`InstancePropertiesDialog`, routed
to the correct per-type field — doors `walkthrough`/openFlags, containers `unknown11`/data.flags),
critter combat + working inventory add/remove/quantity (`CritterPropertiesDialog`), object
script attach (`ScriptSelectorDialog`), spatial scripts (`SpatialScriptDialog`), and map ops
(`MapInfoPanel` clear/copy elevation). Instance edits route through a shared `registerInstanceEdit`
undo command; built-tile packing, SID encoding and script-record construction are centralized
(`util/BuiltTile.h`, `MapScript` helpers/factories); `MapObject::cloneDeep` supports copy/stamp.

## Known limitations & follow-ups

1. **Undo is partial.** Single-object instance edits (flags, light, critter, destination,
   locked/jammed) are undoable. **Inventory edits, script attach/detach, spatial-script
   creation, and map-wide ops (clear/copy elevation) are direct model mutations without undo**
   — they live in panels holding a raw `Map*`, like the pre-existing elevation add/remove.
   Routing them through `ObjectCommandController` (a generic `cloneDeep`-based vector-swap
   command for inventory/clear/copy + a script-edit command, via the existing signal-up
   pattern) is the main follow-up; also add a cascading script-delete on object deletion.
2. **Exit grids are rectangle-only** — detailed below.
3. **AI packet / script program are raw values** (engine packet numbers / scripts.lst names),
   not friendly labels — no invented label tables, per the engine-fidelity rule. Real labels
   need `data/ai.txt` parsing (AI) or the scripts.lst trailing comment (script "description" —
   see the SSL-editing section; the `.int` has no description, Q below).
4. **Newly created scripts get `local_var_count=0` / `offset=-1`** — exactly what the engine's
   `scriptAdd` writes; locals are allocated at runtime from the `.int`. The editor does not
   parse `.int` headers, and the local-var *count* lives in `scripts.lst`, not the binary.
5. **F11 spatial placement is dialog-driven** (enter tile/elevation/radius), not click-to-place
   with a live hex marker, radius overlay, and a new `EditorMode`. Existing spatial scripts also
   aren't visualized on the map or editable/deletable through the UI yet.
6. **F16 per-instance kill-type and custom name are out of scope** — not exposed in the engine
   mapper and would require new serialized `MapObject` fields.
7. **Script attach reassigns the object OID** (`unknown0`) to a fresh unique id (matching the
   engine's `objectSetScript`); existing cross-references to the old OID aren't audited/rewritten.
8. **Inventory "Add" uses a numeric proto index**, not a browsable item picker.
9. **Edit visuals are sprite-rebuild only** — no engine-style `_obj_toggle_flat` outline
   recompute, multi-hex occupancy overlay, or live light-radius overlay beyond the rebuild.

### Exit-grid shapes (rectangle-only) — limitation #2 in detail

**Current behaviour:** the reachable exit-grid creation path
(`ExitGridPlacementManager::selectExitGridsInArea`) fills a **rectangular** drag selection — it
collects every hex whose footprint intersects the rectangle and makes an exit-grid object on
each, so only axis-aligned rectangular exit-grid regions can be authored.

**Engine reality (investigated):** exit grids are **independent per-hex MISC objects**
(pid 16–23), each carrying its own exit destination; there is **no rectangle, perimeter, or
"interior texture" concept** in the format — the player triggers the transition by stepping on
any exit-grid hex. The fallout2-ce mapper's **"Mark Exit-Grids"** mode toggles a marker on each
**individually clicked hex** (`mapper.cc`: `markExitGridMode` → `mapper_mark_exit_grid()` on each
left-click, mapper.cc:1325), and the legacy `reference/F2_Mapper_Dims-master/Mapper` works the
same way. Because the playable map area is an iso diamond, real exit grids commonly run
**diagonally** along the screen edges — the "triangular"/diagonal shapes a rectangle fill cannot
represent.

**Fix:** drop the rectangle-fill assumption and support arbitrary-shape placement. We already
have per-hex placement (`ExitGridPlacementManager::placeExitGridAtPosition`), but it's gated
behind `EditorMode::PlaceExitGrid`, which has **no UI trigger** (see the EditorMode gap above) —
wiring that one button gives engine-equivalent per-hex marking. Optionally add line/freehand
placement for fast diagonal runs. There is no perimeter/interior distinction to preserve: every
exit-grid hex is a full, independent exit-grid object.

---

## SSL Script Editing Integration

### Goal
Let users view and edit the Fallout 2 SSL script behind a `scripts.lst` entry from inside Gecko, instead of alt-tabbing to an external toolchain. This connects to our existing model: `scripts.lst` is a flat name list whose line index is the *program index* stored in the map header's `script_id` and in each `MapScript.script_id` (see `MapInfoPanel.cpp:397-403`, `ScriptSelectorDialog`, `SpatialScriptDialog`). Today Gecko only *references* scripts by index/name — it cannot open the source. The `.ssl` source compiles to `.int` bytecode (what the engine actually loads), which lives under `scripts/` alongside `scripts.lst`.

### The three external tools

| Tool | Purpose | Lang | License | Cross-platform | Ships binaries |
|------|---------|------|---------|----------------|----------------|
| **sslc** (sfall-team) | `.ssl` → `.int` compiler | C | **No LICENSE file** (`license: null` on GitHub; Watcom-derived heritage, bundles `mcpp` preprocessor) | CMake; releases include `compile.exe`, `sslc-linux`, **and a wasm/emscripten/node build** | Yes |
| **int2ssl** (falltergeist) | `.int` → `.ssl` decompiler | C++ | **GPL-3.0** | CMake (Win/macOS/Linux); release ships `int2ssl.exe` only | Win only prebuilt |
| **BGforge-MLS** | VS Code ext **+ standalone LSP** server (syntax/completion/hover/diagnostics/go-to-def/dialog preview for SSL) | TypeScript | **Effectively unstated** — `LICENSE.txt` is 0 bytes; npm says "SEE LICENSE IN LICENSE.txt" | npm `@bgforge/mls-server`, needs **Node ≥20**; runs standalone over LSP | npm; also bundles wasm **sslc** as an optional dep, so the server can compile too |

Key facts that shape the decision:
- **sslc has no license file.** Redistribution terms are unclear. Bundling its binary into Gecko's installer is a legal grey area; safest to *invoke* a user-provided/separately-fetched binary rather than vendor it into our repo/installers until licensing is clarified upstream.
- **int2ssl is GPL-3.0.** We can ship/build it as a *separate executable* we shell out to (mere aggregation — no GPL obligation on Gecko itself). We must **not** link it into our binary.
- **BGforge-MLS's empty LICENSE = all-rights-reserved by default.** Embedding/redistributing it is risky; depending on a user-installed copy (VS Code or npm) is safe; vendoring is not.
- Decompilation (int2ssl) is **lossy**: comments, original names, and some constructs don't round-trip. Treat `.int`→`.ssl` as best-effort, not authoritative.

### Option A — Embed BGforge-MLS LSP into a Qt editor widget

A Qt code-editor widget (QScintilla, or `QPlainTextEdit` + **KSyntaxHighlighting**, or a custom widget) acting as an LSP *client* talking to `@bgforge/mls-server` over stdio.

- **Effort: L.** We'd write a full LSP client (JSON-RPC framing, lifecycle, `textDocument/*`, diagnostics, completion, hover, semantic tokens), a process supervisor for the Node server, and editor-side rendering of all of it. No mature drop-in LSP client exists for Qt Widgets (Qt Creator's is internal/not reusable).
- **Pros:** Best in-app UX — same diagnostics/completion as VS Code, fully integrated, no context switch.
- **Cons / risk: High.** Requires a **Node ≥20 runtime** on the user's machine (heavy new dependency for a C++ desktop app). The MLS license is unstated, so we can't bundle the server. LSP client is a large surface to build and maintain against an upstream we don't control.
- **Cross-platform:** Node + npm install must work on Win/macOS/Linux; manageable but adds a runtime-detection/onboarding burden.
- **Verdict:** High value, but premature. Revisit only after a basic editor exists and if MLS licensing gets clarified.

### Option B — Hand off to external VS Code + sslc/int2ssl round-trip

User installs VS Code + BGforge-MLS. Gecko opens the `.ssl` in VS Code (`code <file>`); on the Gecko side we provide "Compile" (invoke sslc → `.int`, copy into `scripts/`) and "Decompile existing" (int2ssl `.int` → `.ssl`).

- **Effort: S–M.** Mostly `QProcess` plumbing + tool-path settings + parsing sslc/int2ssl exit codes and stderr into a Gecko output panel.
- **Pros:** Best editing experience for free (full MLS in VS Code); we own only the compile/place/decompile glue, which we need regardless.
- **Cons:** Hard dependency on the user having VS Code + extension; clunky two-app workflow; no live feedback inside Gecko; we don't control when the user saves.
- **Cross-platform:** `code` CLI exists on all three; sslc ships `sslc-linux`/`compile.exe` (no macOS prebuilt → users build it or we provide a CMake recipe); int2ssl ships Win-only (build from source on macOS/Linux). Tool-path config + "locate binary" UX needed.
- **Licensing:** Cleanest — everything stays a separate user-installed/shelled-out process; no vendoring.
- **Verdict:** Good pragmatic baseline; the compile/place/decompile glue is reusable by every other option.

### Option C — Built-in lightweight editor + bundled-ish toolchain (hybrid, recommended core)

A simple Gecko-native editor: `QPlainTextEdit` (or QScintilla) + **KSyntaxHighlighting** (we may already pull Qt/KDE deps; KSyntaxHighlighting ships an SSL/`*.ssl` syntax definition) — **no LSP**. Plus the Option-B compile/decompile glue. Optionally detect an installed external editor and offer "Open in VS Code" as a power-user escape hatch.

- **Effort: M.** Editor widget + highlighter wiring + reuse of B's `QProcess` compile/decompile glue + tool-path settings.
- **Pros:** Zero hard external editor dependency for basic edits; consistent in-app UX; no Node runtime; degrades gracefully (highlighting + compile, no completion). Lays the LSP-client groundwork (the editor widget) so Option A becomes incremental later.
- **Cons:** No completion/diagnostics-as-you-type (only sslc compile errors, surfaced in an output panel); KSyntaxHighlighting adds a dependency if not already present.
- **Cross-platform:** KSyntaxHighlighting is portable. sslc/int2ssl binary acquisition is the only friction (same as B) — mitigated by tool-path settings + first-run "locate/download sslc" helper. Avoid vendoring sslc binaries until its license is clarified.
- **Licensing:** Safe — we ship our own editor + highlighter; sslc/int2ssl are invoked as external processes (int2ssl GPL stays at arm's length).
- **Verdict:** **Best value-for-effort and the recommended foundation.**

### Recommendation & phased path

Adopt **C as the core**, structured so **B** falls out for free and **A** remains a future upgrade.

- **Phase 1 — Toolchain glue (S/M).** `QProcess` wrappers for sslc (compile `.ssl`→`.int`, place into `scripts/`) and int2ssl (decompile `.int`→`.ssl`); tool-path Settings + first-run "locate binary" UX; output panel for compiler errors/warnings. Wire into the existing `scripts.lst`/`MapScript` flow: from `ScriptSelectorDialog`/`SpatialScriptDialog`/`MapInfoPanel`, given a program index, resolve `scripts.lst[index]` → `scripts/<name>.int`, and offer "Edit script."
- **Phase 2 — Built-in editor (M).** `QPlainTextEdit`/QScintilla + KSyntaxHighlighting SSL highlighting. Open the `.ssl` if present, else int2ssl-decompile the `.int` (clearly flagged "decompiled, lossy"); Save → Phase-1 compile → place `.int`. Add "Open in external editor (VS Code)" detection as the escape hatch (this *is* Option B, now a menu item).
- **Phase 3 — Optional LSP upgrade (L, gated on demand + MLS licensing).** Reuse the Phase-2 widget as an LSP client to `@bgforge/mls-server` for completion/hover/diagnostics, **only if** a Node runtime is acceptable and MLS's license is clarified so we can guide installation. Do not bundle the server.

### Ties to scripts.lst + MapScript model

- The bridge from our data model to a source file is: `header.script_id` / `MapScript.script_id` (program index) → `scripts.lst` line → `scripts/<name>.int` (compiled, what the engine loads) → `scripts/source/<name>.ssl` or `scripts/<name>.ssl` (source). Current code already resolves index→name (`MapInfoPanel.cpp:397-403`); the new layer adds name→file→edit/compile.
- **Compiling a *new* script must also register it** in `scripts.lst` (append a line) so a fresh program index exists for `MapScript` to reference — mirror the engine's index-is-line-number convention exactly (1-based in the map header; `at(index-1)` in our `Lst`).
- Keep `.int` placement consistent with VFS/`ResourcePaths::Lst::SCRIPTS` (`scripts/`); a recompiled `.int` must land where the engine and our `repository().load<Lst>` lookups expect it.
- **Note:** BGforge-MLS itself already parses `scripts.lst`, MAP, and PRO formats — if we ever adopt its server, some of our format awareness overlaps, but our writers remain the source of truth for IDs.

---

# Scripting & Automation Layer (Patterns / Prefabs + Procedural Generation)

> Status: design proposal. Grounded in `src/format/map/Map.h`, `MapObject.h`,
> `src/editor/HexagonGrid.h` / `Hex.h`, `src/format/map/Tile.h`,
> `src/ui/editing/ObjectCommandController.h`, and `src/util/UndoStack.h`.

## 1. Goals & two use cases

**(1) User patterns / prefabs.** A reusable piece (tent, building wing, room) is a set
of *relative* placements: objects at hex offsets plus floor/roof tiles at tile offsets.
The user picks a pattern and stamps / drag-drops it on the map with a rotation
(0–5, F2's six hex directions). Patterns are authored once, shared as files, and
combined to assemble larger structures.

**(2) Procedural generation.** The user selects an area and fills it with a seamless
ground pattern (desert + scattered rocks; acid lake with a shore border that adapts to
edges). Endgame: a script that generates a full random map from a high-level definition.

The two cases pull in opposite directions: (1) is *fixed data* a non-programmer authors
in the editor and replays deterministically; (2) is *open-ended computation* (loops,
noise, conditionals, neighbour queries) that genuinely needs a language. This asymmetry
is the central design fact and drives the two-tier recommendation below.

## 2. Embedding option comparison (C++20 / Qt6, 2025)

Weighted on: embedding ease, sandboxing/safety, performance at map scale
(40,000 hexes / 10,000 tiles per elevation), binding ergonomics for our `Map` /
`HexagonGrid` / placement API, licensing, and modder learning curve.

| Option | Embed ease | Sandboxing | Perf @ 40k hex | Binding ergonomics | License | Learning curve | Verdict |
|---|---|---|---|---|---|---|---|
| **Lua 5.4 + sol2/sol3** | Excellent — header-only wrapper, ~25k LOC C core, trivial CMake | Strong: build a custom `_ENV`/sandbox, no default `io`/`os`/`require`; per-call instruction-count hook for timeout | Excellent (sol2 is among the fastest bindings); interpreter loop fine for tens of thousands of host calls | Best-in-class: `usertype`, overloads, `std::function`, containers, automatic shared_ptr handling | MIT (Lua) + MIT (sol2) | Lowest — the de-facto game/modding language; tiny surface area | **Primary** |
| LuaJIT + sol2 | Good, but LuaJIT stalled on 5.1 semantics + ARM/Apple-Silicon JIT caveats | Same sandbox story as Lua | Fastest, but we don't need JIT speed for I/O-bound host calls | Same as sol2 | MIT | Same as Lua | Overkill; portability risk on macOS arm64 |
| Luau (Roblox) | Moderate — C++ build, fewer turnkey C++ binding libs than sol2 | Best-in-class (sandbox + type checker designed for untrusted user scripts) | Very good (near-LuaJIT without JIT) | Good but you hand-roll more glue than sol2 | MIT | Low (Lua-family) | Strong #2 if untrusted-script safety becomes paramount |
| QuickJS (+ quickjs-ng) | Good — single C file, but you write your own C++ binding glue | Good: no ambient FS/net unless you wire it; interrupt handler for timeouts | Good; ~15% behind Lua-for-speed in micro-benchmarks, fine here | Verbose: manual `JS_NewCFunction`, class IDs, no auto C++ usertypes | MIT | JS is widely known — *broadest* non-gamedev audience | Viable alt if JS familiarity is the priority |
| duktape | Excellent (two files) | Good | Slower than QuickJS/Lua | Manual, C-style | MIT | JS | Only if footprint trumps speed |
| V8 | Poor — heavyweight, large build, version churn | Strong but huge surface | Fastest JS, irrelevant here | Complex | BSD | JS | Rejected: disproportionate for an editor plugin |
| AngelScript | Good — C++-like, registration-based | Strong (statically typed, no ambient access) | Good | Verbose registration; very C++-familiar to *us* | zlib | Medium; small community, niche | Rejected: thin ecosystem, weak modder familiarity |
| Wren | Good, small | Decent | Good | Manual | MIT | Medium | Rejected: effectively unmaintained |
| pybind11 / CPython | Embedding CPython is heavy; GIL, packaging, venv hell | Weak — sandboxing Python is notoriously hard | OK | Excellent bindings, but for *exposing C++ to Python apps*, not embedding | PSF/BSD | Python is well known | Rejected: distribution + sandboxing cost too high for a desktop editor |
| **Pure data-driven (JSON/TOML)** | Trivial (we already parse formats) | Total — data can't execute | N/A (host does the work) | N/A | n/a | Lowest (authored in-editor, no code) | **Tier 1 of the recommendation** |

Precedent worth noting: **Qt Creator 14 (2024) added a first-class plugin system built
on embedded Lua 5.4**, and sol2 remains the reference C++<->Lua binding. That is a strong
signal for a Qt6 C++20 app choosing Lua.

## 3. Recommendation — TWO-TIER design

Do **not** pick a single mechanism. Split by use case:

- **Tier 1 — Declarative JSON prefab/pattern format** for *stamping* (use case 1, and the
  "fill area with this ground/border ruleset" parts of use case 2). No code executes;
  fully data. Authored in-editor ("Save selection as pattern"), diffable, shareable,
  safe by construction, instantly replayable, and trivially undoable. This covers the
  overwhelming majority of what users want and needs **zero** scripting runtime.

- **Tier 2 — Embedded Lua (5.4 via sol2/sol3)** for *generation* (use case 2's noise,
  neighbour rules, randomness, and the eventual full-map generator). Lua scripts are the
  only place arbitrary computation lives, and they reach the map **only** through the same
  narrow host API that Tier 1's interpreter uses.

Rationale: Tier 1 keeps the common, user-facing path safe and code-free (a level designer
should never write a script to stamp a tent). Tier 2 confines the security/perf surface of
"real code" to the genuinely procedural cases, behind one audited façade. Both tiers funnel
through the **identical** host API and therefore through `ObjectCommandController`, so
**everything is undoable through one code path**.

### Why Lua over JS/AngelScript here

Lowest modder learning curve (it *is* the modding lingua franca), best C++ binding
ergonomics via sol2 (shared_ptr-aware usertypes map cleanly onto our `MapObject` /
`Hex` model), trivial sandboxing (drop `io`/`os`/`require`, custom `_ENV`, instruction
hook), MIT throughout, and a tiny build footprint that won't disturb the existing
FetchContent/clang pinning recorded in the toolchain memory.

## 4. Pattern/prefab JSON format (Tier 1)

A pattern is captured from a selection, anchored at an origin hex, storing **relative**
offsets. Object offsets are in hex space; tile offsets in tile space (per the
TILES vs HEXES distinction in CLAUDE.md — never validate one against the other's range).

```jsonc
{
  "name": "small_tent",
  "version": 1,
  "anchorHex": 19998,            // authoring origin; offsets are relative to this
  "size": { "hexW": 6, "hexH": 5 },
  "rotatable": true,
  "objects": [
    { "dxHex": 0,  "dyHex": 0, "proPid": 33555201, "frmPid": 16777345,
      "direction": 0, "flags": 0 },           // walls/scenery; pro/frm PIDs preserved verbatim
    { "dxHex": 2,  "dyHex": 0, "proPid": 33555201, "frmPid": 16777345, "direction": 2 }
  ],
  "floor": [ { "dxTile": 0, "dyTile": 0, "tileId": 271 },
             { "dxTile": 1, "dyTile": 0, "tileId": 271 } ],
  "roof":  [ { "dxTile": 0, "dyTile": 0, "tileId": 4096 } ]
}
```

Notes:
- PIDs (`pro_pid`, `frm_pid`) and `direction` are stored **verbatim** — engine IDs are
  preserved exactly, per the Engine Data Fidelity rule. The format stores no display labels.
- **Rotation** at stamp time maps both the offset vectors and each object's `direction`
  field by the chosen 0–5 step. Hex-grid rotation is not a trivial (x,y) swap; implement a
  tested `rotateHexOffset(dx, dy, steps)` against the F2 odd/even-row hex layout
  (`Hex::HEX_WIDTH=16`, `HEX_HEIGHT=12`) and `direction = (direction + steps) % 6`.
- Tier 1 needs **no scripting engine**: a C++ `PatternStamper` reads JSON and calls the
  host API directly. Lua is never required to place a prefab.

## 5. Host API (the single façade for both tiers)

One C++ class — call it `MapScriptApi` (a.k.a. the "host API") — wraps a live editing
session and is the *only* surface either tier touches. It is registered into the Lua state
as a global table for Tier 2 and called directly by `PatternStamper` for Tier 1. Every
mutator routes through `ObjectCommandController`, so undo is automatic and uniform.

Proposed surface (Lua-facing names; C++ methods mirror them):

```lua
-- Queries (no mutation)
local h    = api.getHex(x, y)              -- -> hex position (0..39999) or nil if off-map
local x,y  = api.hexToXY(pos)              -- HexagonGrid::coordinatesForPosition
local t    = api.getFloor(tilePos)         -- -> tileId ; api.getRoof(tilePos)
local pid  = api.objectAt(hexPos)          -- nil or pro_pid of an object on that hex
local r    = api.rng(seed)                 -- -> deterministic stream object: r:int(a,b), r:float(), r:chance(p)

-- Iteration over a user-selected area (rectangular or selection mask)
api.forEachHexInArea(area, function(hexPos, x, y) ... end)
api.forEachTileInArea(area, function(tilePos, tx, ty) ... end)

-- Mutators (each becomes an undoable step, auto-batched into one macro -- see 6)
api.placeObject(proPid, hexPos, { direction = 0, frmPid = ..., flags = 0 })
api.removeObjectAt(hexPos)
api.paintFloor(tileId, area)               -- or single tilePos
api.paintRoof(tileId, area)
api.stampPattern(name, hexPos, rotation)   -- loads a Tier-1 JSON prefab and replays it here
```

`area` is a host-side value (rectangle from the current selection, or a positions list),
constructed by the editor and handed to the script — scripts cannot fabricate arbitrary
file/RAM handles. `rng(seed)` is a host-provided deterministic generator (PCG/xoshiro)
so generation is reproducible and never reaches into ambient randomness.

`placeObject` mirrors the existing creation path in `EditorWidget.cpp` (~line 1396):
construct `std::make_shared<MapObject>()`, set `position`/`elevation`/`x`/`y` from the
target `Hex`, copy `pro_pid`/`frm_pid` verbatim from the PRO/ObjectInfo lookup, build the
visual `Object` + sprite, then register it — see §6.

## 6. Routing through ObjectCommandController for undo

The host API never mutates `Map` directly; it calls the same controller the UI uses:

- **`placeObject`** -> build `MapObject` + visual `Object` (the §5/EditorWidget pattern),
  then `ObjectCommandController::registerObjectPlacement(mapObject, object)`.
- **`stampPattern`** -> for each object entry, `MapObject::cloneDeep()` from a cached
  prototype (or build fresh), rotate, then `registerObjectPlacement`. cloneDeep is
  mandatory because `MapObject` is non-copyable (its `inventory` holds `unique_ptr`s);
  it deep-clones inventory so stamped containers keep their contents.
- **`paintFloor` / `paintRoof`** -> accumulate `TileChange{ elevation, tileIndex, isRoof,
  before, after }` (`src/ui/core/TileChange.h`) and call
  `ObjectCommandController::registerTileEdit(description, changes)`.
- **`removeObjectAt`** -> collect (mapObject, object) pairs and
  `registerObjectDeletion(...)`.

### Critical constraint — batch into ONE undo command

`UndoStack` (`src/util/UndoStack.h`) is a **flat `std::function`-based list with no macro
nesting and a hard `maxCommands` cap (default 100)**. A procedural fill that registers one
command per hex would (a) blow the cap and silently evict earlier history, and (b) force
the user to press Ctrl-Z thousands of times. Therefore:

- A whole stamp / area-fill / generation run **must** collapse to a **single**
  `UndoCommand` whose `undo`/`redo` closures replay the entire batch.
- Concretely: open a "scripted operation" scope, have the host API **buffer** its object
  placements/deletions and `TileChange`s instead of pushing each immediately, then on
  scope close build **one** `UndoCommand` (or extend `ObjectCommandController` with a
  `beginBatch()/endBatch(description)` that wraps `pushCommand` once). `registerTileEdit`
  already takes a *vector* of changes — that is the model to follow for objects too.
- This also bounds the redo path: one closure re-applies, one reverts. `rng(seed)` being
  deterministic means redo reproduces identical generated content.

> Action item: add `ObjectCommandController::beginBatch()/endBatch(desc)` (or an RAII
> `ScopedUndoBatch`) so script-driven multi-edits land as one history entry. Without it the
> 100-command cap makes generation unusable.

## 7. Sandboxing & limits (Tier 2 only)

- Fresh `sol::state` per run with a restricted `_ENV`: expose `api`, `math`, `string`,
  `table`, `ipairs/pairs/select/...`; **omit** `io`, `os`, `package`/`require`,
  `dofile`/`loadfile`, `debug`. No filesystem, no network, no process access.
- Instruction-count `lua_sethook` (or quota in the iteration callbacks) to abort runaway
  loops over 40,000 hexes with a clear error rather than freezing the UI.
- Run generation **off the Qt UI thread** or chunked, surfacing progress; never block the
  event loop during a full-map generate.
- Cap total buffered edits and surface failures explicitly (per the "no silent fallback"
  rule) rather than partially applying.

## 8. Suggested sequencing

1. **Host API skeleton** (`MapScriptApi`) over `ObjectCommandController`, plus
   `beginBatch/endBatch` so any multi-edit is one undo step. Unit-testable headless.
2. **Tier 1 JSON prefab**: `PatternFormat` (read/write), "Save selection as pattern",
   `rotateHexOffset` with tests, `PatternStamper`, and `stampPattern` drag-drop with
   rotation. Ships the highest-value feature with no scripting runtime.
3. **Tier 2 Lua**: integrate Lua 5.4 + sol2 via FetchContent, sandboxed state, bind the
   *same* host API, add a script console/runner. Start with area-fill generators
   (desert+rocks, acid lake + shore border).
4. **Full-map generator** as a Lua entry point consuming a definition file, reusing
   `stampPattern` for set-pieces.

## 9. Open questions

- Hex rotation correctness on the F2 odd/even-row layout (needs golden tests).
- Multi-elevation prefabs (store/stamp across the 3 `ELEVATION_COUNT` slots?).
- Collision policy on stamp (overwrite vs skip vs error when target hexes are occupied).
- Whether to lift `UndoStack::maxCommands` or rely solely on batching (batching is enough).
