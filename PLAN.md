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
- **Tile-adjacency analysis.** `analyze --json` now carries `adjacency` per map and aggregated: the
  floor-tile *borders* (which tile sits next to which different tile, and how often). Since the
  Fallout engine has no autotiling — mappers place edge tiles by hand — this is the empirical data
  an agent curates a transition set from before generating **seamless** terrain (the §11 P2 item).
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
