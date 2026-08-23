# Improvement Backlog

> **This file lists only open work.** Completed items are deleted rather than marked done —
> shipped history is in `git log` and the merged-PR list, and the full design/scoping docs for
> delivered or deferred features (the two-tier scripting stack, area fill, the Luau plugin
> system, the MCP server) are in this file's git history: `git show edf773d:PLAN.md`.
> Reminder: this repo squash-merges, so `git merge-base --is-ancestor <branch> master` reports
> merged work as unmerged — use `gh pr list --state merged` to check.

*Last compacted: 2026-08-06.*

## What's next (priority order)

1. **Cave rim quality** — the known-issues list below; highest-value generation work.
2. **Biome script library** — `town.luau`, `coast.luau`; placement polish.
3. **SSL script editing** — open question: whether an in-app editor is still wanted now that
   compiling lives in VS Code + BGforge MLS (see the SSL section).
4. **Minimap / overview panel** (feature-gap audit).
5. **In-game preview mode** — idle animations first.
6. **Analysis/MCP tail** — small, self-contained items.
7. **Editor UX** — configurable keybindings + customizable toolbar (share one command/action
   table); undo residuals; log-panel follow-ups.

---

# Procedural generation

**Standing decisions (guard notes — do not re-litigate):**

- **Blind statistical generation is a dead end.** Frequency-weighted scatter still drops
  structural objects (vault doors, cars) at random hexes — *what* is scatter-able is semantic,
  not statistical. Curated per-biome palettes are the scatter primitive; an AI agent over the
  MCP is the intelligence layer; `mapSceneryHistogram` is an analysis tool, not a generator.
- **Seamless floors shipped as patch quilting** (`FloorSynth`, `api:quiltFloor*`/`quiltObjects`,
  PRs #125/#126). **Do not rebuild** the `autotile_floor`/`FloorTileSet`/Wang-variant-table
  design — it was built (A1/A2) and deleted by design in favor of Luau fills + quilting.
- **Orientation is a variant set** (pre-authored direction-specific art the editor cycles),
  never geometric rotation — F2 object art is direction-specific.
- The Tier-2 scripting runtime is **Luau + LuaBridge3** (spike-validated over sol2/PUC-Lua on
  sandbox safety); decision record in git history.

## Cave rim quality (improved, NOT hand-authored quality yet)

`cave.luau` builds the cavern from a metaball field and lines the rim with pieces learned from the
shipped caves, keyed by **rock-neighbour mask** (79% orientation-family accuracy cross-validated on
cave4, vs 53% for a compass bin). Inspect regions with `map render --crop-hex`.
*Superseded piece-selection models — do not retry:* outward-normal compass bin; edge-constraint
(Wang) `follow[prev][dir]` sequence; hand-authored per-piece override table.

**Known issues (screenshot review, 2026-07):**

- **Jagged edges** — the learned pieces still don't tile cleanly at the floor/rock boundary;
  up close the run is messy vs a hand-authored rim (mismatched faces, imperfect corners/tops).
- **Dead-straight bottom edge** — the shipped reference caves continue off the map edge, so
  their boundary there is a straight map-edge line the model mimics. Fix: detect and exclude
  off-map-edge boundary when learning from references, and don't carve generated chambers up
  to the map edge.
- **Near-edge lip cover** — shipped maps layer flat `Wall`/`Wall s.t.` fills over the tops of
  rock walls on the near (down-screen) edges; currently excluded by `isFlatWall`.

**Remaining content (complementary):**

- **Rim scenery** — shipped caves scatter ~70 Rocks/Stalagmites; the generator places 0
  (floor-quilt + `quiltObjects` transplant, or a curated palette, is a cheap first step).
- **Stamped rock formations** — extract real multi-hex formations from `cave1..4`
  (`extract_pattern`/`placeStamp`) to reuse authored composition.
- **Town/building walls** — straight `Wall s.t.` runs + door openings differ from organic rims.
- **Roof layer** for enclosed areas.

## Biome script library

Expand the `scripts/README.md` table as new generators land.

- **`town.luau`** — needs the straight-wall-run + door-opening model above.
- **`coast.luau`** — newly practical: quilting reproduces a shoreline's authored blend tiles
  from a reference map.
- **Ergonomics (minor):** normalized `[0,1]` coordinate helpers (`hexAt(fx, fy)`) and document
  the on-screen orientation convention.

## Placement polish

Footprint-aware, iso-diamond-masked placement for the curated scripts/tools; extract recurring
multi-object clusters as **prefabs** (place a rock formation as one unit).

## Smarter exit placement

Exits should sit where the map actually *leads out*, not on a blind rectangle. The primitive
(`placeExitGridRect`) + the directional-art mapping are the reusable foundation; feed them
terrain-derived locations:

- **At the ends of roads/paths** — once the generator lays roads (or a path graph), drop an
  exit cluster where a road runs off the playable area, oriented along the road. Needs the
  generator to retain road endpoints + headings (it currently keeps no such structure).
- **Along the real map edge** — trace the iso playable boundary (the diamond, not an
  axis-aligned box) and place exits on the edge segments the design wants open. Reuse the
  screen→hex edge walk from `placeExitGridRect`, following the diamond boundary with a
  per-edge open/closed mask.
- **Reachability-aware placement** — placement should consult reachability, not just have
  `generate` report an unreachable exit grid after the fact.

## Open questions

- **`findProtos` scope/cost:** scan all proto types into one cached index, or per-type
  (`findScenery`/`findWall`) to bound the first-call scan? Lean: one cached index, documented.
- **Coordinate convention:** expose `(col,row)` as the engine's storage layout or remap to
  match the editor's displayed coordinates? Pick one and document it.
- **Collision policy** when a generator/stamp targets an occupied hex/tile
  (overwrite / skip / error).
- **Multi-elevation prefabs** (store/stamp across the 3 `ELEVATION_COUNT` slots?).
- **Scripts in patterns** (object scripts via `programIndex`; spatial scripts) — deferred;
  programIndex is portable, SID/OID re-allocated at stamp.
- **Freeform selection** (lasso / flood fill / magic wand) — selection is one `FloatRect` +
  discrete items today; a future selection primitive, prerequisite for non-rect area fills.
- **Fill-preview cost on huge selections** — bounded today by debounce + the placement cap;
  open whether to additionally clip the preview to viewport-visible cells.

---

# Editor: known limitations & follow-ups

1. **Undo coverage.** *Remaining:* the pre-existing elevation add/remove
   (`MapInfoPanel` checkboxes) is still a direct mutation; a cascading script-delete when an
   object is deleted; and the command-controller actions themselves aren't integration-tested
   (they need GameResources/Qt — a `qt_tests` follow-up).
2. **Newly created scripts get `local_var_count=0` / `offset=-1`** — exactly what the engine's
   `scriptAdd` writes; locals are allocated at runtime from the `.int`. The editor does not
   parse `.int` headers, and the local-var *count* lives in `scripts.lst`, not the binary.
3. **F11 spatial placement is still dialog-driven** (enter tile/elevation/radius), not
   click-to-place with a live hex marker, radius overlay, and a new `EditorMode`.
4. **F16 per-instance kill-type and custom name are out of scope** — not exposed in the engine
   mapper and would require new serialized `MapObject` fields.
5. **Script attach reassigns the object OID** (`unknown0`) to a fresh unique id (matching the
   engine's `objectSetScript`); existing cross-references to the old OID aren't audited/rewritten.
6. **Edit visuals are sprite-rebuild only** — no engine-style `_obj_toggle_flat` outline
   recompute, multi-hex occupancy overlay, or live light-radius overlay beyond the rebuild.
7. **Keybindings are hardcoded — no user remapping.** Shortcuts are scattered and fixed: the
    menu/toolbar `QKeySequence`s in `MainWindow::setupMenuBar()`/`setupToolBar()` (New/Open/Save,
    Select All `Ctrl+A`, scroll-blocker `B`, exit grids `Ctrl+E`, undo/redo, …), the editor-mode
    keys in `InputHandler::handleKeyPressed` (`R` cycles a stamp variant, `Esc` cancels, `Delete`/
    `Backspace` deletes), and a few ad-hoc ones (F11 spatial-script, F16). There is no central
    registry or any UI to view or change them. **Add configurable keybindings:** a single
    command/action table (stable action id → default `QKeySequence` + human label/category), a
    Preferences page to rebind with live conflict detection, and persistence via `Settings`.
    Build it from the stock pieces rather than inventing them: `QAction` *is* the command table
    (it already carries the shortcut, icon, text and enabled state the menu/toolbar need), and
    `QKeySequenceEdit` is the capture widget for the rebinding page — neither is used anywhere
    in the codebase today. Drive
    the menu/toolbar `QAction`s *and* the `InputHandler` dispatch from that table instead of
    literals, so a rebind takes effect everywhere and the bindings stay discoverable. Engine-fidelity
    note: this is editor UX only — it changes no map/format data.
8. **Toolbar is a fixed button set — not user-customizable.** The primary toolbar (New, Browse Maps,
    Save, Play) is a hardcoded `primaryToolbarActions` array in `MainWindow::setupToolBar()`. Most
    editors let users choose which buttons appear and reorder them. **Add a customizable toolbar:**
    drive it from the same command/action table proposed in #7 (stable action id → icon/label/handler),
    with a context-menu / Preferences UI to add, remove, and reorder buttons, persisted via `Settings`.
    Editor UX only; no map/format change.
9. **Log panel follow-ups.** Add jump-to-source where a record carries a hex/object (needs
   structured records, not text). Editor UX only; changes no map/format data.
10. **Move the item views onto Qt's model/view where the convenience widgets are fighting us.**
    30 files use `QTableWidget`/`QListWidget`/`QTreeWidget`, 4 use a model. Three places where
    that costs something concrete — each its own change, in rising order of size:
    - **`DataPathsWidget`** — a `QAbstractTableModel` + `QTableView` would bring drag-to-reorder
      (`moveRows` + `InternalMove`) alongside the Move Up/Down buttons, and drop the manual
      renumbering pass. Note `QTableWidget` + `InternalMove` is *not* a shortcut: it moves cells
      and duplicates rows, so the model is the prerequisite.
    - **`FileBrowserPanel`** (1333 lines) — populates a `QTreeWidget` in chunks off a `QTimer`,
      which is what `canFetchMore()`/`fetchMore()` exists for. A lazy model would also stop the
      per-row `DataFileSystem` calls that contend with the loader thread.
    - **The palettes** — `GridPalettePanel` builds one widget per item in a `QGridLayout`, which
      is the only reason `PaginationWidget` exists. A `QListView` in `IconMode` with a
      `QStyledItemDelegate` recycles item views and scrolls thousands of entries, so pagination
      stops being a concept the palettes need at all.
11. **Background work uses `std::thread`/`QThread` directly; `QtConcurrent` is unused.** The
    loaders (`Loader.h`, `DataPathLoader`, `MapLoader`) each own a raw `std::thread`;
    `QtConcurrent::run` + `QFutureWatcher` would deliver completion on the GUI thread without the
    hand-rolled marshalling. **Weigh before doing:** this is the code the loading-dialog
    starvation fixes landed in, so the risk is real and the benefit is idiom rather than
    behaviour. Worth it only alongside a change that already touches the loaders.
---

# SSL script editing

**Gecko does not compile or decompile SSL.**
"Edit Script Source" resolves a program index to its `.ssl` (a data path marked as a script source,
a loose file, or a DAT extracted via `ensureWritableCopy`) and opens it in VS Code with the source
tree as the workspace root, which is what lets BGforge MLS resolve headers and compile.

**Do not rebuild the compiler integration.** It existed and was removed deliberately: `sslc` ships
no licence file and `int2ssl` is GPL-3.0, so neither can be bundled; `sslc` has no structured
diagnostic API even in DLL mode, so any integration is text-scraping its stdout; and a separate
process keeps a compiler crash out of the editor. BGforge MLS already does all of it, including
placing the `.int` via its `outputDirectory` setting.

Still open, if an in-app editor is ever wanted: it would be **edit-only** unless that decision is
revisited, since Save could not compile.

### Still unbuilt

- **Registering a new script name in `scripts.lst`** — appending a line so a fresh program index
  exists for `MapScript` to reference. Needs an Lst writer plus override semantics, and must mirror
  the engine's index-is-line-number convention (1-based in the map header, `at(index-1)` in `Lst`).
- A recompiled `.int` has to land where `ResourcePaths::Lst::SCRIPTS` (`scripts/`) expects it, or
  neither the engine nor `repository().load<Lst>` will find it.

---

# Map semantics & analysis — remaining tail

**Guiding principles (standing):** don't hardcode classification heuristics ("N critters ⇒ a
fight") — surface the engine's own semantic sources faithfully and cross-referenced (join keys:
`pid`↔proto.msg, `script_id`↔`scripts.lst`↔`.ssl`↔`.msg`, `ai_packet`↔`ai.txt`) and let the
model infer purpose. Parse engine data in the **vault** library (a `format/…` object + a
`reader/…`, like `MapsTxt`/`AiTxt`), never inline in the cli/MCP layer; keep new readers Qt-free.
Deliberately **not** building a computed "critical path to the ending" — `.ssl` is imperative
quest logic and static win-path extraction would be brittle; the MCP supplies ground truth
(quest → gvar → `find_gvar` → `describe_script`), the model infers the route.

Open items:

- **Corpus / world index** — index `analyze` + the semantic facts across all shipped maps so an
  agent can query *examples* ("how do shipped towns place and wire shopkeepers?") — improves
  generation, not just analysis.
- **`worldmap.txt`** — the per-position sub-tile *encounter* chances and fill flags are still
  unparsed (of each subtile line only the terrain is kept; the tile's `art_idx`,
  `walk_mask_name` and `encounter_difficulty` are read).
- **`worldmap.msg`** — random-encounter descriptions (`[3000 + 50*tableId + entryId]`,
  fallout2-ce worldmap.cc:3595); runtime-index-tied to the encounter table/entry ordering, so
  not a small add. (Area/city labels turned out to live in map.msg and are already surfaced.)
- **Endgame follow-ups** — `enddeath.txt` death endings (in master.dat) and the narration
  subtitle text (`text/<lang>/cuts/<narrator>.txt`).
- **`party.txt`** (companions), **`holodisk.txt`**, **`karmavar.txt`** — lore/state, lower
  priority.

*(MCP server guard note: per-call cancellation / progress notifications are deliberately not
planned — the stdio loop is synchronous and tool calls are short.)*

---

# World map

**Shipped (view-only):** `View > World Map` renders the worldmap the way the game draws it —
the `wrldmp*` tile grid (whose yellow subtile lines are part of the art) with each area's circle
blended over it. `WorldMapScene` (gecko_core) composes it in **palette-index space** and expands
to RGBA only at the end, so the markers are pixel-identical to the engine rather than an
approximation; `util/PaletteBlend` is the port of fallout2-ce's `_buildBlendTable` /
`_commonGrayTable` / `intensityColorTable`. Also on `gecko-cli map world --render <png>`.

**Guard note — do not re-derive:** `city.txt`'s `world_pos` is measured from the engine's 640x480
interface window, so a marker's real worldmap position is `world_pos - (WM_VIEW_X, WM_VIEW_Y)` =
`- (22, 21)`. The engine applies the same bias when drawing and when hit-testing, and the
hard-coded new-game start position (173, 122) only lands inside Arroyo's circle with it. Asserted
in `test_city_txt.cpp`.

Open items, roughly in order:

- **Town-map view** — per-area screen from `townmap_art_idx` / `townmap_label_art_idx` (both now
  parsed) with the `entrance_N` hotspots drawn at their `townX,townY`; double-click already opens
  an area's first map, this would let you pick *which* entrance.
- **Overlays** — terrain type, per-subtile encounter chance (needs the unparsed subtile fields,
  see the worldmap.txt item above), the `.msk` travel masks (1bpp, 44-byte rows; reproduce the
  engine's own `1 << ((x/8) & 3)` indexing rather than "fixing" it), tile `encounter_difficulty`.
- **Editing** — drag markers, change size/state/entrances. Writing `city.txt` / `worldmap.txt`
  should follow the lossless round-trip pattern used for `maps.txt` (`MapsTxtSerializer`): both
  files are heavily commented and hand-maintained, so a rewrite-from-model would destroy them.
- **Labels** are Qt text in the engine's green, not the engine's bitmap font — the AAF fonts
  don't survive being scaled, and the view zooms. Revisit only if 1:1 fidelity is wanted.

---

# Feature-gap audit vs the reference mappers

Full parity catalogue: [`docs/feature-gap-audit.md`](docs/feature-gap-audit.md), from a read-only
audit of the fallout2-ce built-in mapper and the legacy Dims mapper against Gecko.

1. **Minimap / overview** *(M)* — click-to-navigate + elevation switch, with a viewport rectangle
   (improving on Dims' cursor-sprite locator).

Deferred (substitute exists / niche): object clipboard copy-paste (pattern-stamp covers it),
whole-elevation hex shift, absolute-rotation setters. Intentional non-goals and the corrected
TODO-claim table are in the audit doc.

---

# In-game preview mode (future idea)

> Status: idea / scoping. A toggle that makes the editor viewport behave more like the
> running game — idle animations play, ambient sound plays, lighting/darkness renders, and
> the editor chrome dims — so a designer can sanity-check "does this scene feel right?"
> without launching Fallout 2.

- **Idle animations — Medium.** We already decode FRM frames and `Object::setDirection` sets a
  frame's texture rect; `TextureManager` stitches FRM frames into sheets. The core work is a
  preview clock advancing each animated object's frame index (honouring FRM `fps` /
  `framesPerDirection`, looping idle anims), per-object animation state, and only animating
  on-screen objects for perf at map scale. Per-frame offset handling is already correct
  (frames anchor by `shiftX/shiftY`), so playback won't wobble. No new assets needed.
- **Lighting / darkness — Medium.** Render honouring `header.darkness` and per-object light
  (`light_radius`/`light_intensity`, already in the model) — an additive light pass / ambient
  tint in `RenderingEngine`. The data already exists; it's a rendering feature.
- **Ambient sound — Large.** SFML audio is currently **disabled** (`SFML_BUILD_AUDIO=FALSE`,
  `cmake/dependencies.cmake`), so step one is enabling it. F2 sounds are **ACM** files (custom
  ADPCM-style codec) needing a decoder, and ambient audio isn't stored in the `.map`
  (script/worldmap-driven), so "what plays here" has to come from the map script or a
  convention. Biggest, most independent lift.
- **"Game-like" chrome — Small.** A mode toggle that hides grid/overlays/selection, dims the
  panels, and centres on the player start. Cheap polish once the above exist.

Sequencing: idle animations first (highest value, reuses existing FRM decode + render), then
lighting (independent, data present), sound only if ever worth the ACM decoder. Full parity with
the running game (day/night, critter wander/AI) is **Large** and probably not worth chasing.

---

# Architecture (residuals & guard notes)

**Deferred (real but modest, do opportunistically):**
- **PanelVisibilityController.** Extracting the panel-visibility snapshot/restore/persist state
  machine (~130 LOC + 3 members) would trim `MainWindow`, but it stays `QDockWidget`/`QAction`-
  bound with no pure testable core. Fold it into the next change that touches dock/panel
  behavior rather than doing it as standalone churn.
- **Central-page switching.** The central `QStackedWidget` now has three modes (welcome, editor,
  world map) and `MainWindow` restores them by hand: `_pageBeforeWorldMap`, a silent
  `clearWorldMapAction()` for the paths that switch pages themselves, and a checkable action whose
  state has to be kept in step. Correct today, but a fourth mode should not add a fourth ad-hoc
  restore path — fold it into a small page controller when one arrives.

**Evaluated and intentionally NOT pursued (guard — churn > value):**
- **PRO/MAP serialization visitor** — the type-specific tails (mixed field widths, optional-on-
  read/unconditional-on-write fields, union/subtype dispatch) can't be expressed symmetrically;
  the read→write→read round-trip test net already provides the safety.
- **`ProFieldFactory` extraction / spacing-token consolidation / `BasePanel` re-parent** — only
  ~15 LOC genuinely shared, a materialId index-vs-value fidelity trap, and the spacing sources
  hold *different* values (consolidation would pixel-shift layouts).

**Intentional non-goal (MAP save):** we deliberately do not recompute / auto-prune the
per-elevation enable flags at save time (the engine does in `_map_save_file`) — our output is
always internally consistent and engine-loadable, and pruning risks silently dropping an
elevation the user wants. Revisit only if exact byte-parity with engine-saved maps becomes a
requirement.

---

# Luau plugin system (Feature B) — DEFERRED indefinitely

Parked 2026-07-12 (PR #122). Rationale: its marginal value over what exists — the Script Console
runs Luau that reads and edits the map, and first-party tools cover the concrete use cases — is
narrow (persistent interactive tools + third-party distribution), while its cost is permanent:
untrusted third-party code on the UI thread, the `api:` surface becoming a compatibility
contract, and a large permissions/`Gui.*`/event/packaging surface for a niche editor with a
small author pool. If a concrete third-party need appears, revive from the full design in git
history (`git show edf773d:PLAN.md`, §"Feature B").

**Kept on master (dual-use, pays off without plugins):** the `ITool`/`ToolRegistry` seam
(powers the native Fill Brush), `MapScriptApi`/`ScriptApiReference` (Console + headless
CLI/MCP), the `LuaSandboxHost` extraction, the plan-sink placement cap + Console time budget,
and the inert `MapScriptApi::retarget`/`detach` (reverting would churn a class the Console,
CLI, MCP and fills all share).
**Scrapped:** the `PluginManager` MVP (PRs #120/#121 closed unmerged; branches remain) and the
resident-VM substrate (`PluginVm`, heap cap, persistent env — removed in #122).
