# Improvement Backlog

## Architecture

The 13-work-package roadmap (`ARCHITECTURE_REVIEW.md`) is complete; no architectural backlog
remains. `src/util/` keeps only genuinely cross-cutting helpers — single-layer utilities now
live with their library (`src/resource/`, `src/ui/`).

> **Intentional non-goal (MAP save):** we deliberately do not recompute / auto-prune the
> per-elevation enable flags at save time (the engine does in `_map_save_file`) — our output is
> always internally consistent and engine-loadable, and pruning risks silently dropping an
> elevation the user wants. Revisit only if exact byte-parity with engine-saved maps becomes a
> requirement.

## Known limitations & follow-ups

1. **Undo coverage.** Instance edits (flags, light, critter, destination, locked/jammed),
   **inventory** add/remove/quantity, **clear/copy elevation**, and **script attach/detach +
   spatial-script creation** are now all undoable through `ObjectCommandController`, covered by
   `UndoStack` + `cloneDeep` unit tests. *Remaining:* the pre-existing elevation add/remove
   (`MapInfoPanel` checkboxes) is still a direct mutation; a cascading script-delete when an
   object is deleted; and the command-controller actions themselves aren't integration-tested
   (they need GameResources/Qt — a `qt_tests` follow-up).
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
10. **PRO-dialog animation preview jitters ("shaky camera").** Playback works, but each frame is
    centred on its own bounding box instead of being anchored to a fixed reference point. FRM
    frames carry per-frame signed (x, y) pixel **offsets** (the `Object::shiftX()/shiftY()`
    values) that the engine accumulates so the sprite's anchor stays put across frames; the
    preview ignores them, so frames with slightly different centres wobble. **Fix:** position
    each preview frame by its FRM offset relative to a fixed anchor (mirror how the map
    renderer/`Object` applies `shiftX/shiftY`) instead of re-centering per frame.
11. **Keybindings are hardcoded — no user remapping.** Shortcuts are scattered and fixed: the
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
12. **Toolbar is a fixed button set — not user-customizable.** The primary toolbar (New, Browse Maps,
    Save, Play) is a hardcoded `primaryToolbarActions` array in `MainWindow::setupToolBar()`. Most
    editors let users choose which buttons appear and reorder them. **Add a customizable toolbar:**
    drive it from the same command/action table proposed in #11 (stable action id → icon/label/handler),
    with a context-menu / Preferences UI to add, remove, and reorder buttons, persisted via `Settings`.
    Editor UX only; no map/format change.
13. **The writable save target is positional, not chosen.** Saving a map (and map-name edits) writes
    into the *last folder* in Data Paths — `findWritableDataPath` walks the list from the end and takes
    the first real directory (archives skipped). That's implicit and order-dependent: reordering Data
    Paths silently changes where saves land, and nothing in the UI shows which location is the writable
    one. Map saves now default to that folder's `maps/` (Save / Save As), so the choice matters.
    **Add an explicit default-writable marker:** a button in Settings → Data Paths to designate the
    selected folder as the default writable location, shown with a badge in the list and persisted in
    `Settings`; saves then target that folder regardless of list order. If none is marked, keep the
    current last-folder fallback and hint the user to pick one. DAT archives can't be marked (read-only).

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

### Generation-side exit placement — current state & smarter follow-up

**Now:** the generation API exposes `api:placeExitGrid(hex, …)` (one marker) and
`api:placeExitGridRect(centerHex, screenHalfWidth, screenHalfHeight, …)`, which walks a
**screen-space rectangle** border (the engine iso projection, so the hex run staircases) and
places the matching directional edge art on each of the four sides — reproducing the framed
rectangle shipped maps like **bhrnddst.map** use (`ExitGrid::RECT_*` in `Constants.h`). Every
marker shares one destination. `random_camp.luau` uses it to frame the playable area with a
worldmap exit. This is a fixed, centred rectangle — placement is **geometric, not terrain-aware**.

**Smarter placement (follow-up).** Exits should sit where the map actually *leads out*, not on a
blind rectangle:
- **At the ends of roads/paths** — once the generator lays roads (or a path graph), drop an exit
  cluster where a road runs off the playable area, oriented along the road, so the transition
  reads naturally. Needs the generator to retain road endpoints + headings (it currently keeps no
  such structure).
- **Along the real map edge** — trace the iso playable boundary (the diamond, not an axis-aligned
  box) and place exits on the edge segments the design wants open, leaving the rest walled. Reuse
  the screen→hex edge walk from `placeExitGridRect`, but follow the diamond boundary and accept a
  per-edge open/closed mask.
- **Reachability-gated** — only place an exit on a hex reachable from the player start (flood-fill,
  cf. the "Pathing, blocking & reachability" analysis item), so generators can't strand an exit
  behind a wall.

The primitive (`placeExitGridRect`) and the directional-art mapping are the reusable foundation;
the follow-up is feeding them terrain-derived locations instead of a centred rectangle.

### Unified exit-grid tool + polygonal drawing (UX)

Exit-grid authoring is split across **two toolbar buttons** (place-single vs the rectangular
"mark exits" drag). Fold them into **one tool with a mode dropdown**:
- **Place single** — the existing per-hex `placeExitGridAtPosition` (also closes the no-UI-trigger
  gap for `EditorMode::PlaceExitGrid` noted above).
- **Draw region** — replace the axis-aligned rectangle fill (`selectExitGridsInArea`) with a
  **polygonal / freehand** path: the user clicks vertices (or drags) and every hex on the outline /
  inside the polygon becomes an exit-grid MISC object, so the diagonal iso-edge runs that real maps
  use are authorable — directly resolving the rectangle-only limitation above.

While drawing, render each prospective hex with its **MISC exit marker texture at the correct
orientation**, tinted by destination kind: **green for an inter-map exit**, **brown for a world-map
exit** (the `-2`/`-1` sentinels; cf. the `ExitGrid::RECT_*` directional art), so the author sees the
region forming with engine-accurate art before committing. One destination per region — the
properties dialog (now name-annotated via `MapNameResolver`) still sets it.

### Map metadata editing (maps.txt / map.msg in the editor)

The editor can now *read* a map's friendly name and its exits' destinations (`MapNameResolver`:
`maps.txt` → `MapsTxt`, plus `map.msg` display names, surfaced in the exit-grid dialog). Next: let
the user **edit** that metadata.

**Surface — where it lives** (decision needed):
- **In the existing Map Info panel** *(recommended for the per-map fields)* — the current map's
  `lookup_name`, display name, music, and yes/no flags (saved / pipboy_active / automap) sit
  naturally beside the map properties the panel already shows. Lowest friction, no new surface.
- **A new tab in the Map Info panel** — if the field set grows (ambient_sfx list, per-elevation
  names, random start points), a "Map Registry" tab keeps the basic panel uncluttered.
- **A dedicated panel/dialog** — only if editing the *whole* `maps.txt` / `map.msg` table (every
  map, not just the current one) becomes a goal; heavier, likely out of scope for v1.

Recommendation: per-map fields in the **Map Info panel** (or a tab there); defer a full-table editor.

**Editability — the DAT problem.** `maps.txt` and the `*.msg` files usually live **inside
`master.dat`**, which the editor reads but must not rewrite. So before editing, **copy the file out
to the native filesystem** (a writable data root / the configured game data dir) and edit *that*
copy, which then shadows the archive entry (the VFS already layers loose files over the DAT). Flow:
on first edit, if the path resolves from a DAT, materialize it to the loose data dir, then read/write
the loose copy. Needs (a) a "writable data root" setting, (b) a VFS helper to tell whether a path is
archive-backed, and (c) a `maps.txt` *writer* — or, to preserve comments/ordering, keep the raw text
and patch only the touched keys rather than re-serializing `MapsTxt`.

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

> Status: **Tier-1 prefabs and the Tier-2 scripting core both shipped.** Tier-1
> (`src/pattern/`: format + JSON serializer, hex cube geometry, `PatternStamper`/`PatternBuilder`,
> undo-batching, pattern browser). Tier-2: the Luau runtime + sandbox + Script Console, the
> `MapScriptApi` façade, the headless `gecko-cli map analyze`/`map generate` commands, and a
> Qt-free `gecko_editing` library so the GUI, the CLI and a future MCP server drive the same
> editing code. See **§10 (what shipped)** and **§11 (improvement backlog)** for the current
> state and the remaining procedural-generation work (the generators themselves are still basic).
>
> **Caveat for the design below:** orientation is a **variant set** (pre-authored
> direction-specific variants the editor cycles through), **not** geometric rotation — F2
> object art is direction-specific, so the `rotatable` / rotate-at-stamp design in §4/§9 was
> superseded by the variant-set model.

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
| **Lua 5.4 + sol2/sol3** | Excellent — header-only wrapper, ~25k LOC C core, trivial CMake | Strong: build a custom `_ENV`/sandbox, no default `io`/`os`/`require`; per-call instruction-count hook for timeout | Excellent (sol2 is among the fastest bindings); interpreter loop fine for tens of thousands of host calls | Best-in-class: `usertype`, overloads, `std::function`, containers, automatic shared_ptr handling | MIT (Lua) + MIT (sol2) | Lowest — the de-facto game/modding language; tiny surface area | **Fallback** (lighter dep, but DIY sandbox — see decision below) |
| LuaJIT + sol2 | Good, but LuaJIT stalled on 5.1 semantics + ARM/Apple-Silicon JIT caveats | Same sandbox story as Lua | Fastest, but we don't need JIT speed for I/O-bound host calls | Same as sol2 | MIT | Same as Lua | Overkill; portability risk on macOS arm64 |
| **Luau (Roblox)** | Moderate — C++ libs built from source (brew ships only the CLI) | Best-in-class: **safe by default** (no `io`, `os` trimmed, no bytecode loaders) + `luaL_sandbox` read-only globals + interrupt CPU hook | Very good (interpreter rivals LuaJIT's) + gradual typing & `luau-lsp` | Good — **LuaBridge3 binding confirmed acceptable by spike**; `std::vector` auto-converts | MIT | Low (Lua-family) | **Primary (spike-validated)** — see decision below |
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

### Decision: Luau + LuaBridge3 (spike-validated)

A throwaway spike bound the same 3-method host-API slice + sandbox + ran the same script on
**both** stacks. Findings:

- **Sandboxing decides it.** Tier 2 exists to run *shared* (untrusted) generator scripts.
  Luau's stdlib is **safe by default** (no `io`, `os` trimmed, bytecode loaders gone) and
  `luaL_sandbox` makes globals read-only in one line. The sol2/PUC-Lua path required manually
  nilling out ~10 dangerous globals (`io os package debug require dofile loadfile load
  loadstring collectgarbage`) — forgetting one is a silent sandbox escape.
- **The one feared cost — binding without sol2 — is fine.** LuaBridge3 bound the API and
  auto-converted `std::vector` cleanly; ~7 lines vs sol2's ~4. Not the blocker the paper
  table implied.
- **Only real downside: Luau is a from-source C++ dependency** (~2.1 MB of static libs;
  brew ships only the CLI) vs sol2+Lua being ready-made brew/FetchContent kegs. Modest and
  one-time — and mitigated by the compile flag below.
- Bonus: Luau's faster interpreter + gradual typing / `luau-lsp` help an in-editor script editor.

This **reverses the earlier "sol2 primary" lean** on evidence. Keep **sol2 + Lua 5.4 (with a
hardened, audited sandbox)** as the documented fallback if minimising the build footprint ever
outranks untrusted-script safety.

## 3. Recommendation — TWO-TIER design

Do **not** pick a single mechanism. Split by use case:

- **Tier 1 — Declarative JSON prefab/pattern format** for *stamping* (use case 1, and the
  "fill area with this ground/border ruleset" parts of use case 2). No code executes;
  fully data. Authored in-editor ("Save selection as pattern"), diffable, shareable,
  safe by construction, instantly replayable, and trivially undoable. This covers the
  overwhelming majority of what users want and needs **zero** scripting runtime.

- **Tier 2 — Embedded Luau (via LuaBridge3)** for *generation* (use case 2's noise,
  neighbour rules, randomness, and the eventual full-map generator). Scripts are the only
  place arbitrary computation lives, and they reach the map **only** through the same narrow
  host API that Tier 1 uses — see the spike-validated decision above.

Rationale: Tier 1 keeps the common, user-facing path safe and code-free (a level designer
should never write a script to stamp a tent). Tier 2 confines the security/perf surface of
"real code" to the genuinely procedural cases, behind one audited façade. Both tiers funnel
through the **identical** host API and therefore through `ObjectCommandController`, so
**everything is undoable through one code path**.

### Why the Lua family over JS/AngelScript here

Lowest modder learning curve (Lua *is* the modding lingua franca), strong C++ binding
ergonomics (sol2 for PUC-Lua, LuaBridge3 for Luau — both map cleanly onto our `MapObject` /
`Hex` model), MIT throughout, and — with Luau — **turnkey sandboxing built for untrusted
code**. JS engines add a heavier runtime for no gain here; AngelScript has a thin modder
ecosystem.

### Optional, compile-flag-gated (`GECK_ENABLE_SCRIPTING`)

Tier 2 is an **opt-in** feature so users who don't script never download or build Luau. The
split that keeps this low-complexity:

- **`MapScriptApi`** — the C++ façade over `ObjectCommandController` — is **Luau-free** and
  always compiled. Tier 1 and headless unit tests use it without any scripting runtime.
- **The Luau runtime + binding + sandbox + script UI** live behind `GECK_ENABLE_SCRIPTING`
  (one CMake `option()` + one `if()` block), which also gates the `FetchContent(luau,
  LuaBridge3)`. A default build never sees Luau.

**Defaults:** `GECK_ENABLE_SCRIPTING` is **OFF** for end-user builds, but **every CI build
enables it** (`-DGECK_ENABLE_SCRIPTING=ON` on the Linux, macOS and Windows jobs in
`.github/workflows/ci.yml`) so the scripting path and the from-source Luau build are tested
on all platforms and kept from rotting.

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

1. ✅ **Host API skeleton** (`MapScriptApi`) over `ObjectCommandController`, one undo step via
   `ScopedUndoBatch`. Done; unit-tested headless.
2. ✅ **Tier 2 Luau** (not sol2 — see §2 decision): FetchContent Luau + LuaBridge3, sandboxed
   state, the same host API bound, a Script Console, and a headless `gecko-cli`. Done.
3. ➡️ **Curated generators + the MCP as the intelligence layer** — §11. Blind statistical
   generation was tried and abandoned (it scatters structural objects). Curated palettes + clumped
   `noise2d` placement and the unified error model are in; the open items are the **seamless
   multi-tile floor** (`autotile_floor`, P2 §4) and the **MCP server** with high-level tools
   (P4 §12) that lets an agent curate and place with judgment.

## 9. Open questions

- Multi-elevation prefabs (store/stamp across the 3 `ELEVATION_COUNT` slots?).
- Collision policy on stamp (overwrite vs skip vs error when target hexes are occupied).
- Scripts in patterns (object scripts via `programIndex`; spatial scripts) — deferred; see
  the script-model notes (programIndex is portable, SID/OID re-allocated at stamp).

## 10. What shipped (Tier-2 scripting + headless generation)

The scripting core and a first procedural generator are in. Concretely:

- **Luau runtime + sandbox** behind `GECK_ENABLE_SCRIPTING` (OFF by default, ON in every CI
  job). `print()` is captured for the console. (`src/scripting/LuaScriptRuntime`.)
- **`MapScriptApi`** — the Lua-free host façade, always compiled. Current surface:
  `isValidHex`, `hexNeighbors`, `getFloor`/`getRoof`, **`tileId(name)`** (FRM name →
  `tiles.lst` index), `paintFloor`/`paintRoof`, `placeObject(proPid, frmPid, hex, dir)`,
  **`placeProto(proPid, hex, dir)`** (resolves the art FID from the proto header),
  **`noise2d(x, y)`** (coherent value noise for non-uniform placement) and **`protoName(pid)`**
  (engine display name). The whole run is one undo entry via `ScopedUndoBatch`.
  - **Unified error model:** genuine failures (no data, unreadable/typo'd map path, unloadable
    proto) **raise** — surfaced as the run's error, `pcall`-able by a script — rather than returning
    a silently-empty result. "Not applicable" stays a value (`placeProto` → `false`, `tileId` →
    `-1` for an unknown name, `listMaps` → `{}`).
- **Script Console** dock (`View → Script Console`), wired to the current map/elevation.
- **`gecko-cli`** (Qt-free): `map analyze` (per-map + aggregate ground-tile and object usage,
  with raw engine PIDs and proto names from the `.msg` files; `--json` for a machine-readable
  report carrying a `flat` structural-vs-decoration flag per object) and `map generate --script
  <file> --out <map> [--arg key=value …]` (runs a Luau script against an empty map and writes
  a `.map`; `--arg`s are exposed to the script as the global `args` table).
- **`gecko-mcp`** (Qt-free, `GECK_BUILD_MCP`, default on): a Model Context Protocol server over
  stdio (newline-delimited JSON-RPC 2.0) that reuses the `gecko-cli` logic, so an AI agent can drive
  the inspect→curate→generate loop conversationally. Tools: `list_maps`, `analyze` (the `--json`
  report), `proto_info` (PID → type/name/`flat`), `generate`, and `render_map`. The dispatch
  (`McpServer`) is pure and unit-tested without any transport. This is §11's "MCP as the intelligence
  layer" landing.
- **Headless map render** (`gecko-cli map render`, MCP `render_map`). `MapRenderer` (Qt-free, in
  `gecko_editing`) draws a map through the *same* `RenderingEngine` the editor uses — into an
  off-screen `sf::RenderTexture`, framed to the content bounds — and saves a PNG, so an agent can
  *see* a generated biome, not just read its stats. `RenderingEngine`/`HexRenderer` moved out of
  `gecko_app` into `gecko_editing` for this (they were always Qt-free). Needs an off-screen GL
  context at runtime; reports an error instead of crashing when none is available.
  - **Objects style** (`--objects` / `objects:true`) mutes the floor to grey so the category-coloured
    object markers pop — for verifying scatter/clumping without the schematic's per-id floor rainbow.
  - **Schematic style** (`--schematic` / `schematic:true`) flat-colours each floor tile by its id
    and marks objects by category, returning a colour legend (id/type → colour → count). This
    *grounds* the analyze JSON to the image — the colours are the ids, colour regions are tiles,
    and borders between colours are the `adjacency` transitions — so a multimodal agent can match
    what it sees to the data instead of guessing which pixels are which tile.
- **Tile-adjacency analysis.** `analyze --json` now carries `adjacency` per map and aggregated: the
  floor-tile *borders* (which tile sits next to which different tile, and how often). Since the
  Fallout engine has no autotiling — mappers place edge tiles by hand — this is the empirical data
  an agent curates a transition set from before generating **seamless** terrain (the §11 P2 item).
- **Palette tool** (`analyze --palette` / MCP `palette`) + `number` in `analyze`. `palette` returns just
  the weighted generation input — `{ floor:[{id,name,weight}], scenery:[{pid,number,name,weight}] }`
  aggregated across the given maps — so an agent gets the exact script input in one small call instead
  of `jq`-ing the ~500 KB `analyze` report. `analyze` objects also now carry `number` (the PID's low 24
  bits, what `api:proto` wants — one less than the `00000NNN.pro` filename), fixing an off-by-one trap
  when generating scripts mechanically. (From an agent's MCP-usage retrospective.)
- **Scripting-API reference** (MCP `script_api`). Returns the generation-script `api:` surface as
  Markdown — every function + signature, plus the two non-obvious behaviours (runs are auto-seeded and
  auto-batched) and the error model — generated from an in-code table (`scriptApiEntries`) so it can't
  drift from a hand-written doc. A `[scripting]` test asserts every documented function is actually
  bound. (Best practice: the scripting surface is reference *material*, so it's emitted from the
  single source rather than maintained separately; the MCP *tool* surface is already self-documented
  by `tools/list`.) Also an **objects** render style and the path-contract docs, from the same agent
  retrospective.
- **Object clustering** in `analyze --json`. Each map carries a `clusters[]` array: nearby objects
  grouped by proximity (single-linkage, Chebyshev ≤ 3 hexes), each with a centroid `centerHex`, a
  bounding box and member PIDs. So an agent reading desert5 sees the perimeter blockers as one
  cluster to ignore and each tent as a `Wall`+furniture cluster to grab — it picks a tent's
  `centerHex` + a radius (from the bbox) and feeds them to `extract_pattern`.
- **Pattern-stamp extraction** (`gecko-cli`/MCP `extract_pattern`). Capture a structure from a real
  map into the editor's prefab/stamp JSON: locate it by its proto PIDs (option A — the agent reads
  them from `analyze`), grow their bounding box by a `radius` so immediate props come along, and
  capture the objects (and, with `includeFloor`, the floor/roof) verbatim. The Qt-free pattern core
  (`Pattern`, `PatternBuilder`, `PatternStamper`) moved into `gecko_editing`; the headless JSON
  writer (`cli::serializePattern`) matches the editor's Qt `PatternSerializer` exactly — proven by a
  round-trip test — so extracted stamps load in the editor's pattern library and feed `generate`.
  This is how an agent builds a library of tents/buildings from the reference maps.
- **Stamp placement in generation** (`api:placeStamp(name, anchorHex, variant)`). `generate` takes
  `--stamp name=file.json` (MCP: a `stamps` map), loads each via a Qt-free `cli::loadPattern`
  (nlohmann, round-trip with the writer) and registers it on the `MapScriptApi`; the script places it
  through the now-shared `PatternStamper`. Verified end to end: a tent extracted from desert5 placed
  into a fresh map renders as the intact tent. So the desert-map loop is closed — an agent can
  `analyze` → cluster → `extract_pattern` the tents and `generate` a new desert map that places them.
- **Reference-map analysis tools.** `MapScriptApi` exposes `mapScenery(mapPath)` (the unique
  scenery PIDs a reference map uses — blockers filtered out via `OBJECT_FLAT`),
  `mapSceneryHistogram(mapPath)` (`{pid → count}`), `mapFloorTiles(mapPath)` (floor-tile ids,
  most-used first) and `listMaps()` (every map in the data), all keyed by **PID** (the unique proto
  id). These were the basis of the abandoned blind generator; they live on as *analysis* tools an
  MCP agent reads to understand a reference before curating — see §11.
- **`gecko_editing`** — a Qt-free library (command controller, sprite/object builders, the
  script API and the Luau runtime) on top of `gecko_core`, linked by both `gecko_app` and
  `gecko-cli`, so the editor and headless tools share one editing implementation.
- **Headless object placement.** `ObjectCommandController::registerObjectData` + the
  `MapScriptApi(..., buildSprites=false)` mode record a `MapObject` as map data without
  building a sprite — so the CLI generates **terrain *and* scenery** with no GL context. The
  GUI keeps building sprites (default `buildSprites=true`).
- **Worked example:** `scripts/editor/terrain.luau` — a **curated** desert generator: a hand-picked
  vegetation palette over wasteland sand, scattered in natural clumps via the `noise2d` density
  field. `scripts/README.md` documents the `api` surface and error model; `scripts/` ships under
  `resources/scripts` (CMake copies it recursively on build + install).

The remaining weakness is the **floor**: it's still a single uniform tile (seamless multi-tile
terrain — `autotile_floor` — is the headline open item, **§11 P2 §4**). The scatter is now curated
and clumped, not uniform-random. Blind statistical generation was tried and abandoned (it scatters
structural objects); the path forward is curated palettes + an MCP agent's judgment — see §11.

## 11. Improvement backlog (procedural generation & scripting)

Ordered by value; the lower tiers build on the upper ones. None requires Qt, and only the
last item needs a GL context.

### Procedural generation — direction (revised after the blind-generation dead end)

**Blind statistical generation is a dead end for coherent maps.** The first desert generator looked
right because it was *curated* (a hand-picked list of small vegetation PIDs over sand). Generalising
to "scatter whatever a reference map uses" — even **weighted by real frequency** (`mapSceneryHistogram`)
— still drops structural objects (a vault door, a car, rock-formation pieces) at random hexes,
because the gap is **semantic**: *which* objects are scatter-able and *where* they belong is authorial
intent statistics can't reverse-engineer. Frequency only lowers a structural object's count, never to
zero, and "place at a random hex" has no notion that a vault door is an entrance. So clustering/WFC
would only fix *the arrangement of the right objects*, never *wrong objects appearing at all*.

The direction instead:

1. **Curated palettes are the reliable scatter primitive.** Curation *is* the semantic knowledge
   ("these are decorations"), encoded cheaply. Per-biome hand-picked lists, like the shipped
   `scripts/editor/terrain.luau`.
2. **An AI agent over MCP is the intelligence layer.** Coherent authoring needs judgment — which
   objects, density, clearings, where the structural pieces go — which an LLM agent supplies and a
   blind algorithm can't. The MCP exposes the map model + analyze/generate so the agent curates and
   places with world knowledge, and iterates against feedback.
3. **The MCP must expose high-level tools, not just `place_object`.** The agent shouldn't place
   10 000 floor tiles or 250 bushes one call at a time. Tools: `autotile_floor(from_reference)` /
   `paint_region` (floor seamlessness still needs a real algorithm — WFC / patch-sampling — as a
   tool the agent *drives*), `scatter(palette, area, density, clustering)`, `place_feature(pid, hex)`,
   plus analyze/inspect (`mapSceneryHistogram`, `protoName`, …) so the agent *understands* a
   reference before curating.

**Shipped.**
- Curated, **clumped** generator (`scripts/editor/terrain.luau`): hand-picked desert palette over
  sand, scattered via a `noise2d` density field (natural patches/clearings, not an even sprinkle).
- **`api:noise2d(x, y)`** — coherent value noise in `[0,1]`, the non-uniform-placement primitive
  (**P2 §6** below); reusable by the MCP.
- **`api:protoName(pid)`** — engine display name, so a caller (or the agent) can tell a decoration
  from a structural feature.
- **`api:mapScenery` / `mapSceneryHistogram` / `mapFloorTiles`** are kept as *analysis* tools (read
  to understand a reference), not as a blind generator.
- **Unified error model:** genuine failures (no data, unreadable/typo'd map path, unloadable proto)
  **raise** — surfaced as a Lua error the runtime reports, `pcall`-able by a script — instead of a
  silently-empty result. "Not applicable" stays a value (`placeProto` → `false`, `tileId` → `-1` for
  an unknown name, `listMaps` → `{}`).

**Still open.**
- **Seamless multi-tile floor** — the headline visual gap. A C++ terrain synthesiser
  (image-quilting / patch-sampling from a reference grid first, WFC with learned adjacency later)
  exposed as `autotile_floor` — the principled form of the autotiling item (**P2 §4**). Naive
  per-cell weighted-random is *not* this; arbitrary FO2 tiles don't blend.
- **The MCP server** (the intelligence layer) with the high-level tool surface above — see the MCP
  section and **P4 §12**.
- Placement polish for the curated scripts/tools: footprint-aware, iso-diamond-masked placement;
  recurring multi-object clusters extracted as **prefabs** (place a rock formation as one unit).

### P1 — Ergonomics: make scripts human-writable — ✅ done

1. ✅ **Palette by PID from a reference map** (not by name). The first attempt resolved a readable
   proto name → PIDs (`findProtos`), but display names are **not unique** (a `.msg` has several
   "Scrub" entries) and match game-wide, so the palette was ambiguous and uncurated. Replaced with
   **`api:mapScenery(mapPath)`** — the exact, unique scenery PIDs a real map is built from (a PID is
   the engine's unique id; names are not). Flat marker scenery (`OBJECT_FLAT`, e.g. `block.frm`) is
   excluded so a generator never scatters an invisible blocker. Companion: `mapFloorTiles`,
   `listMaps`.

2. **Human coordinates.** *(still open.)* Addressing by linear index (`hex = row*200 + col`, tiles `row*100 + col`)
   is unintuitive, and the two grids differ (200×200 hexes vs 100×100 tiles). Add `(col, row)`
   variants of the common ops (`paintFloorXY`, `placeProtoXY`, `getFloorXY`) plus index↔(col,row)
   converters (`hexIndex(col,row)`/`tileIndex(col,row)` + inverses) and a **tile↔hex bridge** (a
   tile covers ~2×2 hexes) so "paint this tile and put a tree on it" is one step. Reuse the engine
   geometry (`hexgrid::offsetToCube`/`columnOf`/`rowOf`). Optionally add normalized `[0,1]`
   helpers (`hexAt(fx, fy)`) so "centre"/"scatter across the map" are grid-size-agnostic. Decide
   and **document the orientation** so `(col,row)` matches what the editor displays (Fallout's hex
   numbering has a right-to-left quirk).

3. **Script parameters & seed.** `gecko-cli map generate --arg density=300 --arg seed=42` (and a
   console params field) exposed as a global table, so one script produces reproducible variants
   without editing it.

### P2 — Generation quality

4. **Analyze → generate model (autotiling).** `edg*` is a hand-authored *blend set* (~49 variants
   per desert map for edges/corners), not one flat texture. Extend `map analyze` to record tile
   **adjacency** (which tiles border which), and have the generator pick the right edge/corner
   variant at biome boundaries (Wang/blob tiling) instead of a uniform fill. Biggest visual jump,
   pure data, derived from the shipped maps — closes the analyze→generate loop (analyze currently
   *learns* the palette but the generator *hardcodes* it).
5. **Statistical scatter.** ⚠️ *Superseded — see "Procedural generation — direction" above.*
   Frequency-weighting (`mapSceneryHistogram`) was tried and still scattered structural objects
   (vault doors, cars), because choosing *what* is scatter-able is semantic, not statistical. The
   reliable path is **curated palettes** + an MCP agent's judgment; the histogram lives on as an
   *analysis* tool, not the generator.
6. ✅ **Noise-based distribution.** `api:noise2d(x, y)` (coherent value noise in `[0,1]`) is in;
   `scripts/editor/terrain.luau` uses it as a density field for natural clumping.
7. **Enclosures / autowalling + roofs.** A helper that rings a region with correctly-oriented
   wall protos (the analyze output is full of left/right/corner `Wall` variants) unlocks the cave
   and town biomes; generate a **roof** layer for enclosed areas (`paintRoof` already exists).

### P3 — Reach & tooling

8. **`--in <map>`** for `generate` — decorate/edit an existing map, not just an empty one (the GUI
   console already runs against the current map).
9. **Fill/region/query helpers** — `fillRect`, `fillRegion`, `tilesByPrefix("cav")`, region and
   neighbour queries — small, composable, make scripts read like intent.
10. **Biome script library** — `cave.luau`, `town.luau`, `coast.luau` beside the desert one; each
    a worked example. Expand the `scripts/README.md` table.
11. **Batch generation** — produce N maps with varying seeds in one `gecko-cli` invocation.

### P4 — AI & visual (ties into the MCP section below)

12. **MCP server** wrapping `gecko_cli`'s `analyze`/`generate` plus a `palette` tool, so an agent
    can inspect and generate maps conversationally. The `gecko_editing` extraction + the existing
    `gecko-cli` already de-risk this — see the MCP section.
13. **Headless render / PNG export.** The one genuine GL case (offscreen `sf::RenderTexture`),
    needed only to *preview* a generated map. Everything else above is GL-free. Shares the Qt-free
    render-library extraction noted under the MCP "visual analysis" item.

### Open questions

- **`findProtos` scope/cost:** scan all proto types into one cached index, or per-type
  (`findScenery`/`findWall`) to bound the first-call scan? Lean: one cached index, documented.
- **Coordinate convention:** expose `(col,row)` as the engine's storage layout or remap to
  match the editor's on-screen/displayed coordinates? Pick one and document it.
- **Collision policy** when a generator targets an occupied hex/tile (overwrite / skip / error).
- **Multi-elevation generation** (run a script across all three elevations in one invocation).

---

# Map semantics & intelligence (analysis MCP roadmap)

> Status: **Phases 1–2 done + the `describe_map` orchestrator (capability 1) shipped.** Phase 1:
> ai.txt reader; `analyze` reports per-map `critters` (team + ai.txt-resolved AI), a `header` digest
> (player spawn / enabled elevations / darkness / map script / named map variables) and an `exits`
> connectivity graph. Phase 2: `describe_script` (.ssl source + dialog) and `reachability`.
> `describe_map` now composes analyze + reachability into one cross-referenced digest (capability 1
> below). Remaining: the Phase-3 semantic render overlay (capability 5). Today the MCP *perceives
> geometry* — tiles, objects,
> clusters, palette, render, extract/generate. It cannot read the map's **semantic** layer: AI
> packet is a raw number (no `ai.txt` reader), scripts are referenced by `scripts.lst` index/name
> only (no `.ssl`/`.int` reading), `analyze` treats critters as generic objects, and there is no
> reachability/connectivity. The goal is to let an agent reason about a map's **purpose**, its
> **critters' AI**, and its **scripts**.

**Guiding principle.** Don't hardcode classification heuristics ("N critters ⇒ a fight"). Surface
the engine's own semantic sources faithfully and **cross-referenced**, and let the model infer
purpose from the evidence — the same data a designer reads. This is MCP best practice (small
composable tools + one orchestrator, structured join-able output) and the repo's engine-fidelity
rule (no invented label tables). Every result should carry the join keys (`pid`↔proto.msg,
`script_id`↔`scripts.lst`↔`.ssl`↔`.msg`, `ai_packet`↔`ai.txt`) and cite which file each fact came
from. Keep all new readers Qt-free (vault/cli) so the server stays headless.

**Capabilities (each notes the reader it needs):**

1. **`describe_map` — purpose synthesis (orchestrator). ✅ Done.** One structured digest per map:
   the `analyze` per-map report (header — enabled elevations / darkness / player start / map script /
   map vars; floor/biome; object `clusters` = structures; `critters` roster with ai.txt-resolved AI
   and each one's attached `{programIndex, name}`; `exits` graph) **plus** a `reachability` field
   (per-elevation walkable/reachable hexes + entry-orphaned objects). Composes `analyzeMaps` +
   `analyzeReachability` (`cli::describeMap`, MCP `describe_map`, CLI `map describe-map`); returns the
   engine's own evidence with join keys preserved (pid, script_id, ai_packet) — the model writes the
   conclusion, no baked-in heuristics. Verified: artemple clean (0 orphans), vctydwtn surfaces the 9
   orphaned servant/slave objects inline with the roster.
2. **Critters + AI — `critters` tool + new `ai.txt` reader.** Per critter: name (`pro_critters.msg`),
   hex, **team (`group_id`)**, **AI packet resolved via `ai.txt`** (aggression, disposition,
   `run_away_mode`, `area_attack_mode`, `best_weapon`, `distance`, `secondary_freq`), equipped
   weapon + inventory, attached script. `ai.txt` is INI (one section per packet; section header =
   name; `packet_num` = the critter's `ai_packet`). **Phase 1.**
3. **Scripts + behavior — `describe_script` (Phase 2, in progress).** An object's `map_scripts_pid`
   (SID) → the map's `MapScript` with that pid → its `script_id` = the program index → `scripts.lst`
   row → the **filename** (e.g. `ncProsti.int`). Resolution keys off that **filename basename**
   (extension stripped), matched **case-insensitively** — *not* `SCRIPT_REALNAME`, which is only a
   source-side debug string (`define.h`: `ndebug` prefixes log lines with it); the engine itself
   loads `dialog\<filename>.msg` from the scripts.lst name (`scripts.cc:2740`), so the filename is
   authoritative. From the basename:
   - **`.ssl` source from the VFS.** Shipped DATs hold only the compiled `.int`; the source comes
     from a community patch (e.g. BGforge's Fallout 2 Restoration Project, which ships ~1500 `.ssl`
     under `scripts_src/<area>/`). The user mounts that source tree as a data path; since it is
     subdir-organized, build a **basename→path index** once (`list("*.ssl")`) and look up by
     basename. `hasSource:false` (+ a hint to mount a source patch) when absent.
   - **`.msg` dialog** at `text/<locale>/dialog/<basename>.msg` via the existing `Msg` reader.
   - The `.int` procedure-hook reader is **optional/deferred** — only a fallback for the no-source
     case; with real `.ssl` we read the source directly. `int2ssl` decompilation stays out of scope.
   Plus a critter/object → `{programIndex, name}` bridge in `analyze`, so the agent reads the roster,
   spots a scripted NPC, and calls `describe_script` for the full who→does→says picture.
4. **Reachability / connectivity — `reachability` (Phase 2, done).** Per-elevation walkable
   flood-fill (reusing `object_query::blocksMovement` + the parity-correct `HexGeometry` neighbours)
   seeded from the entry points (player start + exit grids). **Doors count as passable** (the player
   opens them — `pro->objectSubtypeId() == SCENERY_TYPE::DOOR`), which is essential: otherwise every
   room behind a closed door looks sealed. Reports `walkableHexes`/`reachableHexes` and
   `orphanedObjects` — critters/items cut off from the entry-reachable area. Elevations with no entry
   (reached via stairs/elevators) report `reachableHexes: null` + a note. Optimistic on doors, so it
   under-reports rather than crying wolf. Verified: artemple/denbus1/newr1 clean (0 orphans),
   vctydwtn flags a real isolated servant/slave cluster.
5. **Semantic render overlay** (extends the schematic). **Object roles done:** a `Semantic` render
   style (`render_map semantic:true` / CLI `--semantic`) greys the floor and colours object markers
   by role — exit grids highlighted, critters by team (`group_id`), scripted objects (`map_scripts_pid`)
   ringed — with a role-keyed legend that joins back to `describe_map`. **Still open:** *shading the
   unreachable regions* `reachability`/`describe_map` identify, which needs a per-hex reachable mask
   exposed from `MapReachability` and a hex→world tint in the renderer (the object-marker path here is
   sprite-bounds-based and doesn't map arbitrary hexes). **Phase 3.**

**Corpus angle (multiplier):** index `analyze` + these semantic facts across all shipped maps so
the agent can query *examples* ("how do shipped towns place and wire shopkeepers?") — improving
generation, not just analysis.

**Phasing.** Phase 1 (small/high-value): `ai.txt` reader + critters-with-AI in `analyze`/a
`critters` tool + header/globals + exits-as-graph. Phase 2 (medium): reachability flood-fill +
`describe_script`. Phase 3 (synthesis): `describe_map` digest + semantic render overlay.

## Next capabilities (post-`describe_map`)

These extend the roadmap above and all follow one **data-fidelity + layering rule**: parse engine
data in the **vault** library (a `format/…` object + a `reader/…`, like `MapsTxt` and `AiTxt`),
never inline in the cli/MCP layer. The MCP composes the structured objects into JSON; it does no
file parsing of its own. (`maps.txt` was moved into vault as `MapsTxt` to set this precedent.)

6. **`engine_reference` — ground answers in the engine source.** A read-only MCP tool that searches
   a mounted `fallout2-ce` checkout (grep + bounded file read) and returns `file:line` + snippets.
   The map-script 1-based rule (`scriptIndex - 1`) and the `scrname.msg[programIndex + 101]` /
   `map.msg[map*3 + elev + 200]` index formulas were all answered by *reading the engine*; this tool
   lets a **shell-less** MCP agent do the same instead of guessing format details. Config: an
   engine-source path (like `--data`); cap results (top-N matches, small snippets) so it can't flood
   context. *Lower priority when the agent already has filesystem/grep tools — then just mounting the
   source is enough; this earns its keep specifically for headless/sandboxed agents.*

7. **Exit-grid connectivity graph — `map_graph`. ✅ Done.** Walks every map's exit grids into a
   directed map→map graph (`{destMap, destMapName, destHex, destElevation}` + a per-edge hex count),
   with stats flagging dead-ends and maps with no incoming edge. **Scope correction:** this is only
   the exit-grid layer — how a *location's* maps link (intramap elevation changes + intermap edges
   within a town) and where they hand off to the world map (`kind=worldmap`). It is **not** inter-city
   travel; cities are crossed on the world map, so the graph is connected only within a location.
   Follow-up: cross with `reachability` to flag one-way edges.

7b. **Worldmap layer — `world_map` (city.txt). ✅ Done.** The inter-city layer `map_graph` doesn't
   cover: a vault `CityTxt` reader (`data/city.txt`) → areas (name, `world_pos`, size, known-at-start,
   the maps each contains via its entrances) + the straight-line distance between every pair of areas.
   This is the actual "map of the world + distances between places." Terrain types + encounter group
   tables are now also covered (`worldmap.txt` → `world_encounters`). **Remaining:** the `worldmap.txt`
   `[Tile NN]` sub-tile grid (per-position terrain → terrain-weighted travel cost, geographic
   encounter placement) and `worldmap.msg` localized names.

8. **Corpus / world index — evidence, not a solver.** The evidence layer now largely exists: the
   `map_graph`↔`world_map` join (`area`/`mapFile`/`lookupName`), the `quests` tool (each quest's area +
   tracking gvar + thresholds + text) and the `gvars` dictionary (gvar index → `GVAR_*` name) — so an
   agent can already *reason* about progression ("which script sets the gvar that gates quest Y?": read
   the quest's gvar → name via `gvars`, then `describe_script` for the scripts that touch it).
   Deliberately still **not** a computed "critical path to the ending": `.ssl` is imperative quest
   logic and static extraction of a win-path would be brittle. The MCP supplies ground truth; the
   model infers the route. The `endings` tool (endgame.txt: gvar==value → ending slide) supplies the
   win-conditions, `world_map.start` marks the entry map (artemple.map / Arroyo), and `find_gvar` gives
   the causal link — a quest's gvar → the .ssl scripts that set it (the action that advances it) vs
   check it — so the start→objectives→ending loop is readable end to end (quest → gvar → find_gvar →
   describe_script).

### Data-extraction roadmap (engine data files → vault readers)

Surveyed from `fallout2-ce` (`configRead` / `messageListLoad`). Each becomes a vault reader + object,
then surfaces through `analyze`/`describe_map` or a dedicated tool. Priority order:

- **`map.msg` display names** *(high value, low effort — the obvious next step).* `mapGetName` =
  `map.msg[mapIndex*3 + elevation + 200]`; city names = `map.msg[1500 + city]`. Gives each map (and
  every `destMap` exit) a friendly per-elevation name ("Arroyo", "Temple of Trials") alongside the
  `.map` filename `MapsTxt` already resolves. Pairs directly with `MapsTxt`.
- **`data/quests.txt` + `game/quests.msg`** ✅ **done** — `QuestsTxt` vault reader + the `quests` tool:
  each quest's area (map.msg location name), tracking **gvar** (index + `GVAR_*` name + default via
  vault13.gam), display/completed thresholds, and quests.msg description. The progression spine.
- **`data/vault13.gam` global variables** ✅ **done** — the `gvars` tool (gvar index → `GVAR_*` name +
  default), reusing the `Gam` reader (fixed to read negative-default gvars, which were shifting
  ordinals). The dictionary that makes quests and scripts legible.
- **map_graph ↔ world_map join** ✅ **done** — `MapsTxt::findByLookupName` + `MapNameResolver`; world_map
  entrances carry `mapFile` and map_graph nodes carry `area` + `lookupName`, so the world layer and the
  exit-grid layer cross-reference in both directions.
- **`data/city.txt`** ✅ **done** — `CityTxt` vault reader + the `world_map` tool (areas, world
  positions, sizes, the maps each area contains, pairwise distances). The inter-city layer.
- **`data/worldmap.txt`** ✅ **done** — `WorldmapTxt` vault reader + the `world_encounters` tool
  (`[Data]` terrain types — `difficulty`, not "weight" — and `[Encounter: NAME]` group tables) **and**
  the `[Tile NN]` sub-tile grid: `WorldmapTxt::terrainAt(x,y)` mirrors the engine's
  `wmFindCurSubTileFromPos`, so `world_map` now reports each area's `terrain` and a terrain-weighted
  `travelCost` between areas. **Follow-up (minor):** per-position *encounter* placement (the subtile
  encounter chances) is still unparsed — only the terrain field is kept.
- **`game/worldmap.msg`** *(follow-up, small)* — localized area/terrain/encounter names to enrich
  `world_map` / `world_encounters` (city.txt `area_name` and the encounter section names are the
  internal labels).
- **`data/endgame.txt`** ✅ **done** — `EndgameTxt` vault reader + the `endings` tool: each ending slide
  keyed by `gvar == value` (gvar resolved to its `GVAR_ENDGAME_MOVIE_*` name + a readable condition),
  the slide art, and the narrator/subtitle base name. Slides sharing a gvar at different values are a
  location's branching outcomes (e.g. Gecko has 5). Closes the start→objectives→ending loop with quests
  + gvars. **Follow-ups:** `enddeath.txt` death endings (in master.dat) and the narration subtitle text
  (`text/<lang>/cuts/<narrator>.txt`).
- **`data/party.txt`** (companions), **`holodisk.txt`**, **`karmavar.txt`** — lore/state, lower
  priority.

---

# Selection type toolbar — combinable layers & non-destructive switching

> Status: idea / scoping. Two related improvements to how the selection *type* works. Today the
> type is a single `SelectionMode` (`src/util/Types.h`: `ALL`, `FLOOR_TILES`, `ROOF_TILES`,
> `ROOF_TILES_ALL`, `OBJECTS`, `HEXES`, `SCROLL_BLOCKER_RECTANGLE`) that the toolbar cycles through
> (`EditorWidget::cycleSelectionMode`/`setSelectionMode`).

## 1. Let the user combine layers (checkboxes instead of one hardcoded type)

**Goal:** instead of one mode at a time, let the user pick a *combination* of layers to select —
e.g. roof + floor tiles, or floor + objects — via a set of checkboxes on the toolbar.

**Difficulty: moderate, and lower than it first looks**, because the selection code is already
decomposed per category — the single-mode `switch`es just fan out to per-category helpers that a
combined model would call directly:

- `SelectionManager::collectItemsInArea` already delegates to `appendObjectsInArea` /
  `appendTilesInArea(roof=false)` / `appendTilesInArea(roof=true)` / `appendHexesInArea`, and the
  `ALL` case already calls several of them (objects + floor + roof-if-visible). A combined selection
  is "call the appenders for each *enabled* category" — `ALL` is just "all enabled".
- `collectDeselectableAtPosition` (Ctrl+click) and `selectAtPosition` are similarly per-category
  (`appendRoofCandidate` / `appendObjectCandidates` / `appendTileCandidate` / `appendHexCandidate`).
- `RenderData.currentSelectionMode` only drives the selection-rectangle colour
  (`applySelectionRectangleColors`) — cosmetic.

**Work involved:**
- **Model:** replace the single `SelectionMode` with a set/bitmask of selectable categories
  (Floor, Roof, Objects — the layer-like ones). Keep `HEXES` and `SCROLL_BLOCKER_RECTANGLE` as
  distinct *tools* (they aren't "layers" and shouldn't combine), so this is really "generalise the
  ALL/tile/object modes into a category set" rather than touching every mode.
- **Dispatch:** rewrite the three `switch (mode)` sites in `SelectionManager` to iterate the enabled
  categories and call the existing appenders. Mechanical, since the appenders already exist.
- **UI:** swap the cycle-button for a few checkboxes (Floor / Roof / Objects), wired to the category
  set. The `selectionModeToString` / cycling logic and the `NUM_SELECTION_TYPES` enum sentinel go
  away or shrink.
- **Edge cases:** `ROOF_TILES_ALL` (include-empty) is a roof variant — fold it into a roof option
  (e.g. a modifier) rather than a separate category; decide how the rect colour reads for a mix.

Risk is low (no new hit-testing; reuses tested per-category helpers and the `SelectionDataProvider`
seam covered by `MockEditorWidget`). The bulk is the model swap + the toolbar widget.

## 2. Don't clear the selection when the type changes (additive / subtractive)

**Current behaviour:** `EditorWidget::cycleSelectionMode` and `setSelectionMode` both call
`_selectionManager->clearSelection()`, so changing the type throws away the current selection.

**Goal:** changing the type keeps the existing selection; subsequent clicks/drags then **add to** or
**subtract from** it under the new type (e.g. select floor tiles, switch to roof, add roof tiles to
the same selection; Ctrl-drag to remove). This is the natural companion to #1 — with checkboxes,
"changing the type" becomes "toggling a category", and persistence is expected.

**Work involved (small):**
- Drop the `clearSelection()` calls from the two mode setters.
- The add/subtract paths are already mode-aware and additive: `addArea`/`addToSelection` (don't
  clear), `deselectArea`/`deselectAtPosition` (remove, visibility-respecting), and the selection
  state already holds **mixed** item types (that's what `ALL` produces), so a persisted floor+roof
  selection is already a valid state.
- Verify the move/region code and the selection visuals handle a mixed selection built up across
  type switches (they should, since `ALL` already yields mixed selections), and that `_state.mode`
  isn't relied on anywhere as "the one true type" in a way that a persisted mixed selection breaks.

---

# Map loader panel — remaining enhancements

The visual map picker shipped (`MapBrowserDialog`, File → Browse Maps…: thumbnail grid + filter
+ preview, lazy render, in-memory cache). Possible follow-ups:
- **Source grouping** — separate vanilla `master.dat` maps from user/filesystem maps.
- **Welcome-screen entry point** — reach the browser from the no-map welcome screen.
- **Persisted cross-session thumbnail cache** (keyed by map path + mtime) — only if cold-start
  render latency proves annoying; the current cache is in-memory `QPixmapCache`.

---

# Visualize spatial scripts on the map (investigate)

> Status: investigation. Spatial scripts can be created (`SpatialScriptDialog` → F-key flow)
> but are **invisible on the map** once placed — there's no marker, no radius overlay, and no
> way to see, select, edit, or delete an existing one (see Known limitations #5). Goal: render
> existing spatial scripts on the canvas so designers can see where their trigger zones are.

## What the data model gives us
A spatial script is a `MapScript` (not a saved object) with `pid == 1`, its position packed
into `timer` as a `built_tile` (`built_tile::create(tile, elevation)`), and `spatial_radius`
(`src/format/map/MapScript.h:11-14`; created via `MapScript::createSpatial(...)`, read at
`MapReader.cpp:254`). They live in the map's script section and are NO_SAVE editor markers in
the engine (no `MapObject`), so visualization must be driven from the script list, not the
object list. Placement currently flows MapInfoPanel → `EditorWidget::addSpatialScript` →
`ObjectCommandController` (undoable); the editor already owns the data.

## Questions to answer
- **Reference behaviour:** how does the fallout2-ce mapper draw spatial-script markers + their
  radius? (Check `mapper.cc`/`map.cc` for the spatial-script overlay; mirror its marker art and
  radius shape rather than inventing one — engine-fidelity rule.)
- **Coordinate path:** `built_tile` → tile/elevation → hex → screen. Confirm the `built_tile`
  position is a tile index vs hex, and reuse `ViewportController`/`HexagonGrid` conversions; only
  draw scripts on the current elevation.
- **Radius overlay shape:** the engine radius is in hexes — is the trigger zone a hex-ring/filled
  hex area (engine uses hex distance) or a screen circle? Render whatever matches the engine's
  trigger test, reusing the hex-grid overlay machinery.
- **Render layer:** add a dedicated overlay layer in `RenderingEngine` (like the exit-grid /
  blocker / light overlays), gated by a View-menu visibility toggle + `VisibilitySettings` flag,
  culled via the viewport like the other overlays.
- **Interaction (stretch):** hit-test a marker to select → open `SpatialScriptDialog` to
  edit/delete; ties into the F-key click-to-place + new `EditorMode` already sketched in
  Known limitations #5 (live hex marker + radius preview while placing).

## Rough effort
S–M for read-only visualization (marker + radius overlay + visibility toggle), reusing the
existing overlay-layer and hex-grid plumbing. Editing/deleting via map interaction is the M
part and overlaps the spatial-placement `EditorMode` follow-up.

---

# Feature-gap audit vs the reference mappers (investigate)

> Status: investigation. Catalog the editing features the two reference mappers have that
> Gecko lacks, then turn the gap into a prioritized backlog. One concrete, confirmed example
> is **EDG (map-edge) support**: fallout2-ce stores per-map `.edg` files (`build/data/MAPS/*.edg`)
> and the engine mapper authors them (`src/map_edge.{cc,h}`, `src/mapper/map_edge_setup.{cc,h}`,
> gated by the `edg_support` setting) — Gecko has no `.edg` read/write or edge-setup UI. Find
> the rest of the gaps the same way, systematically rather than ad hoc.

## The two references
- **Legacy Dims Mapper** — `reference/F2_Mapper_Dims-master/` (the community F2 mapper Gecko is
  modelled on). TODO.md already has a partial "Legacy F2_Mapper_Dims Missing Features" list
  (minimap, brush system, batch property editing, script-assignment UI, template system,
  advanced search/filter, richer progress dialogs) — fold that into this audit rather than
  keeping a second list.
- **fallout2-ce built-in mapper** — `/Users/jansimek/Development/fallout2-ce/src/mapper/`
  (`mapper.cc`, `map_func.cc`, `mp_instance.cc`, `mp_proto.cc`, `mp_scrpt.cc`, `mp_targt.cc`,
  `mp_text.cc`). This is the engine's own authoring tool and the source of truth for behaviour
  — walk its tool modes / menu actions and check each against Gecko.

## What to produce
A single prioritized parity list: feature → which reference has it → does Gecko have it →
adopt / defer / intentional non-goal (per the engine-fidelity rule — match the reference's
behaviour, don't invent). Candidate areas to check beyond EDG/map-edge and the Dims list:
edge-scroll panning (`map_func.cc`), proto editing (`mp_proto`), script & target tooling
(`mp_scrpt` / `mp_targt`), per-hex block/roof/light toggles, and exit-grid authoring (already
partly covered — see the exit-grid limitation above).

## Rough effort
S to produce the catalogue (read-only audit of two known codebases); the individual features
it surfaces are then sized and sequenced separately.

---

# Clean and verify TODO.md (housekeeping)

> Status: to do. `TODO.md` has drifted and needs the same "track what's left, not what's done"
> pass this plan just had.

## What needs doing
- **Remove completed items.** Several Code Quality / Architecture entries are done now that the
  architecture roadmap landed — e.g. splitting the resource singleton (`Settings` is injected;
  resource access is the `GameResources`/`DataFileSystem` facade), the MAP read/write refactor
  + round-trip coverage, and the `ProEditorDialog` decomposition. Verify each against the
  current code and delete the ones that shipped.
- **De-duplicate.** "placing lights - light.frm" is listed twice; collapse duplicates.
- **Verify the rest still applies** — some bugs/usability items may already be fixed (e.g. the
  scroll-blocker isometric-rectangle bug is also tracked here in the exit-grid section).
- **Reconcile with this plan.** Fold the "Legacy F2_Mapper_Dims Missing Features" section into
  the feature-gap audit above so there is one backlog, not two.

## Rough effort
S. Pure housekeeping — read TODO.md against current code, delete done/dupe items, cross-link
the rest.

---

# In-game preview mode (future idea)

> Status: idea / scoping. A toggle that makes the editor viewport behave more like the
> running game — idle animations play, ambient sound plays, lighting/darkness renders, and
> the editor chrome dims — so a designer can sanity-check "does this scene feel right?"
> without launching Fallout 2.

## What it would involve, by piece (rough effort)

- **Idle animations — Medium.** We already decode FRM frames (the PRO dialog previews them)
  and `Object::setDirection` sets a frame's texture rect; `TextureManager` stitches FRM frames
  into sheets. The core work is a preview clock that advances each animated object's frame
  index over time (honouring the FRM `fps` / `framesPerDirection`, looping idle anims), plus
  per-object animation state and only animating culled/on-screen objects for perf at map scale.
  **Depends on** fixing the per-frame offset handling (known limitation #10) or animations will
  wobble. No new assets needed.
- **Lighting / darkness — Medium.** Render honouring `header.darkness` and per-object light
  (`light_radius` / `light_intensity`, already in the model) — an additive light pass / ambient
  tint in `RenderingEngine`. The data already exists; it's a rendering feature.
- **Ambient sound — Large.** SFML audio is currently **disabled** (`SFML_BUILD_AUDIO=FALSE`,
  `cmake/dependencies.cmake`), so step one is enabling it. F2 sounds are **ACM** files (a custom
  ADPCM-style codec) needing a decoder, and ambient/background audio isn't stored in the `.map`
  (it's script/worldmap-driven), so "what plays here" has to come from the map script or a
  convention. Biggest, most independent lift.
- **"Game-like" chrome — Small.** A mode toggle that hides grid/overlays/selection, dims the
  panels, and centres on the player start. Cheap polish once the above exist.

## Recommendation / sequencing

Value is front-loaded, cost is back-loaded — so tier it:
1. **Idle-animation preview** (Medium) — highest value, reuses existing FRM decode + render;
   gated on fixing the frame-offset bug. Ship as a "Play animations" toggle first.
2. **Lighting / darkness** (Medium) — independent, data already present.
3. **Ambient sound** (Large) — only if worth enabling SFML audio + writing an ACM decoder; the
   long pole and least essential for an editor.

Bottom line: an "idle animations + lighting" preview is a **Medium** effort on top of what
exists; full parity with the running game (sound, day/night, critter wander/AI) is **Large**
and probably not worth chasing for a map editor.

---

# MCP server for AI-assisted map analysis & editing (future)

> Status: idea / scoping — **now substantially de-risked.** Expose the editor's map model as an
> MCP (Model Context Protocol) server so an AI assistant can analyze a map, describe it, add/move
> objects, change scripts, and (eventually) understand it visually and via its scripts/NPC dialogs.
>
> **Foundations already in place** (see the scripting §10): the Qt-free **`gecko_editing`** library
> (controller + script API + Luau runtime) and a headless **`gecko-cli`** with `map analyze` and
> `map generate`. The "build a headless CLI over the libs first, then wrap it in MCP" plan below is
> now half-done — the MCP server is largely a JSON-RPC shim over `gecko_cli`'s existing entry points
> plus the read/describe tools.

## MCP server hardening — done, and one deferred follow-up

**Done** (a code-review pass): a **table-driven tool registry** (one `ToolSpec` list — name,
description, schema, handler — that both `tools/call` dispatch and `tools/list` derive from, so they
can't drift); **argument validation** (typed `requireString`/`requireInt(min,max)`/bounded
`optInt`/strict `optBool`, surfaced as `isError` — no more negative-pid-wraps-to-huge-uint or
negative-`maxDimension`); and **protocol edge cases** (a `ping` handler, no-id request methods no
longer execute as notifications, `jsonrpc:"2.0"` validated → `-32600`).

**Deferred — richer tool output/metadata (MCP 2025-06).** Worth doing some day, not now:
- **`structuredContent`** on the JSON-emitting tools (analyze/describe_map/palette/proto_info/…) —
  return the parsed object alongside the text block, so clients get typed data instead of re-parsing
  a string.
- **Tool annotations** — `readOnlyHint` (everything except generate/render/extract is read-only),
  `destructiveHint`, `openWorldHint:false` (all data is local). Cheap to add to each `ToolSpec` now
  that the registry carries per-tool metadata.
- **`render_map` as an image/resource** — return an embedded image or a resource link rather than the
  written path (more idiomatic; the path works fine for a local agent).
- *(Not planned: per-call cancellation / progress notifications — the stdio loop is deliberately
  synchronous and tool calls are short, so the threading cost isn't justified.)*

## Why it's cheap here

The four-library split already makes the model, formats, and resources **Qt-free and
headless-linkable** (`vault` → `gecko_resource` → `gecko_core`; the test suite links them with
no GUI). So an MCP server **reuses `MapReader`/`MapWriter`, `MapObject`/`MapScript`
(+ `cloneDeep`, the `makeObjectScript`/`makeSpatialScript` factories), and PID→name resolution
via the resource layer** — zero format re-implementation, guaranteed fidelity, same validation
rules (hex 0–39999, 3-elevation framing, exit-grid PIDs 16–23).

## Tool surface (tiers)

- **Read / describe — Small–Medium.** `describe_map` (header, enabled elevations, object/script
  counts), `list_objects(elevation)` with resolved names, `list_scripts`, `get_hex(pos)`,
  `find_objects(pid|type)`, exit-grid/transition connectivity. This is the bulk of "analyze the
  map completely" and is the easy half.
- **Write — Medium.** `add_object(pid, hex)`, `remove_object`, `edit_object_fields`,
  `attach_script`/`detach_script`, `place_spatial_script`, `paint_floor/roof`,
  `clear/copy_elevation`, `save`. Mirrors logic now centralized in `ObjectCommandController` /
  `MapScript`; headless needs no undo, just model mutation + the existing writer.
- **Transport — Small.** MCP is JSON-RPC over stdio (`initialize`, `tools/list`, `tools/call`).
  Lowest-risk: build a headless **`gecko-cli`** (JSON in/out) over the existing libs first
  (independently testable, reuses the round-trip tests), then wrap it with an MCP server in any
  language. Alternatively a C++ MCP server linking the libs directly.

## Deeper understanding (the longer-term goals)

- **Script & NPC-dialog analysis — Medium.** "Understand the scripts" means reading the **real
  `.ssl` source** the map's scripts compile from (we usually have it — see the deep-understanding
  section below), indexed by the **`.int` metadata reader** (procedure names, exported/imported
  procs, string table — see the SSL/INT notes) and paired with the **`.msg`** file of the same
  basename (we already have an `Msg` reader), which holds the NPC's dialogue/display lines. So
  `describe_script(index)` → SSL source + proc list + the linked `.msg` text gives an AI the actual
  behaviour and conversation tree without running the game. Cross-reference `scripts.lst`
  (index→name) and the map's `MapScript`/object `sid` to answer "what does the NPC on this hex
  say/do?".
- **Visual analysis — Small–Medium (a refactor, not a rewrite).** To let the AI *see* the map
  (rendered screenshot per elevation/region), reuse the **existing** SFML renderer:
  `RenderingEngine`/`MapSpriteLoader` already have **zero Qt includes** and `render()` already
  takes a generic `sf::RenderTarget&`. They just live in the Qt CMake target (`gecko_app`). The
  clean move is to **extract a Qt-free `gecko_render` library** (renderer + sprite loader +
  hex/viewport math; CMake-target move, not a code change) that both `gecko_app` (window target)
  and the headless MCP/CLI use. The CLI renders to an **`sf::RenderTexture`** (offscreen) and does
  `copyToImage()` → PNG — the same draw code the editor runs each frame, **no duplication**.
  *Caveat:* `sf::RenderTexture` needs an OpenGL context — automatic on a desktop, but a headless
  box (Docker/CI) needs `xvfb`/EGL. No Qt event loop is involved.

## Deep map understanding (the powerful-server goal)

The end state is a server that can answer *"what is on this map, what is each thing for, and how
does it all connect?"* — not just dump object rows. That means walking every layer of the data the
engine itself uses, all of which `vault`/`gecko_resource` can already read (or read with a small
addition). Each item below is a tool (or a field on `describe_map`) and the data path it stands on:

- **Per-object semantic dump (all types) — Small.** For every `MapObject`, resolve `pro_pid` →
  the `.pro` (already loaded for analyze), and emit the *type-specific* proto body, not just the
  display name: flags (`OBJECT_FLAT`, `NO_BLOCK`, shootable, light-emitting + radius/intensity),
  the `script_id`/`sid` link, frame/orientation, and elevation. PID encoding (`(type<<24)|id`) and
  the `.lst`/`.msg` lookups are already done in `map analyze`; this just stops collapsing them to a
  count. Output keyed by `[Item]/[Critter]/[Scenery]/[Wall]/[Misc]` so the AI sees the inventory of
  the world by role.
- **Critters & their purpose — Medium.** A critter's purpose lives in its **critter `.pro`** plus
  its **script**. From the proto: base **SPECIAL** stats, HP/AC/derived stats, **team** and
  **kill-type**, the **AI packet number**, body type, and the **inventory** (`MapObject` carries
  child objects — guns/ammo/armor the NPC spawns with). The AI packet number indexes
  **`data/ai.txt`** (aggression, morale, `run_away_mode`, preferred-weapon distance, chem use,
  area-attack flags) — a new but tiny INI-style reader gives "this critter is a cowardly melee
  raider who flees at 25% HP." Team + kill-type + AI packet together answer *"is this a friendly,
  a guard, or an ambush?"* without running the game.
- **Scripts, AI behaviour & dialogue — Medium.** The goal is the **real SSL source**, not a
  metadata summary. In practice we usually *have* the `.ssl` source for the compiled `.int` a map
  references (it ships alongside `scripts/`, or is fetched from the script source tree), so the
  server should **resolve `sid`/`script_id` → `scripts.lst` row → basename → the `.ssl` file** and
  hand the AI the actual code — the authoritative behaviour, comments and original names intact.
  The `.int` and friends are the **index and fallback** around that source: the **`.int` metadata
  reader** (procedure table — `start`/`map_enter_p_proc`/`talk_p_proc`/`destroy_p_proc`, plus
  imported/exported procs and the string table) confirms which proc hooks exist and *when* they
  run; the **`.msg`** of the same basename (existing `Msg` reader) supplies the NPC's dialogue
  lines; and only when the source is genuinely missing does the server fall back to **`int2ssl`
  decompilation** (lossy — see "SSL Script Editing Integration" for the toolchain/licensing). So
  `describe_script(sid)` returns the SSL source + proc hooks + linked dialogue, letting the AI read
  and reason about what an NPC actually does, not just summarize it.
- **Pathing, blocking & reachability — Medium.** Build a walkability view of each elevation: a hex
  is blocked if it holds a `NO_BLOCK`-clear object, with the **invisible movement blockers**
  (`OBJECT_FLAT` scenery over `block.frm`, the same signal the generator filters on) called out
  separately from real cover. Surface the **exit grids** (scenery PIDs **16–23**) and their
  destination map/elevation/hex as the map's connectivity graph, and flood-fill from each exit /
  player-start to report **reachable vs. walled-off regions** and orphaned objects. This is what
  turns "a list of hexes" into "you enter here, the locked room in the NE is unreachable without the
  key, and this exit leads to the world map."
- **Map framing & globals — Small.** Header-level context the AI needs to reason about the rest:
  enabled elevations, **player start position/elevation/orientation**, map flags
  (save/`pipboy`/elevation flags), **local/map variables** (`.gam`/`MAP_VARS` counts and the LVAR
  block), and the map's own script. Cheap — it's all in the `MAP` header already parsed.

Together these let the server answer open-ended questions ("who guards the entrance?", "can the
player reach the vault?", "what does this terminal say?") by cross-referencing **proto + SSL
source + `.msg` + `ai.txt` + exit graph** — the same sources the engine (and a script author)
consults. Every reader needed is either already in `vault` (`Pro`, `Msg`, `Map`, `.lst`), the
plain-text `.ssl` source itself, or a small INI/metadata addition (`ai.txt`, `.int` header); none
requires the Qt layer or a running game.

## Estimate

A read-only "describe/analyze" server is a **few days**; adding write tools is **another few
days** (~**1–2 weeks** for a solid read+write server), mostly tool-surface design + a JSON-RPC/CLI
shim, not format work. Script/dialog understanding reuses the `Msg` reader + the proposed `.int`
metadata reader. Visual analysis becomes a **Small–Medium** add-on once the Qt-free `gecko_render`
extraction is done (the renderer is already Qt-free; it just needs to move to a shared library +
an offscreen `sf::RenderTexture` wrapper). Start with `gecko-cli` + read tools, since that's
immediately useful and de-risks the rest.

---

# Area-Fill + Luau Plugins — Unified Design Proposal

> **Status:** Feature A (area fill, phases A0–A3) has **LANDED** — a Luau-driven "Fill Selection"
> ships with a Fill dialog, ghost preview, and the `scripts/fills/*.luau` recipes, exercised through
> `MapScriptApi` over a placement batch and surfaced in `gecko-cli`/MCP `fill`. Feature B (the Luau
> plugin system) remains a proposal. The design below is kept as the reference write-up for both.

This proposal specifies two features that share one substrate: **Feature A**, a Luau-and-data-driven *area fill* ("Fill Selection") that closes the `autotile_floor` / "paint a pattern of tiles" gap; and **Feature B**, a *Luau plugin system* that lets third parties add tools, panels, menus, and event handlers. The decision throughout is to **build one set of seams and exercise it twice**: area-fill is the first first-party consumer of the same selection-projection, ghost-preview, `ITool`, and `MapScriptApi`-over-a-batch machinery that the plugin system opens to third parties. Engine-data-fidelity is non-negotiable: PIDs/directions/flags/tile-ids stored and replayed verbatim, no fallback label tables, no rotation math, validated readers with no silent fallback.

---

## 1. Where things stand

Gecko already ships a coherent two-tier authoring stack. Both features extend it; neither replaces it.

**Tier-1 — declarative patterns (`src/pattern/`), always compiled.** `pattern::Pattern` is a POD with one-or-more pre-authored `PatternVariant`s (orientation is a variant set, never a rotation transform — `Pattern.h:30-33`). `PatternStamper` has a clean **pure `plan()` / impure `stamp()`** split (`PatternStamper.cpp:22-77` vs `146-168`): `stamp()` wraps N object placements + tile edits in one `ScopedUndoBatch`. Capture (`PatternBuilder::fromSelection`), serialize (`PatternSerializer` Qt-side / `cli/PatternJson` nlohmann-side, wire-identical, validated, no silent fallback), thumbnail (`PatternThumbnail` reusing `plan()` at identity anchor), and click-to-stamp (`EditorMode::StampPattern` + live ghost at `DRAG_PREVIEW_ALPHA`) all converge on this one POD. The library lives at `PatternLibrary::rootDir()` (`<ConfigLocation>/gecko/patterns`).

**Tier-2 — Luau scripting (`src/scripting/`), gated `GECK_SCRIPTING_ENABLED`.** `LuaScriptRuntime::run` is **fresh-VM-per-run** (`luaL_newstate` `:51` → `lua_close` `:134/:149`), synchronous on the UI thread, with the binding/`args`/`print` set **before** `luaL_sandbox(L)` (`:110`) so they freeze as read-only globals. There is **no interrupt, no timeout, no memory cap** today. `MapScriptApi` is the host façade (queries, coordinate helpers, `placeProto/placeObject/paintFloor/paintRoof`, `placeStamp`, `placeExitGrid*`, `newMap`, `setPlayerStart`), bound via the `GECK_SCRIPT_API` X-macro whose shared `&MapScriptApi::name` reference is the anti-drift guard (`LuaScriptRuntime.cpp:66`). Convention: **errors raise, "not applicable" stays a value** (off-grid place → `false`, unknown tile → `-1`). `MapScriptApi` holds map/elevation **by reference at construction** (`MapScriptApi.h:211-214`) and is rebuilt per run.

**Headless — CLI/MCP, always compiled façade.** `MapScriptApi`/`ScriptApiReference` compile without Lua; `gecko-cli generate` / MCP `generate` drive the same façade with `buildSprites=false` over a `CallbackCommandHost` (`MapGenerator.cpp:50-105`). `--stamp name=file.json` loads Tier-1 patterns into `addStamp`.

**Mapping the two asks onto what exists:**

| Ask | Already covered | Net-new |
|---|---|---|
| **A. Area fill** | The *commit* primitive (`PatternStamper`'s pure-plan + one-`ScopedUndoBatch`), the *preview* primitive (`PatternSprite` ghosts + a typed `RenderData` field), the *library/serialize* discipline, headless `MapScriptApi` | A **selection→area** value object; a **plan-sink** inside `MapScriptApi` mutators (preview-then-replay); a **seamless-floor (`autotileFloor`) primitive**; a **`FillRecipe`** declarative format + C++ runner; seeded scatter (weight/noise/density/spacing/jitter); a Fill dialog/preview; CLI/MCP `fill` |
| **B. Plugin system** | `MapScriptApi`+`ScopedUndoBatch` as a UI-free undoable mutation API; narrow `*Context` host interfaces (`ExitGridContext.h`) as the decoupling shape; `GameResources&` injection; the Luau VM bring-up code | **`ITool`+`ToolRegistry`** replacing the closed `EditorMode` enum + scattered switches; a **persistent per-plugin VM** with persistent print capture; **`MapScriptApi::retarget`**; **capability-gated binding**; manifest + permission model; declarative `Gui.*`; lifecycle/discovery; resource limits |

The architecture brief is explicit: **there is no tool/panel/menu registration seam today** — every tool is an `EditorMode` value wired through hand-written `switch`es in `InputHandler`, `EditorWidget::setMode`, and `MainWindow::syncToolModeActions`. Feature B's first job is to add that seam; Feature A is what proves it.

---

## 2. Dims-mapper benchmark

Read from the Dims source vendored at `reference/F2_Mapper_Dims-master/Mapper/` and web-confirmed for the official BIS mapper:

- **F2_Mapper_Dims.** Single-tile pen (one tile per click, ghost preview, *no* drag-paint, *no* brush size). Rectangular **single-tile** region fill (`SetFloorRegion`/`SetRoofRegion`, `tileset.cpp:52-70`) — not flood fill, not a pattern. A **random-object scatter brush**: 7 INI-defined sets (`DrawObject.ini`: Tree/Grass/Rock/Small Rock/Dirt/Corn #1/Corn #2), `CRandomObj::GetObjectID()` returns `objPid[random(count)]` — **uniform** random, one object per click, re-rolled each click. **No density, radius, jitter, rotation, weighting, or area fill.** Templates/prefabs were **stubbed and never built** (`objtempl.h` empty).
- **Official BIS mapper2.exe.** "Use Pattern" (Alt-Y): pick a pre-made **tile** pattern, stamp it, Plus/Minus change stamp size (2×2…N×N), right-click exits. A genuine resizable pattern stamp — but **tiles only**, fixed built-in list, no random object scatter, no user-authored prefabs.

**How this proposal beats both, by construction:**

1. **Weighted, noise-clumped, density-controlled area scatter** — Dims has uniform per-click selection only; the BIS mapper has none. `FillRecipe.scatter` carries a cumulative-weight palette, value-noise clumping/thresholding, density, spacing, direction jitter, and occupancy — applied across an arbitrary selection in **one undo step**.
2. **Seamless multi-tile floor** (`autotileFloor`) — neither legacy tool repeats a multi-tile floor *material*; Dims fills a rect with one id, the BIS mapper stamps a fixed pattern. We pick each cell's tile from its neighbour mask against a data-driven `FloorTileSet`.
3. **Real saved prefabs in the fill** — scatter palette entries may be `"stamp":"name"` → `placeStamp`, so a captured "bush+rock cluster" scatters as a unit. This is the feature Dims stubbed.
4. **Live ghost preview + locked seed reproducibility** — preview *is* the apply (plan-capture → replay), even for nondeterministic scripts.
5. **INI parity, upgraded** — we keep Dims' best idea (data-defined sets, low-friction extensibility) but as validated JSON with **weights**, and shared with the Tier-1 library, CLI/MCP, and Luau.

The one Dims/BIS idea we *also* deliver and they lack: **drag-to-paint with an adjustable footprint** (the freehand Fill Brush, §3 Phase F).

---

## 3. Feature A — Luau-driven area fill

### 3.1 Execution model (the heart)

Both tiers drive **one** `MapScriptApi`; a `FillPlan*` sink sits inside its mutators.

```
 Tier-1 FillRecipe ─┐                         ┌─ sink active? RECORD into FillPlan
 (C++ runner)       ├─▶ MapScriptApi mutators ─┤
 Tier-2 Luau fill ──┘   paintFloor/placeProto  └─ else COMMIT live via controller
                        autotileFloor/placeStamp
        preview: run with FillPlan* installed ─▶ ghosts (DRAG_PREVIEW_ALPHA)
        apply:   PlacementBatch.replay(plan)  ─▶ ONE ScopedUndoBatch
```

- **Preview** runs the fill with a `FillPlan*` installed. Mutators resolve art/tile-ids at full fidelity but **record** rather than commit. Rendered as semi-transparent ghosts.
- **Apply** is `PlacementBatch::replay(plan)` inside one `ScopedUndoBatch` — **no re-run**, so preview == apply byte-for-byte even for nondeterministic Tier-2 scripts.
- The plan-sink is the single place that enforces **clip-to-area** and the **placement cap**.

**Capture coverage — the two real chokepoints, plus the stamp fix.** Insert the sink at exactly two points: `registerObject` (`MapScriptApi.cpp:288-330`, funnels `placeProto/placeObject/…XY/placeExitGrid*`) and `paintTile` (`:361-371`, funnels `paintFloor/paintRoof/autotileFloor`). **`placeStamp` does *not* route through these** — `MapScriptApi::placeStamp` (`:432-448`) builds its own `PatternStamper` and calls `stamp()`, which opens its **own** `ScopedUndoBatch`. Left unfixed, a stamp palette entry would mutate the live map and push a real undo entry *during preview*. **Fix (folded in):** add a sink-aware planning entry to `PatternStamper` — `void planInto(FillPlan&, const PatternVariant&, int targetHex, int elevation)` that runs the existing pure `plan()` and resolves its `ObjectPlacement`/`TilePlacement`s into the `FillPlan` (building sprites when `_buildSprites`), committing nothing. `MapScriptApi::placeStamp` becomes: `if (_planSink) stamper.planInto(*_planSink, …); else stamper.stamp(…);`. This preserves the pure/impure split and makes stamp scatter capturable. Factoring `PlacementBatch` out of `PatternStamper::stamp` fixes the *commit* side; `planInto` fixes the *capture* side — both are required.

### 3.2 Data model (new C++, always compiled unless marked)

- `src/scripting/EditArea.h` — `{ std::vector<int> hexes, floorTiles, roofTiles; }`, each **sorted ascending** (canonical order is contractual so seeded draws reproduce). Built by the host from `SelectionManager::getHexesInArea`/`getTilesInAreaIncludingEmpty`/`getObjectsInArea` (a `sf::FloatRect`) **or** — when the committed selection has no rect — from the discrete `SelectionState` getters (`getHexIndices/getFloorTileIndices/getRoofTileIndices/getObjects`), then `std::sort`ed. *(Folded in: `selectionArea` is `std::optional` and a discrete object/hex selection has no rect; never assume a rect exists.)*
- `src/scripting/FillPlan.h` — `{ std::vector<TileChange> tiles; std::vector<std::pair<std::shared_ptr<MapObject>,std::shared_ptr<Object>>> objects; int dropped; }`.
- `src/pattern/FloorTileSet.{h,cpp}` (+ Qt `FloorTileSetSerializer`, Qt-free `cli/FloorTileSetJson`) — the autotile material (§3.4).
- `src/pattern/FillRecipe.h` + `FillRecipeSerializer.{h,cpp}` (Qt) + `src/cli/FillRecipeJson.{h,cpp}` (nlohmann) — wire-compatible, validated, no silent fallback, same split and `checkInt` range discipline as `PatternSerializer.cpp:62-78`.
- `src/pattern/FillRecipeRunner.{h,cpp}` — Tier-1 interpreter; ctor `(MapScriptApi&, const FillRecipe&, uint32_t seed)`, holds `std::mt19937`; `FillResult run()` (floor first, then scatter). **Bounded by construction → no sandbox.**
- `src/pattern/PlacementBatch.{h,cpp}` — factored out of `PatternStamper::stamp` (`:146-168`): one `ScopedUndoBatch`, replays objects via `registerObjectPlacement` (GUI) / `registerObjectData` (headless) + tiles via `applyTileChanges`+`registerTileEdit`. `PatternStamper` is refactored to use it, so stamp and fill share one tested commit path.
- GUI: `src/ui/dialogs/FillDialog.{h,cpp}`; `src/pattern/FillThumbnail.{h,cpp}` (reuses `ThumbnailComposer`/`PatternSprite`); a **generic ghost-overlay field** on `RenderingEngine::RenderData` (see §5 — shared with plugin tool previews, not a fill-specific field).
- *Gated:* the Tier-2 prelude/run wiring + "Edit as Script". CLI `gecko-cli fill` + MCP `fill` are **always-on for recipes** (`buildSprites=false`, the `MapGenerator.cpp:50-105` context).

### 3.3 New Luau / `MapScriptApi` surface

Host-only (not script-bound, like `addStamp`): `void setArea(const EditArea*)`, `void setPlanSink(FillPlan*)`, `void registerFloorSet(std::string, FloorTileSet)`.

Script-bound — add to `GECK_SCRIPT_API` (`ScriptApiReference.h:13-50`), each backed by a real `MapScriptApi::name` (the `&MapScriptApi::name` reference is the drift guard). `noise2d`, `paintFloor[XY]`, `placeProto[XY]`, `placeStamp`, `proto`, `tileId`, `hexCol/hexRow`, `tileCol/tileRow`, `mapSceneryHistogram` **already exist**:

```c
/* selection / area (input; EditArea borrowed, host-set per run) */
X(hasArea,           "() -> bool",                  "True if a selection area is bound to this run.")
X(areaHexes,         "() -> {hex,...}",             "Hex indices in the selection, ascending.")
X(areaFloorTiles,    "() -> {tileIndex,...}",       "Floor-tile indices in the selection, ascending.")
X(areaRoofTiles,     "() -> {tileIndex,...}",       "Roof-tile indices in the selection, ascending.")
X(areaContainsHex,   "(hex) -> bool",               "Is hex inside the selection?")
X(areaContainsTile,  "(tileIndex) -> bool",         "Is tile inside the selection?")
X(areaFloorEdgeMask, "(tileIndex) -> int",          "8-neighbour selection-membership mask of a floor tile.")
/* seamless multi-tile floor */
X(autotileFloor,     "(setName) -> int",            "Paint the selection's floor from a FloorTileSet by neighbour mask. Returns tiles painted.")
X(autotileFloorAt,   "(setName, tileIndex) -> tileId","Tile autotile WOULD choose for one cell; paints nothing.")
/* deterministic seeded helpers */
X(rng,               "() -> [0,1)",                 "Deterministic PRNG draw seeded from args.seed.")
X(rngInt,            "(lo, hi) -> int",             "Deterministic integer in [lo,hi].")
X(weightedPick,      "(values, weights, r) -> value","Pick values[i] by weight using r in [0,1).")
X(objectAt,          "(hex) -> pid",                "PID of a blocking object at hex in the COMMITTED map, else 0.")
X(noise3d,           "(x, y, z) -> [0,1]",          "Coherent value noise with a third (seed/octave) axis.")
```

`area.forEachHex/forEachFloorTile` ship as a **bundled trusted prelude** `resources/scripts/lib/fill.luau`, prepended to Tier-2 source (fresh-VM-per-run makes concatenation correct; frozen by the same `luaL_sandbox`):

```lua
local area = {}
function area.forEachHex(f)       for _,h in ipairs(api:areaHexes())      do f(h, api:hexCol(h), api:hexRow(h)) end end
function area.forEachFloorTile(f) for _,t in ipairs(api:areaFloorTiles()) do f(t, api:tileCol(t), api:tileRow(t)) end end
area.hexes = function() return api:areaHexes() end
return area
```

**`objectAt` is a committed-map query, not a pending-plan query** *(folded in)*. Because apply is replay, the script runs once into the sink; `objectAt(hex)` reads `_map` and **never sees placements already recorded in this run's `FillPlan`**. Intra-fill de-dup/spacing must be tracked by `FillRecipeRunner` (which already maintains a touched-cell set) or by the Tier-2 script itself. This is documented in the `objectAt` doc string and the fill-authoring guide; a script that relies on `objectAt` for self-occupancy will silently stack objects. The "errors raise, N/A is a value" contract holds throughout (`objectAt`→0 when free; unknown set/tile → `ScriptError`).

### 3.4 The `autotileFloor` primitive (closes the gap)

A `FloorTileSet` is a data-driven **mask → authored tile** table — no rotation (FO2 `edg*` art is edge-specific; same ethos as `Pattern.h:30-33`). Floor is a plain 100×100 square grid (`PatternStamper.cpp:60-72` notes "no parity offset"), so 4/8-neighbour masks are trivial.

```jsonc
// resources/scripts/fills/sets/desert_sand.json  (bundled; users override under .../fills/sets/)
{ "name":"desert_sand", "version":1, "center":"edg5000",
  "neighborhood":"blob8",                 // "edges4" (16 masks) | "blob8" (47-tile reduction)
  "variants": { "0":"edg5001", "255":"edg5000", "17":"edg5010" },  // mask -> tile FRM name
  "fallback":"center" }                   // mask not listed => center (explicit, never PID-0)
```

`autotileFloor(set)`: for each `t` in `areaFloorTiles()`, build the mask from **in-area** neighbours (`blob8` counts a diagonal only if both flanking cardinals are set, reducing 256→47), look up `variants[mask]` else `center`, resolve via `tileId` (−1 → `ScriptError`, never PID-0), `paintFloor(t,id)`. Because it routes through `paintTile`, it is **captured by the plan sink for free** — autotile previews automatically. `areaFloorEdgeMask` exposes the raw mask so advanced Tier-2 scripts can autotile by hand or blend into pre-existing terrain via `getFloorXY` (an optional advanced flag; MVP masks against selection membership only). Registered exactly like stamps: `registerFloorSets()` scans bundled `resources/scripts/fills/sets/*.json` first, then user `.../fills/sets/` last (user wins), mirroring `registerLibraryStamps` (`EditorWidget.cpp:384-419`).

### 3.5 `FillRecipe` (Tier-1) and Tier-2 brushes

```jsonc
// .../gecko/patterns/fills/desert_scrub.fill.json   ("kind":"fill" distinguishes it in the shared library)
{ "name":"Desert scrub", "version":1, "kind":"fill",
  "floor":  { "mode":"autotile", "set":"desert_sand" },   // none | single | scatter | autotile
  "roof":   { "mode":"none" },
  "scatter":{ "palette":[ {"proto":["scenery",102],"weight":5},
                          {"proto":["scenery",116],"weight":1},
                          {"stamp":"small_bush_cluster","weight":2} ],   // Tier-1 prefab via placeStamp
              "paletteFromMap":"maps/desert1.map",        // optional: seed palette via mapSceneryHistogram
              "density":0.25, "noiseScale":0.08, "noiseThreshold":0.45,
              "jitterDirection":true, "spacing":1, "respectOccupancy":true },
  "clipToSelection":true, "seed":null }                   // null => per-run; int => locked
```

`FillRecipeRunner::run()` paints floor (`single`→`paintFloorXY`; `scatter`→weighted+`noise2d`; `autotile`→`autotileFloor`), then scatter: cumulative-weight palette; per hex sample `noise2d((col+ox)*scale,(row+oy)*scale)`; below `noiseThreshold`→clearing; else with prob `density`, `weightedPick`→`placeProtoXY`/`placeStamp`, honouring `spacing`/`jitterDirection` and a **runner-maintained occupancy set** (not `objectAt`, per §3.3). Validation reuses `checkInt`; `proto` types validated through `MapScriptApi::proto` (raises on bad type), tile names through `tileId` (−1 → reject) — engine values verbatim, no fallback table.

A **Tier-2 brush** is a Luau script plus a thin `*.fill.json` manifest (`"script":"x.luau"` + a typed `params:[{id,type,role,…}]` array) whose params drive generated dialog controls **and** are passed as `args` (closing the "Console passes no args" gap, `EditorWidget.cpp:431`). The dialog's "Edit as Script…" lowers a `FillRecipe` to an equivalent Luau script and drops it into `ScriptConsoleWidget::setSource` (`MainWindow.cpp:1020-1027`) — the Tier-1→Tier-2 graduation path (scripting builds only).

### 3.6 UX

**Core MVP — an action on the selection, no new `EditorMode`.** Edit-menu "Fill Selection…" + toolbar button (`addMenuAction`/`addToolAction`), enabled only when the selection is non-empty **and the relevant layer is present** (an `autotile`/floor fill requires `floorTiles`; gate it so a pure object/hex selection can't run a no-op floor fill — folded in from the `EditArea` rect-less correction). `FillDialog` (structure modelled on `PatternBrowserDialog`): left, a Fills browser over `PatternLibrary::rootDir()/fills` + bundled, thumbnails via `FillThumbnail`; right, auto-generated controls (floor on/off+mode, scatter on/off, density/spacing/jitter/clip, a **seed field** defaulted random, shows the resolved seed after a run, lockable); bottom, Live-preview toggle, "Edit as Script…" (gated), Apply/Cancel. Live preview runs the fill into a `FillPlan`, converts to ghosts at `DRAG_PREVIEW_ALPHA`, **recomputed only on parameter change (debounced), never per frame**. Apply commits the previewed plan; Cancel discards (nothing was committed). Post-apply runs the existing `mutated()` resync (clear selection/visualizer, refresh Map Info, emit `mapModifiedByScript()`, `EditorWidget.cpp:435-447`).

**Freehand Fill Brush (final phase) — built as a native `ITool`, not a bespoke `EditorMode`** (see §5). Footprint = tile/hex disc of radius `size`; one `beginBatch("Fill: <name>")` on press, `endBatch()` on release; track touched cells to de-dup and respect occupancy across overlapping footprints; footprint ghost via the shared overlay. Reuses `FillRecipe`/`FillRecipeRunner`/`PlacementBatch` wholesale — only the area source differs (footprint vs selection), so freehand applies incrementally inside the manual batch rather than via plan-replay.

### 3.7 Undo as one step

Always one entry. Selection fill: `PlacementBatch::replay` wraps everything in one `ScopedUndoBatch` (`ObjectCommandController.h:172-197`); tile paints and object placements interleave and revert in reverse, identical to `PatternStamper::stamp`. Freehand: one manual `beginBatch`/`endBatch` per stroke; nested batches are safe (only the outermost `endBatch` flushes, `:71`). **Mandatory, not cosmetic:** `UndoStack` caps at `maxCommands=100` and evicts oldest (`UndoStack.h:17,31-33`) — a 5,000-tile fill unbatched wipes all history; batched it is one Ctrl-Z. Preview never enters undo (the sink never calls the controller).

### 3.8 Pattern-library reuse

Fills live in `fills/` under the existing `PatternLibrary::rootDir()`; one browser with a Patterns/Fills filter. Scatter palette `"stamp":"name"` entries route to `placeStamp` (`MapScriptApi.cpp:432-448`), so a prefab captured via "Save Selection as Pattern" or `extract_pattern` scatters as a cluster. A captured selection's `mapSceneryHistogram` seeds a starter palette. Fills and prefabs share library, browser, thumbnail, and `placeStamp`/`addStamp` plumbing.

### 3.9 Phased plan (A)

- **A0 — Selection + plan/apply core (always compiled, headless-testable).** `EditArea` (both rect and discrete-getter construction); `setArea` + area accessors (X-macro + reference TU); `FillPlan` + `setPlanSink`; the two sink insertion points; `PatternStamper::planInto` (stamp capture) + `PlacementBatch` (factored from `stamp`); seeded `rng/rngInt/weightedPick/objectAt/noise3d`. Unit tests: same seed → identical `FillPlan`; replay == captured; stamp palette entries captured (not committed) under a sink.
- **A1 — Tier-1 recipes (always compiled; works in default OFF build).** `FillRecipe` + serializers + validator; `FillRecipeRunner` (floor single/scatter; scatter palette/noise/density/spacing/jitter/runner-occupancy); CLI `fill` + MCP `fill`. First user value, headless, beats Dims.
- **A2 — Seamless floor.** `FloorTileSet` + reader + `registerFloorSet`; `autotileFloor`/`autotileFloorAt`/`areaFloorEdgeMask`; bundle `desert_sand`/`cave_rock`. Closes `autotile_floor`.
- **A3 — Interactive GUI MVP.** Shared ghost-overlay `RenderData` field; `FillDialog` (browser + generated controls + seed/lock + debounced live preview + Apply/Cancel); Edit-menu/toolbar action; `FillThumbnail`. **No `EditorMode`.** First end-user release.
- **A4 — Tier-2 Luau (gated).** `fill.luau` prelude; custom Luau fills via the dialog; pass `args`; recipe→Luau lowering. **Prerequisite: the sandbox interrupt+deadline (B-side) must land first** (see §3.10).
- **A5 — Freehand Fill Brush.** A native `ITool` on the registry introduced by Feature B (§5), not a new bespoke mode.

### 3.10 Sandbox ordering (folded-in correction)

`LuaScriptRuntime::run` has **no interrupt and no timeout** today, and `EditorWidget::runScript`→`runtime.run` is synchronous on the UI thread — a previewed Tier-2 fill with an infinite loop hangs the editor. **Therefore Tier-2-in-GUI (A4) must not ship before the interrupt+deadline exists.** Two acceptable orderings, pick one: (i) land the interrupt+deadline watchdog (Feature B's `LuaSandboxHost` work) before A4; or (ii) restrict A4 to *trusted bundled* fills until the watchdog lands. Tier-1 (A0–A3) is bounded by construction and needs none of this. The plan-sink additionally enforces a **placement cap** (`k × area.size()`, surplus → `++dropped`) and **clip-to-area**, which apply to both tiers.

---

## 4. Feature B — Luau plugin system

Plugins add **tools, panels, menus/toolbar buttons, and event handlers**. They need a **broader, capability-gated trust model than the generation sandbox** — and this must be stated plainly: the Tier-2 generation runtime is *safe-by-default-and-ephemeral* (fresh VM, run once, discarded), so it can afford zero limits; a plugin is **resident** (it must answer `QAction::triggered`, tool mouse events, and `on(event)` callbacks for the life of the session) and runs **untrusted third-party code on the UI thread**, so it requires per-plugin isolation, resource limits, and an explicit permission grant. These are different requirements, not a stricter version of the same thing.

### 4.1 Abstraction seams to add (named)

The architecture brief identifies the hard couplings; the plugin layer adds these seams (all **always compiled, no Lua dependency**, so native tools can adopt them and they are testable with plugins off):

1. **`ITool` + `ToolRegistry`** (`src/ui/tools/ITool.h`, `ToolRegistry.{h,cpp}`) — replaces the closed `EditorMode` enum + scattered switches with dynamic dispatch to an active tool. `ITool` exposes `id()`, `onActivate/onDeactivate`, `onMousePressed/Moved/Released(const ToolMouseEvent&)`, `onKey(const ToolKeyEvent&)`, and `ToolPreview buildPreview(const ToolMouseEvent&)`. **Engine coordinates are resolved by the host** (`hex/col/row/tileIndex` in `ToolMouseEvent`); tools never see `sf::Vector2f`, and `buildPreview` returns a *spec*, never SFML draws. Validate by porting one native tool (tile placement) onto `ITool` with no UX change.
2. **`EditorMode::PluginTool` + one generic `InputHandler` branch** — `onPluginTool{Pressed,Moved,Released,Key}` added **once**, forwarding to `ToolRegistry::active()`. Not per-tool callbacks.
3. **One generic ghost-overlay `RenderData` field** — populated by the active tool's `buildPreview` (and reused by Feature A's fill preview, §5). Replaces the bespoke-typed-field-per-preview pattern.
4. **MainWindow registration APIs** — `addPluginMenuItem/addPluginToolButton/addPluginDock/removePluginUi`, and relaxation of the fixed `std::array<QDockWidget*,6>` (`MainWindow.h:167-168`) into a `std::vector<QDockWidget*> _pluginDocks`. One `syncToolModeActions` case for `PluginTool`.
5. **`PluginToolHost`** — implements the **union** of the existing `*Context` methods (`getMap/getViewportController/getCurrentElevation/getSelectionManager/register*`, modelled on `ExitGridContext.h`) so plugin tools commit through `_controller.commandController()` exactly like native tools.

### 4.2 Persistent VM, print, and `retarget` (the three load-bearing host changes)

These are net-new and more invasive than "refs→pointers"; they land early with focused tests.

- **Phase-0 refactor `LuaSandboxHost`** — extract shared VM bring-up from `LuaScriptRuntime` (`luaL_openlibs`, the `capturePrint` closure, `luau_compile`/`luau_load`, and the critical **`luaL_sandbox` after binding** ordering). No behavior change; existing scripting tests stay green.
- **Persistent print capture.** `capturePrint` today carries a **lightuserdata upvalue pointing at a stack-local `result.output`** (`LuaScriptRuntime.cpp:27-44,59`) — that lifetime is invalid for a resident VM. The extraction must repoint `print` at a **persistent per-VM ring buffer** owned by the `PluginVm`, surfaced in the plugin's console dock.
- **`MapScriptApi::retarget(GameResources&, const HexagonGrid&, ObjectCommandController&, Map*, int elevation, bool buildSprites)`.** `_resources/_hexgrid/_controller` are references and `_map` is `Map&` (`MapScriptApi.h:211-214`); a persistent VM outlives any one map and survives elevation switches and `newEmptyMap()` swapping the underlying `Map` (owned as `std::unique_ptr<Map>&` inside `ObjectCommandController`). Convert internals to pointers and **audit the `_map == nullptr` state across *every* method, not just mutators** — queries (`getFloor`, `hexNeighbors`, `mapScenery`) assume a live map/grid and must return the N/A value or raise `ScriptError` when no map is open. The host re-points on File>New / load / elevation-switch. **Keep the value constructor** for the generation runtime and CLI/MCP (which build a fresh `MapScriptApi` per run and never call `retarget`). This is the single riskiest change and should land in Phase B2 with tests. *(Note: Feature A never needs `retarget` — its fills build a fresh `MapScriptApi` per run like `EditorWidget::runScript`. The invasive refactor is a plugin-only cost.)*

### 4.3 Manifest + capability/trust model

**Manifest is C++-parsed JSON, never executed Lua** — permissions/identity must be known before any plugin code runs. Validated, no-silent-fallback, same discipline as `PatternSerializer::deserialize`. **Capability gating is by *binding*, not runtime check** (defense-in-depth): a denied capability's function is simply **absent** from the VM (`attempt to call a nil value`), bound only when granted, *before* `luaL_sandbox` freezes globals. A `GECK_PLUGIN_API` X-macro mirrors `GECK_SCRIPT_API`, each entry carrying its required `Capability`.

**Enforcement invariant (folded in):** this works *only* because there is **one `lua_State` per plugin**. LuaBridge registers the class on `getGlobalNamespace(L)` per state, so a `map.read` plugin's VM does `beginClass` with the `GECK_SCRIPT_API_READ` subset and the write methods are **genuinely absent from that VM's metatable**. Split `GECK_SCRIPT_API` into `GECK_SCRIPT_API_READ` + `GECK_SCRIPT_API_WRITE` (with `GECK_SCRIPT_API = READ+WRITE`, so the generation runtime is byte-for-byte unchanged), and the binder selects which method set to register at bind time. It is all-or-nothing per VM — you cannot downgrade a single shared `api` object — which is fine given per-plugin VMs.

Coarse capability set:

| Capability | Tier | Grants | Cannot touch |
|---|---|---|---|
| `ui` | Standard | register menu/toolbar/dock/tool, status/notify, declarative widgets | raw Qt, other plugins' widgets, MainWindow internals |
| `map.read` | Standard | `api:` queries + coordinates + `editor:selection()` | other maps on disk |
| `map.write` | Standard | `api:` undoable mutators (place/paint/stamp/exit-grid) | non-undoable internals |
| `events` | Standard | subscribe to the fixed event list | post fake / cancel host events |
| `storage` | Standard | JSON KV under `plugins/<id>/storage.json` | other plugins' / global settings |
| `fs.read` | **Sensitive** | read files canonicalized + confined to plugin dir | writes; `..`/symlink escape |
| `net`, `fs.write` | — | **never bound in v1** | everything |

**Trust model, deliberately minimal for v1** *(folded in — the heavy machinery is over-built for hand-installed plugins).* v1 ships a **single install-time `PluginPermissionDialog`** (Standard vs Sensitive grouping, plain-language descriptions, per-cap toggles for Sensitive), persisted as a `PluginGrant`. **Deferred to a later phase, not v1:** SHA-256 package pinning, re-prompt-on-capability-widening, quarantine, the `manifest_version`/`apiVersion`/`plugin_abi` triple, `.gplug` packaging, and ed25519 signing. Until ABI 1 is frozen, gate real third-party *distribution*; hand-installed plugins are the v1 audience. `fs.read` is path-confined (canonicalized, symlinks resolved and re-checked, `..` rejected); `net`/`fs.write`/process-spawn/FFI are not bindable regardless of trust.

### 4.4 The Luau API (three namespaces)

`api:` is the **existing `MapScriptApi`** bound by capability (`map.read`→READ subset, `map.write`→READ+WRITE). No new map verbs.

```lua
-- editor:  app integration --------------------------------------------------
h = editor:addMenuItem{ menu="Edit", text="…", shortcut="Ctrl+Shift+G", icon="a.png", onTrigger=fn } -- ui
h = editor:addToolButton{ text="Scatter Brush", icon="a.png", onTrigger=fn }                          -- ui
h = editor:addDockPanel{ id="scatter.panel", title="…", area="right", ui = Gui.Column{...} }          -- ui
t = editor:registerTool{ id="scatter", title="…", icon="a.png",
       onActivate=fn,onDeactivate=fn,onMouseDown=fn,onMouseMove=fn,onMouseUp=fn,onKey=fn,
       preview=function(ev) return {tiles=..., objects=...} end }                                     -- ui
editor:activateTool(t)
editor:setWidget(id, props)   editor:getWidget(id) -> props   editor:removeUi(h)                      -- ui
editor:status(text)           editor:notify("warn", text)                                             -- no cap
editor:hasSelection() -> bool                                                                          -- map.read
editor:selection() -> { rect=, hexes={}, floorTiles={}, roofTiles={}, objects={ {hex,pid} } }         -- map.read
editor:currentElevation() -> int   editor:currentMapPath() -> string|nil                              -- map.read
editor:undoBatch(desc, function() ... end)                                                            -- map.write
sub = editor:on(event, fn)   editor:off(sub)                                                          -- events
-- plugin:  self + sandboxed services ---------------------------------------
plugin.id, plugin.version, plugin.dir   plugin:log(level,msg)   plugin:capabilities() -> {...}
plugin:asset(rel) -> path               -- bundled assets always readable, NO fs.read
plugin:require("submodule")             -- resolved INSIDE plugin.dir only
plugin:store(k,v)  plugin:load(k)  plugin:keys()  plugin:delete(k)                                    -- storage
plugin:readFile(rel)  plugin:listDir(rel)  plugin:exists(rel)                                         -- fs.read
editor:registerStamp(name, "assets/hut.json")   -- PatternSerializer + addStamp (no fs cap)
```

`editor:selection()` is the **same `SelectionManager`→tables projection** Feature A uses for `EditArea` (§5). Objects surface as `{hex, pid}` — **no `Object`/`QObject` ever crosses to Lua**.

**Declarative UI (`Gui.*`) — the only way a plugin builds widgets.** A closed vocabulary (`Column/Row/Group/Label/Spacer/Button/Combo/Checkbox/Slider/SpinBox/List/LineEdit/IconButton`), materialized by `DeclarativeUiBuilder` into real `QWidget`s themed via `ui::theme`. No `findChild`, no metaobject reflection, no raw widget pointer. Live updates by opaque string `id` (`editor:setWidget`). Mirrors Qt Creator 14's constrained `Gui` module.

**Three enforced Qt-safety rules:** (1) no Qt pointer enters Lua — registration returns **opaque string handles**; the real `QAction`/`QDockWidget` lives in `Plugin::uiHandles` as `QPointer`, validated against the calling plugin. (2) UI is data, not imperative Qt. (3) every boundary is `pcall`-wrapped with a per-plugin `debug.traceback` and the interrupt deadline armed — an error or `ScriptError` is caught at the edge and **never** reaches the Qt event loop.

### 4.5 Interactive tools, end to end

`editor:registerTool{...}` → `PluginManager` builds a `LuaTool : ITool`, registers it in `ToolRegistry`, calls `MainWindow::addPluginToolButton` (checkable, exclusive with native tools). Click → `setMode(EditorMode::PluginTool)` + `ToolRegistry::setActive(id)`. Mouse events → the one generic `InputHandler` branch → `EditorWidget` resolves world→hex via `viewport().worldPosToHexIndex` (the `stampPatternAt` path, `EditorWidget.cpp:1255`) → the active `LuaTool` → `PluginInvoker::call` into Lua with the **engine-coordinate** event. Hover → `buildPreview` returns a ghost spec → the shared overlay field → rendered next frame via `PatternSprite::buildSpriteObject`/`buildTileSprite` + `DRAG_PREVIEW_ALPHA`. Commit → `api:` mutators inside one undo batch → post-mutation resync.

### 4.6 Undo, threading, error containment

**Undo.** All plugin mutation goes through `MapScriptApi`→`ObjectCommandController`. `PluginInvoker` opens a `ScopedUndoBatch("<Plugin>: <action>")` around each dispatched `map.write` callback (one menu click / event = one Ctrl-Z), structurally identical to `LuaScriptRuntime::run` wrapping a whole run. Drag tools open the batch on `onMousePressed` and flush on `onMouseReleased` (one stroke = one entry). `editor:undoBatch`/`api:beginBatch/endBatch` allow explicit grouping; nested batches are safe. Mandatory because `UndoStack` caps at 100. `ScopedUndoBatch`'s destructor flushes even on a raised callback.

**Threading.** All plugin code runs on the UI thread; **the host builds `buildSprites=true`** like `EditorWidget::runScript`. No thread spawning escape exists. **Honest scope of the watchdog *(folded in):* the interrupt watchdog bounds runaway *Lua loops*, not heavy *host calls*.** `lua_callbacks(L)->interrupt` fires at Lua instruction boundaries only; a single long bound C++ call invoked from Lua — `mapSceneryHistogram`/`loadReferenceMap` over a big map, a `placeStamp` of a large prefab, or building thousands of sprites with `buildSprites=true` — is not preemptible and will blow the deadline and stutter the ~60 fps loop. The watchdog makes infinite Lua loops catchable; it does not make every host call cheap.

**Memory + placement caps *(folded in):*** the tracking allocator on `lua_newstate(allocf)` bounds **only the Lua heap**. The `Object`/`MapObject`/`sf::Sprite`/`std::vector` results created **C++-side** by `api:` calls are invisible to it — a `map.write` plugin can exhaust host memory while under the Lua cap. Therefore `map.write` plugins also get the **placement/result cap** Feature A introduces in the plan-sink (refuse beyond `k × area.size()`/per-dispatch budget; surplus → reported `dropped`), not just the allocator.

**Error containment.** Every boundary `pcall` + traceback; the C++ caller is `noexcept` at the edge. Fault accounting: consecutive errors/timeouts increment `faultCount`; after a threshold the host auto-disables the plugin, tears down its UI, and shows a dismissible banner with Re-enable + traceback. A sandboxed Luau plugin cannot segfault (no FFI/raw memory), so "crash" reduces to error/timeout/OOM — all contained. **Teardown is total:** `Plugin` owns every `UiHandle`/`EventSub`/`toolId`, so disable/quarantine/reload removes every `QAction`/`QDockWidget`/tool/subscription, `editor:off`s all subs, and `lua_close`es the VM — leak-free, which is what makes hot-reload safe.

### 4.7 Discovery, lifecycle, hot-reload

Scan `<ConfigLocation>/gecko/plugins/*/plugin.json` (user, writable) and bundled `resources/plugins/*/` (read-only), dedupe by `id` with **user shadowing bundled** — the same precedence as `registerLibraryStamps`. Invalid manifests become `Faulted` rows with a reason, never silently dropped. **Enable:** resolve grant (prompt if incomplete) → build `PluginVm` (tracking allocator, openlibs, persistent print ring, capability-gated `PluginBinder::bind`, interrupt watchdog, `luaL_sandbox`, seed) → compile+load entry, run once to capture callbacks as registry refs → register UI/tools/events → `Enabled`. **Disable:** optional `onDisable` (pcall) → total teardown → `lua_close`. **Hot-reload (dev-mode toggle):** `QFileSystemWatcher` debounced → disable → re-scan → enable; `storage` persists, UI rebuilds from scratch.

### 4.8 CMake gating

`option(GECK_ENABLE_PLUGINS … OFF)` that **requires** `GECK_ENABLE_SCRIPTING` (configure error otherwise). The **seam is always compiled** (`ITool`, `ToolRegistry`, `EditorMode::PluginTool`, the overlay field, MainWindow `addPlugin*`). The **Lua host is gated** behind `GECK_PLUGINS_ENABLED`: all of `src/plugin/*` and `src/ui/plugin/*`, with MainWindow's Plugins menu/discovery `#ifdef`-guarded exactly as `GECK_SCRIPTING_ENABLED` guards the console today.

### 4.9 Phased plan (B)

- **B0 — `LuaSandboxHost` extraction** (pure C++, no behavior change; persistent-print-ring repoint designed in). Existing scripting + tests green.
- **B1 — The seam** (pure C++, no Lua): `ITool`+`ToolRegistry`; `EditorMode::PluginTool` + one `setMode`/`syncToolModeActions` case; one generic `InputHandler` branch; shared overlay field; MainWindow `addPlugin*`/`removePluginUi` + `_pluginDocks`. **Validate by porting tile placement onto `ITool` with no UX change.**
- **B2 — Persistent VM + lifecycle + manifest (no UI registration, read-only `api`).** `PluginManifest` parse, `PluginManager` discovery/enable/disable, `PluginVm` (allocator cap, watchdog, print/log ring, `pcall` isolation, auto-disable on fault), **`MapScriptApi::retarget` with the full null-safe audit**, `api` bound read-only behind `map.read`, basic Plugin Manager dialog. **This is the plugin MVP.**
- **B3 — `editor:` registration + write.** `READ/WRITE` `beginClass` split; `map.write` with auto-batch + resync + placement cap; `addMenuItem`/`addToolButton`; install-time permission prompt + grant; `storage`.
- **B4 — Panels + `Gui.*`.** `DeclarativeUiBuilder`, `addDockPanel`.
- **B5 — Tools + events.** `LuaTool`+`registerTool`+preview rendering+stroke batching; `PluginEventBus` + `editor:on/off`.
- **B6 — Reference plugin + DX.** A reference plugin; `fs.read` confined cap; hot-reload; `gecko-cli plugin scaffold`; `plugin_api` MCP tool. *Packaging/signing/quarantine/version-triple deferred beyond v1.*

---

## 5. How A and B fit together

**Area-fill brushes are the first first-party "plugins."** Build the seams once, exercise them with first-party fill, then open them to third parties. Four shared mechanisms — do not build them twice:

1. **Selection → plain data.** `EditArea` (Feature A) and `editor:selection()` (Feature B) are the **same `SelectionManager`→tables/vectors projection** (`getHexesInArea`/`getTilesInAreaIncludingEmpty`/`getObjectsInArea` from a rect; the discrete `SelectionState` getters otherwise; objects as `{hex,pid}`, no `Object` crossing to Lua). One implementation, two thin adapters (a borrowed `EditArea*` bound per fill-run vs a live `editor:selection()` query).

2. **One ghost-overlay `RenderData` field.** Feature B introduces a single generic overlay (populated by `ITool::buildPreview`). Feature A's fill preview and the freehand Fill Brush populate the *same* field rather than a bespoke `fillPreview`/`stampPreview`/`pluginToolPreview` triple. All three render through `PatternSprite::buildSpriteObject`/`buildTileSprite` + `DRAG_PREVIEW_ALPHA`.

3. **One `ITool`/`ToolRegistry`.** The **freehand Fill Brush is a native `ITool`** (Feature A Phase A5), *not* a bespoke `EditorMode::FillBrush`+manager. It is the native tool that **validates the `ITool` seam (B1)** before any Lua `LuaTool` exists — exactly the "port one native tool" validation the seam needs. This deletes the duplicate tool plumbing the two original designs each proposed.

4. **One commit/scatter engine.** `PatternStamper` (refactored onto `PlacementBatch`, with `planInto` for sink capture), `FillRecipeRunner` (the weighted+noise+density+spacing+jitter scatter engine), and the seeded primitives (`rng/rngInt/weightedPick/noise2d/noise3d/objectAt`) are shared. A third-party Scatter Brush plugin scatters with the **same primitives**; it does not reimplement scatter from scratch, and the first-party "Fill Selection" is the canonical worked example that proves the API surface is sufficient.

**The one deliberate divergence:** the first-party fill builds a **fresh `MapScriptApi` per run** (like `EditorWidget::runScript`) and so **never needs `retarget`**; only the resident plugin VM does. This keeps the invasive `retarget`/persistent-VM/persistent-print work entirely inside Feature B, off the critical path of shipping area fill.

---

## 6. Sequencing & effort

Interleaved so value ships early and each phase de-risks the next. "Always compiled" phases work in the default `GECK_ENABLE_SCRIPTING=OFF` build.

| # | Phase | Depends on | Effort | Ships |
|---|---|---|---|---|
| 1 | **A0** plan-sink core: `EditArea`, `FillPlan`, two sink points, `PatternStamper::planInto`+`PlacementBatch`, seeded primitives | — | M | Headless, unit-tested core |
| 2 | **A1** Tier-1 `FillRecipe`+runner; CLI/MCP `fill` | A0 | M | First user value; **beats Dims**, no Qt/Lua |
| 3 | **A2** `FloorTileSet`+`autotileFloor` | A0 | M | Closes `autotile_floor` |
| 4 | **B0** `LuaSandboxHost` extraction (persistent-print repoint) | — (parallel) | S | No regression; unblocks resident VM |
| 5 | **B1** `ITool`+`ToolRegistry`+`PluginTool`+generic input+overlay field+MainWindow `addPlugin*` | B0 | L | The seam, validated by porting tile placement |
| 6 | **A3** `FillDialog` + debounced preview (uses overlay field) | A1, A2, B1 | M | **First end-user fill release** |
| 7 | **A5** freehand Fill Brush as native `ITool` | A1, B1 | S | Drag-to-paint; proves `ITool` for real |
| 8 | **Sandbox** interrupt+deadline+placement cap | B0 | M | Prereq for any GUI Tier-2 |
| 9 | **A4** Tier-2 Luau fills via dialog | A3, #8 | M | Custom scriptable fills |
| 10 | **B2** persistent VM + manifest + lifecycle + **`MapScriptApi::retarget`** + read-only `api` | B0, #8 | L | **Plugin MVP** (read-only, isolated) |
| 11 | **B3** `editor:` register + `map.write` + permission prompt + `storage` | B2 | L | Plugins add menus/buttons + undoable mutation |
| 12 | **B4/B5** `Gui.*` panels; `LuaTool` tools + events | B3 | L | Plugins add panels/tools |
| 13 | **B6** reference plugin, `fs.read`, hot-reload, scaffold, `plugin_api` | B5 | M | DX + headline example |

Effort: S ≈ days, M ≈ 1–2 weeks, L ≈ 3–4 weeks for one developer. The **fastest path to shipped value is rows 1–3 + 6** (area fill, end to end, default build, no plugin system at all). The plugin system is a strictly larger effort and should follow once the seam (row 5) is paid for by area fill.

**Risks & open questions (decisions taken inline above):**

- **`MapScriptApi::retarget` null-safety** is the single riskiest change — it touches a class shared by the generation runtime and CLI/MCP. Audit `_map == nullptr` across **every** method, keep the value ctor, land in B2 with focused tests. *Decision:* Feature A avoids it entirely by building fresh per run.
- **Watchdog ≠ host-call preemption.** Document and accept that a heavy bound call (`mapSceneryHistogram`, large `placeStamp`, mass sprite build) can stutter the loop; bound it instead with the placement/result cap and by keeping such calls out of `preview`/event hot paths. Off-thread plugin work (pure-data `buildSprites=false` over `CommandHost`) is a *later* possibility, out of v1 scope (GL + sprite building are UI-thread-only).
- **Preview cost on huge selections (40k hexes).** Bounded by debounce + placement cap + once-per-tweak plan; open whether to additionally clip preview to viewport-visible cells.
- **Two scatter implementations** (C++ `FillRecipeRunner` + Lua). *Decision:* the runner is the source of truth; "Edit as Script" lowers a recipe to Lua that calls the same primitives, so the algorithm is defined once.
- **Selection has no freeform region** (`SelectionState.h:69` is one `FloatRect` + discrete items). v1 area-fill and plugin area-fill are rect/discrete-set only; lasso/flood-fill/magic-wand is a separate future selection primitive.
- **Autotile across the selection boundary** masks against selection membership in MVP; blending into pre-existing terrain via `getFloorXY` is a deferred advanced flag.
- **`blob8` authoring burden** (47 entries). Support both `edges4` and `blob8`; ship `edges4` examples first.
- **Trust beyond hand-installed plugins.** v1 deliberately ships only the install-time prompt; SHA pinning, re-prompt-on-widening, quarantine, the version triple, `.gplug`, and ed25519 signing are deferred until ABI 1 is frozen and a distribution channel exists. Gate third-party *distribution* until then.

---

# Exit-grid rendering vs the engine — DONE (engine-faithful, two real rows)

**DONE — implemented.** Diagonal exit grids now place TWO rows of REAL, selectable objects, each bar
drawn once centered on its hex like the engine (`Object::applyExitGridOutwardOffset` + the display
doubling removed; a second row added in `ExitGridPlacementManager::classifySegment` via
`secondRowNeighbor`/`secondRowHex`, `kSecondRowSteps = 2` so the bars tile into a clean ~2× band).
Visible bar == selectable object, and the cardinal "hexes outside the texture" is fixed too. The notes
below record the engine truth and the before→after rationale for reference.

**Both rows are real exit grids → a 2-deep trigger zone (by design).** The inner/second row is
functionally inert in normal play (walking off-map the player exits at the first row it reaches and never
steps onto the second), but it IS a real exit in the saved data. A "non-exiting decorative exit-grid bar"
is **impossible**: the engine fires the map transition on any object whose PID is in the exit-grid range
(`0x05000010–17`) **unconditionally**, ignoring `exit_map`/flags (`fallout2-ce/object.cc:1425`); no
`exit_map` value means "no transition" (`0`/`-1`/`-2` are world/town map; the inactive-grid FID swap is
cosmetic, keyed on FID not PID). So two real selectable rows necessarily means two exits in data — chosen
(author's call) over a display-only second texture, which would keep one exit line but make the second
bar un-selectable.

**Engine truth** (verified against shipped maps `ncr1`, `redmtun`, `redwan1`, `artemple` via the new
`map render --exit-dots` overlay, and against `fallout2-ce/src/object.cc` ~4942): every exit grid is ONE
bar per object, drawn once, **centered on its hex** — `left = hexScreenX - frameWidth/2`, `bottom =
hexScreenY`; all eight exit-grid FRMs (`exitgrd1-8` / `ext2grd1-8`) have frame offsets `(0,0)`; there is
no exit-grid special case in the renderer. The trigger hex == the object's hex == the bar's bottom-center.
A real exit edge is a SINGLE line of objects (cardinal = straight, diagonal = a one-hex staircase) — never
a doubled / 2-wide band. It only *looks* wide because the bars (96×24 … 127×48) overlap across the ~16×12
hex step.

**Editor divergence** (the cause of the known quirks): `Object::applyExitGridOutwardOffset` slides the
bar off its hex (cardinals onto the bar's inner bbox edge), and `RenderingEngine::renderDiagonalExitGridBars`
/ `drawDoubledDiagonalBar*` draws diagonal bars TWICE as a doubled display band. Consequences: object
selection misses the visible bar and instead selects the on-hex object (which renders no bar of its own);
the trigger hex falls outside the texture; the live preview doesn't match the placement; diagonal pairs
misalign.

**Fix when prioritized**: delete `renderDiagonalExitGridBars` / `drawDoubledDiagonalBar*` (+ the
`isDiagonalExitGridObject` skip in `renderObjects`) and `Object::applyExitGridOutwardOffset`; let exit
grids draw once through the standard object path — the base `setHexPosition` already reproduces the engine
anchor (centered horizontally, bottom edge at the hex). Then visible bar == real object == selectable. If
a wider authoring visual is still wanted, do it as a translucent overlay tied to the object's actual hex —
never an offset or a second copy of the texture, which re-breaks selection.
