# Improvement Backlog

> **This file lists only open work.** Completed items are deleted rather than marked done —
> shipped history is in `git log` and the merged-PR list, and the full design/scoping docs for
> delivered or deferred features (the two-tier scripting stack, area fill, the Luau plugin
> system, the MCP server) are in this file's git history: `git show edf773d:PLAN.md`.
> Reminder: this repo squash-merges, so `git merge-base --is-ancestor <branch> master` reports
> merged work as unmerged — use `gh pr list --state merged` to check.

*Last compacted: 2026-10-18.*

## What's next (priority order)

1. **Minimap / overview panel** (feature-gap audit).
2. **In-game preview mode** — idle animations first.
3. **Editor UX** — customizable toolbar; undo residuals; log-panel follow-ups; the small items
   under "Editor: known limitations".
4. **Biome script library** — `town.luau`, `coast.luau`. Note: generating
   random caves proved not possible (every synthesised rim broke the wall art, see Caves), so
   `cave.luau` only copies an existing shipped cave map; expect the same limit for other biomes
   whose look depends on hand-laid wall art.
5. **SSL script editing** *(possible)* — only if an in-app editor is still wanted now that
   compiling lives in VS Code + BGforge MLS (see the SSL section).

---

# Procedural generation

**Standing decisions (guard notes — do not re-litigate):**

- **Blind statistical generation is a dead end.** Frequency-weighted scatter still drops
  structural objects (vault doors, cars) at random hexes — *what* is scatter-able is semantic,
  not statistical. Curated per-biome palettes are the scatter primitive; an AI agent over the
  MCP is the intelligence layer; `mapSceneryHistogram` is an analysis tool, not a generator.
- **Seamless floors shipped as patch quilting** (`FloorSynth`, `api:quiltFloor*`/`quiltObjects`,
  PRs #125/#126). **Do not rebuild** the `autotile_floor`/`FloorTileSet`/Wang-variant-table
  design — it was built and deleted by design in favor of Luau fills + quilting.
- **Orientation is a variant set** (pre-authored direction-specific art the editor cycles),
  never geometric rotation — Fallout 2 object art is direction-specific.
- The Tier-2 scripting runtime is **Luau + LuaBridge3** (spike-validated over sol2/PUC-Lua on
  sandbox safety); decision record in git history.

## Caves

`cave.luau` copies a whole shipped cave interior (19 found at run time) — floor, walls, scenery
verbatim via `api:copyObjects` — and adds worldmap exits and a player start.
*Synthesised cave rims — do not retry:* metaball, tile and grid-axis-room cavern shapes with
learned panel chains (`api:artRect`), per-hex rock-mask placement, compass bins, Wang edges, a
hand override table. The wall art is a panorama of strips that only reads right where the mappers
laid it: every synthesised rim showed slabs or cut edges at steps and corners. Stitching shipped
fragments at corridor cuts is data-starved: shipped floor edges are jagged, so a corridor keeps one
width for 5 tiles only ~14 times in cave1..4.

- **Variety** — every generated cave is one of the 19 shipped layouts. More would need joining
  shipped pieces with a pixel-exact re-walled seam, or more source maps (mods).

## Biome script library

Expand the `scripts/README.md` table as new generators land.

- **`town.luau`** — needs a straight `Wall s.t.` run + door-opening model, and a roof layer for
  enclosed areas.
- **`coast.luau`** — newly practical: quilting reproduces a shoreline's authored blend tiles
  from a reference map.
- **Ergonomics (minor):** normalized `[0,1]` coordinate helpers (`hexAt(fx, fy)`) and document
  the on-screen orientation convention.

## Placement polish

Footprint-aware, iso-diamond-masked placement for the curated scripts/tools. Placement today
only drops off-grid targets (`PatternStamper`); nothing checks occupancy or footprint. (Prefabs
shipped as stamps: `extract_pattern` + `api:placeStamp`.)

## Smarter exit placement

Exits should sit where the map actually *leads out*, not on a blind rectangle. The primitive
(`placeExitGridRect`) + the directional-art mapping are the reusable foundation; feed them
terrain-derived locations:

- **At the ends of roads/paths** — once the generator lays roads (or a path graph), drop an
  exit cluster where a road runs off the playable area, oriented along the road. Needs the
  generator to retain road endpoints + headings (it currently keeps no such structure).
- **Along the real map edge** — trace the iso playable boundary (the diamond, not an
  axis-aligned box) and place exits on the edge segments the design wants open. Reuse the
  screen→hex edge walk from `placeExitGridRect` (also exposed as `api:hexesOnScreenRect`),
  following the diamond boundary with a per-edge open/closed mask.
- **Reachability-aware placement** — placement should consult reachability, not just have
  `generate` report an unreachable exit grid after the fact (`warnOnUnreachableExits`).

## Open questions

- **`findProtos` (unbuilt API) scope/cost:** scan all proto types into one cached index, or
  per-type (`findScenery`/`findWall`) to bound the first-call scan? Lean: one cached index,
  documented.
- **Collision policy** when a generator/stamp targets an occupied hex/tile
  (overwrite / skip / error). Today everything stacks.
- **Multi-elevation prefabs** (store/stamp across the 3 `ELEVATION_COUNT` slots?).
- **Per-instance scripts in patterns** — stamped objects get their *proto's* script (#177), but a
  captured object's own script (`programIndex`) and spatial scripts are not carried;
  `PatternObject` has no script field. programIndex is portable, SID/OID re-allocated at stamp.
- **Freeform selection** (lasso / flood fill / magic wand) — selection is one `FloatRect` +
  discrete items today; a future selection primitive, prerequisite for non-rect area fills.
- **Fill-preview cost on huge selections** — bounded today by debounce + the placement cap;
  open whether to additionally clip the preview to viewport-visible cells.

---

# Editor: known limitations & follow-ups

1. **Undo coverage.** *Remaining:* deleting an object leaves its script behind
   (`ObjectEditService::registerObjectDeletion` never erases it; only clearing/removing an
   elevation does), and the `EditorWidget`-level handlers are untested (the command controller
   itself is covered headless by `test_object_command_controller.cpp`).
2. **Spatial script placement has no map mode.** `SpatialScriptDialog` has "Pick on map" and
   placed spatial scripts render with marker + radius, but there is no dedicated `EditorMode`
   with a live radius preview while placing.
3. **Per-instance kill-type and custom name are out of scope** — not exposed in the engine
   mapper and would require new serialized `MapObject` fields.
4. **Script attach reassigns the object OID** (`unknown0`) to a fresh unique id (matching the
   engine's `objectSetScript`); existing cross-references to the old OID aren't audited/rewritten.
5. **Edit visuals are sprite-rebuild only** — no engine-style `_obj_toggle_flat` outline
   recompute or multi-hex occupancy overlay.
6. **Toolbar is a fixed button set — not user-customizable.** The primary toolbar (New, Browse Maps,
    Save, Play) is a hardcoded `primaryToolbarActions` array in `MainWindow::setupToolBar()`. **Add a
    customizable toolbar** with a context-menu / Preferences UI to add, remove, and reorder
    buttons, persisted via `Settings`. Key the buttons by the stable `ActionSpec` ids
    (`src/ui/input/ActionSpec.h`); that table is keybinding-only today (no icon or handler), so
    it needs those fields or a lookup from id to the bound `QAction`.
7. **Log panel follow-ups.** Add jump-to-source where a record carries a hex/object (needs
   structured records, not text). Editor UX only; changes no map/format data.
8. **Item views still on the convenience widgets** — each its own change:
    - **`DataPathsWidget`** — a `QAbstractTableModel` + `QTableView` would bring drag-to-reorder
      (`moveRows` + `InternalMove`) alongside the Move Up/Down buttons, and drop the manual
      `renumberPriorities()` pass. Note `QTableWidget` + `InternalMove` is *not* a shortcut: it
      moves cells and duplicates rows, so the model is the prerequisite.
    - **`FileBrowserPanel`** — already a `QTreeView` over a `QStandardItemModel`, but still fills
      it in `QTimer` chunks (`processNextChunk`); a lazy model (`canFetchMore()`/`fetchMore()`)
      would replace that.
9. **Background work uses `std::thread` directly; `QtConcurrent` is unused.** The loaders start
    the raw `std::thread` owned by `Loader` (`DataPathLoader`, `MapLoader`);
    `QtConcurrent::run` + `QFutureWatcher` would deliver completion on the GUI thread without the
    hand-rolled marshalling. **Weigh before doing:** this is the code the loading-dialog
    starvation fixes landed in, so the risk is real and the benefit is idiom rather than
    behaviour. Worth it only alongside a change that already touches the loaders.
10. **Hex-flag placement.** Only scroll blockers can be placed (the B rectangle tool); wall
    blockers can be shown but not placed, and shoot-through exists only as an object/PRO flag.
    No palette or overlay for marking unwalkable / wall / shoot-through hexes.
11. **Play-map leaves changes behind.** `GameLauncher` restores `mods/mods_order.txt` when the
    game exits, but never undoes the `ddraw.ini` StartingMap edit, the
    `data/config/game#patch.cfg` edit or the map copied into `data/maps/`. (Steam launch was
    removed; reviving it means resolving the install dir from `libraryfolders.vdf` +
    `appmanifest_38410.acf`, then reusing the executable path's copy + config step.)
12. **Closing a dock resizes the canvas**, which rebuilds the SFML render texture. Panels
    floating over the canvas would avoid it.
13. **Tile-pattern brush.** Stamps place a captured tile+object pattern and the fill brush paints
    single tiles; nothing paints a repeating multi-tile pattern by dragging.

**Known bugs:**
- **Exit Grid Map Markers are invisible and unclickable.** Like Light Sources did, the "Exit Grid
  Map Marker" scenery (PID `0x02000031`, 741 in shipped maps) draws `art/scenery/block.frm`, one
  transparent pixel, and gives no light, so it gets no marker; it can only be caught by a drag.
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
  exists for `MapScript` to reference. Needs an Lst writer (none exists) plus override semantics;
  the new line's 0-based position is its `programIndex` (see AGENTS.md "Script Index Bases").

---

# SSL script analysis

**Goal:** one SSL front end for the editor, gecko-cli and the MCP server. `cli/GvarRefs` already
runs on it; the raw source text in `cli/ScriptIntrospect` is the last ad-hoc consumer.

**Shape** — `src/ssl/`, namespace `geck::ssl`, in `vault` (no Qt; JSON stays in `cli/`):

```
SourceProvider -> lex -> Preprocessor -> Parser -> AST (ScriptUnit) -> analysis passes -> ScriptFacts
.int -> IntDisassembler ------------------------------------------------------> ScriptFacts (subset)
ScriptIndex: corpus-level, lazily parsed, cached by (path, mtime, size); queried by editor/CLI/MCP
```

Built: `Source` (providers: disk with case-insensitive fallback, memory; `resource/VfsSourceProvider`
for the VFS), `Lexer`, `Preprocessor` (includes incl. `#include MACRO`, `#`/`##`/`__VA_ARGS__`,
conditionals, hide sets, per-token expansion chain, constant evaluation), `Parser` + `Ast`
(sslc's grammar incl. sfall for/foreach/switch/arrays/`a if c else b`, `=` as assignment, error
recovery), `ScriptUnit` (owns sources, tokens, tree), `AstWalk`, `ConstantFolder`, `CallGraph`
(sslc's keep rule: engine handlers + timed/conditional/exported + `@proc` and calls, minus dead
constant branches), `VariableRefs` (global/local var accesses in live code), `Corpus` (parallel
load), `ScriptFacts` (self team/AI writes in live code, with values; LVAR names incl. headers).
`Lst::localVars()` keeps `local_vars=N`. `find_gvar` (CLI + MCP) is built on it.
Corpus check: `GECK_SSL_CORPUS=<scripts_src> GECK_SSL_COMPILED=<data/scripts> general_tests "[.corpus]"`
— all 1513 compiled RP scripts preprocess and parse with no diagnostics, and each `.int` holds exactly
the procedures CallGraph keeps, with their argument counts (~8 ms per script, Debug; the whole tree
in ~1.7 s on all cores).

**Rules**
- sslc (`../sslc`: `lex.c`, `parse.c`, `parseext.c`, `mcpp_*.c`) defines SSL the way fallout2-ce
  defines the engine — match its behaviour. **Never copy its code**: it ships no licence.
- Script-content problems are `Diagnostic` values, never exceptions; every stage returns a result.
- Match engine opcodes after expansion (by value, engine constants), never macro spellings; the
  command.h alias names are only a fallback for when the headers are missing.
- `IntOpcodes.inc` becomes the single opcode catalog (name, opcode, arity, what it reads/writes),
  shared by the disassembler and the SSL passes.

**Known divergence (waits for a mod that needs it):** `LstReader` skips blank and `;`-only lines, but the engine
(`scripts.cc` `scriptsLoadScriptsList`) counts every line of scripts.lst as an index. The shipped and RP
files have none, so indices agree today; a modded scripts.lst with one would shift every later script.
The engine counts every line of the art lists too (`art.cc` `artReadList`). None of vanilla's 23 or the
RP's 17 `.lst` files has a blank or comment-only line, and ~24 consumers iterate `Lst::list()` (the
tile/object palettes would try to load an empty name), so this waits for a mod that needs it.

**Built on it (CLI + MCP):** `find_gvar`, `script_refs` (gvar/call/procedure/macro), `lint_script`
(local-vars past `scripts.lst`, missing dialog messages, syntax, gvar indices past vault13.gam,
timers without `timed_event_p_proc`, `create_object_sid` PIDs/scripts, constant hexes/elevations;
unused procedures and local variables as notes; `--all` adds gvars no script sets; `--map` checks each
script against where the map places it), `gecko-cli script lint <files>` (compiler-style output),
`describe_script analyze`. Gvars set but never read are deliberately not flagged: the engine reads
gvars itself (character screen, karma, quests, endings, holodisks, worldmap). `ssl/MessageRefs` lists message uses. On the RP tree the lint's errors are
real bugs, e.g. `eastereg` `local_var(JOTISZ_TILE)` (a tile number as a variable number), `nibasdor`
LVAR 6 with `local_vars=6`, `epas5` offering `epas1` messages 104/105 that `epas1.msg` lacks.

**Not built, on purpose:** a `ScriptIndex` cache. Every query re-parses the tree in ~2 s on all cores,
and keeping every parsed script would cost gigabytes; build it only when a tool needs faster answers.
`script_refs pid`/`map` (`ssl/ObjectRefs`) find a PID's uses and the `load_map` calls into a map. The RP's `template/` holds real Interplay scripts (door, containr, generic parse
cleanly); only `sprtrp0.ssl` is a placeholder sslc can't compile (the game runs vanilla's .int), so its
syntax errors are true and the folder is not skipped.

**Open — short-circuit writes in the bytecode report:** the bytecode report (`resource::reportInt`) walks back over each opcode's
arguments with the engine's stack effects (`IntOpcodes.inc`, generated from the handlers) and skips
branches under a constant condition. On the RP it shows 99.96% of what the source shows. Left: 34 RP
writes whose value contains sslc's short-circuit and/or (jumps and `OPCODE_POP`), which the walk
doesn't model, so their index stays unknown.

**Rejected:** tree-sitter + the BGforge SSL grammar — no preprocessor, and RP hides structure behind
macros, so the preprocessor, parser and analysis passes would still be needed on top of a new C
dependency.

---

# Map semantics & analysis — guard notes

**Guiding principles (standing):** don't hardcode classification heuristics ("N critters ⇒ a
fight") — surface the engine's own semantic sources faithfully and cross-referenced (join keys:
`pid`↔proto.msg, `script_id`↔`scripts.lst`↔`.ssl`↔`.msg`, `ai_packet`↔`ai.txt`) and let the
model infer purpose. Parse engine data in the **vault** library (a `format/…` object + a
`reader/…`, like `MapsTxt`/`AiTxt`), never inline in the cli/MCP layer; keep new readers Qt-free.
Deliberately **not** building a computed "critical path to the ending" — `.ssl` is imperative
quest logic and static win-path extraction would be brittle; the MCP supplies ground truth
(quest → gvar → `find_gvar` → `describe_script`), the model infers the route.

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
- **Overlays** — terrain type, per-subtile encounter chance (the data is parsed now:
  `WorldmapSubtile` `chance`/`encounterTable`, as `world_encounters --x/--y` reports), the
  `.msk` travel masks (no reader yet; only the mask name is parsed) (1bpp, 44-byte rows; reproduce the
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

1. **Minimap / overview** *(medium)* — click-to-navigate + elevation switch, with a viewport rectangle
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
- **Lighting / darkness — Medium.** Port the engine's light grid rather than approximate it:
  per elevation, 40,000 hex intensities starting at 655, each emitter deposited by
  `_obj_adjust_light` (fallout2-ce `object.cc`: linear per-ring falloff through the six-direction
  fan, shadowed by objects without `OBJECT_LIGHT_THRU`), in a Qt-free core shared with the hex
  light overlay so the overlay gains wall shadows too. A pixel then shows at
  `L = max(ambient, hex intensity)` through `intensityColorTable[colour][L / 512]` — already ported
  as `BlendTables::intensity` (`util/PaletteBlend.h`); floors interpolate L across the engine's
  sampled vertices (`tile.cc`), sprites and roofs take their hex's value. On the GPU that is a
  shader (the renderer already loads one) reading the palette index, a light-grid texture and the
  intensity LUT. `TextureManager` uploads RGBA only, so the preview also needs palette-index
  textures alongside — the largest part of this item. The editor already marks invisible lights
  (`light.frm`) and shows a selected light's reach; the preview replaces that cue, not the marker. **Not** from `header.darkness`: the engine never uses it — ambient is reset on
  map load and set by the map script (`set_light_level`, 0→16384, 50→40960, 100→65536), so the
  preview offers day / cavern / night presets, defaulting to what the map script sets when the
  SSL front end can read it.
- **Ambient sound — Large.** SFML audio is currently **disabled** (`SFML_BUILD_AUDIO=FALSE`,
  `cmake/dependencies.cmake`), so step one is enabling it. Fallout 2 sounds are **ACM** files (custom
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
- **`EditorWidget` (~3,100 lines) and `MainWindow` (~3,100 lines) keep growing.** Controllers
  already split off (`EditorController`, `EditorSession`, `ObjectPicker`, `SelectionVisualizer`,
  the `*EditService`s, `GameLauncher`), but map load / save-prompt / close
  (`handleMapLoadRequest`, `maybeSaveChanges`, `closeEvent`, `closeCurrentMap`) still live in
  `MainWindow`. Extract a workspace/document controller when next touching that flow.
- **Section-level MAP reader/writer.** `MapReader::read()` and `MapWriter::write()` are each one
  ~150–180-line function; round-trip coverage (byte-for-byte on a shipped map) is already there,
  so the split is safe whenever it's wanted.
- **Inventory edits.** `SelectionPanel` mutates `holder->inventory` itself and only records the
  snapshot through `InventoryEditService`; `InventoryViewerDialog` mutates with no undo and is
  used only by a test — delete it or route it through the service.
- One `const_cast` left: `SFMLWidget.cpp` `ensureRenderTexture` from a const method.

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
