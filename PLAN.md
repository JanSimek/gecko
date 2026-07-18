# Improvement Backlog

> **This file lists only open work.** Completed items are deleted rather than marked done —
> shipped history is in `git log` and the merged-PR list, and the full design/scoping docs for
> delivered or deferred features (the two-tier scripting stack, area fill, the Luau plugin
> system, the MCP server) are in this file's git history: `git show edf773d:PLAN.md`.
> Reminder: this repo squash-merges, so `git merge-base --is-ancestor <branch> master` reports
> merged work as unmerged — use `gh pr list --state merged` to check.

*Last compacted: 2026-07-15.*

## What's next (priority order)

1. **Cave rim quality** — the known-issues list below; highest-value generation work.
2. **Biome script library** — `town.luau`, `coast.luau`; placement polish.
3. **SSL script editing** — unstarted; Phase 1 toolchain glue is the entry point.
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

Current state: `cave.luau` generates the cavern as a metaball field (chambers = radial sources,
corridors = line sources; walkable interior = the field-≥1 isocontour, noise-warped,
flood-filled) and lines the rim with a dense 2-cell wall band. Pieces are learned from the
shipped cave maps, keyed primarily by **rock-neighbour mask** (density-independent;
cross-validated train cave1–3 / predict cave4: 79% orientation-family accuracy vs 53% for the
compass bin), with finer face-direction keys and a compass fallback; corner protos
(ca063/ca032/ca080) are keyed by their bent turn shape. Rim sealed with Secret-Blocking-Hex
fills, scroll-blocker ring, exit patch + player start; deterministic, reachability-verified.
Inspect regions with `map render --crop-hex`.
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

`cave.luau` and the quilt generators (`quilt_sampler.luau`, `quilt_biomes.luau`,
`fills/quilt_desert.luau`) are shipped; expand the `scripts/README.md` table as new ones land.

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
- **Reachability-aware placement** — the *check* shipped (`generate` runs reachability on every
  map it writes and warns when an exit grid is unreachable from the player start); still open:
  having placement consult reachability instead of just reporting after the fact.

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
    Preferences page to rebind with live conflict detection, and persistence via `Settings`. Drive
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
   structured records, not text). When the SSL-editing output panel lands, make compiler output a
   category of the existing log panel rather than a second dock. Editor UX only; changes no
   map/format data.

---

# SSL Script Editing Integration

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
- **`worldmap.txt`** — the per-position sub-tile *encounter* chances are still unparsed (only
  the terrain field is kept).
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

The 13-work-package architecture roadmap is delivered.

**Deferred (real but modest, do opportunistically):**
- **PanelVisibilityController.** Extracting the panel-visibility snapshot/restore/persist state
  machine (~130 LOC + 3 members) would trim `MainWindow`, but it stays `QDockWidget`/`QAction`-
  bound with no pure testable core. Fold it into the next change that touches dock/panel
  behavior rather than doing it as standalone churn.

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
