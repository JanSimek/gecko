# Improvement Backlog

## Architecture backlog (remaining)

- **Reorganize the catch-all `src/util/` directory** (still ~40 mixed files spanning UI
  helpers, resource utilities and platform code). Group UI-specific helpers under `ui/`,
  resource utilities under `resources/`, and platform helpers separately to improve
  discoverability and reduce accidental dependencies.
- **`ResourceRepository` cache tests** — the DI precondition is met (`gecko_resource` and
  `gecko_core` are Qt-free, headless-linkable) and `test_frm_resolver.cpp` now covers the
  resolver, but there is still no dedicated cache hit/miss / type-mismatch test for the
  repository.

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

> Status: **Tier-2 scripting (procedural generation) is the remaining work** — the rest of
> this section. Tier-1 prefabs shipped (`src/pattern/`: format + JSON serializer, hex cube
> geometry, `PatternStamper`/`PatternBuilder`, undo-batching, and the pattern browser), which
> is the reusable foundation Tier-2's host API binds to.
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

1. **Host API skeleton** (`MapScriptApi`) over `ObjectCommandController` so any multi-edit is
   one undo step — the `beginBatch/endBatch` + `ScopedUndoBatch` foundation it sits on already
   exists. Unit-testable headless.
2. **Tier 2 Lua**: integrate Lua 5.4 + sol2 via FetchContent, sandboxed state, bind the
   *same* host API, add a script console/runner. Start with area-fill generators
   (desert+rocks, acid lake + shore border).
3. **Full-map generator** as a Lua entry point consuming a definition file, reusing
   `stampPattern` for set-pieces.

## 9. Open questions

- Multi-elevation prefabs (store/stamp across the 3 `ELEVATION_COUNT` slots?).
- Collision policy on stamp (overwrite vs skip vs error when target hexes are occupied).
- Scripts in patterns (object scripts via `programIndex`; spatial scripts) — deferred; see
  the script-model notes (programIndex is portable, SID/OID re-allocated at stamp).

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

> Status: idea / scoping. Expose the editor's map model as an MCP (Model Context Protocol)
> server so an AI assistant can analyze a map, describe it, add/move objects, change scripts,
> and (eventually) understand it visually and via its scripts/NPC dialogs.

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

- **Script & NPC-dialog analysis — Medium.** "Understand the scripts" pairs the **`.int`
  metadata reader** (procedure names, exported/imported procs, string table — see the SSL/INT
  notes; the `.int` has no description) with the **`.msg`** file of the same basename (we already
  have an `Msg` reader), which holds the NPC's dialogue/display lines. So `describe_script(index)`
  → proc list + the linked `.msg` text gives an AI the conversation tree and behaviour surface
  without running the game. Cross-reference `scripts.lst` (index→name) and the map's
  `MapScript`/object `sid` to answer "what does the NPC on this hex say/do?".
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

## Estimate

A read-only "describe/analyze" server is a **few days**; adding write tools is **another few
days** (~**1–2 weeks** for a solid read+write server), mostly tool-surface design + a JSON-RPC/CLI
shim, not format work. Script/dialog understanding reuses the `Msg` reader + the proposed `.int`
metadata reader. Visual analysis becomes a **Small–Medium** add-on once the Qt-free `gecko_render`
extraction is done (the renderer is already Qt-free; it just needs to move to a shared library +
an offscreen `sf::RenderTexture` wrapper). Start with `gecko-cli` + read tools, since that's
immediately useful and de-risks the rest.
