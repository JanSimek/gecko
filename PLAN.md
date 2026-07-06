# Improvement Backlog

## Architecture

The 13-work-package architecture roadmap is delivered.

**Evaluated and intentionally NOT pursued (churn > value):**
- **PRO/MAP serialization visitor.** SKIP. The clean visitor pattern used for the fixed
  common/header/trailer blocks does not carry to the type-specific tails (mixed field widths, a
  `uint8_t soundId`, optional-on-read/unconditional-on-write fields a symmetric visitor cannot
  express, plus union/subtype dispatch). It would add machinery over the highest-blast-radius code
  without removing the drift risk; the read->write->read round-trip test net already provides
  the safety.
- **`ProFieldFactory` extraction / spacing-token consolidation / `BasePanel` re-parent.** SKIP.
  Only ~15 LOC is genuinely shared (from a single non-inheriting widget) and it carries a materialId
  index-vs-value fidelity trap; the spacing sources hold *different* values (not dupes), so
  consolidation would pixel-shift layouts; `BasePanel` is a grid-browser base that would force stub
  overrides on the form-inspector panels.

**Deferred (real but modest, do opportunistically):**
- **PanelVisibilityController.** Extracting the panel-visibility snapshot/restore/persist state
  machine (~130 LOC + 3 members) would trim `MainWindow`, but it stays `QDockWidget`/`QAction`-bound
  with no pure testable core and bidirectional wiring back into `MainWindow`. Fold it into the next
  change that touches dock/panel behavior rather than doing it as standalone churn.

`src/util/` keeps only genuinely cross-cutting helpers — single-layer utilities now
live with their library (`src/resource/`, `src/ui/`).

> **Intentional non-goal (MAP save):** we deliberately do not recompute / auto-prune the
> per-elevation enable flags at save time (the engine does in `_map_save_file`) — our output is
> always internally consistent and engine-loadable, and pruning risks silently dropping an
> elevation the user wants. Revisit only if exact byte-parity with engine-saved maps becomes a
> requirement.

## Known limitations & follow-ups

1. **Undo coverage.** *Remaining:* the pre-existing elevation add/remove
   (`MapInfoPanel` checkboxes) is still a direct mutation; a cascading script-delete when an
   object is deleted; and the command-controller actions themselves aren't integration-tested
   (they need GameResources/Qt — a `qt_tests` follow-up).
2. **Newly created scripts get `local_var_count=0` / `offset=-1`** — exactly what the engine's
   `scriptAdd` writes; locals are allocated at runtime from the `.int`. The editor does not
   parse `.int` headers, and the local-var *count* lives in `scripts.lst`, not the binary.
3. **F11 spatial placement is dialog-driven** (enter tile/elevation/radius), not click-to-place
   with a live hex marker, radius overlay, and a new `EditorMode`. Existing spatial scripts also
   aren't visualized on the map or editable/deletable through the UI yet.
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
9. **The writable save target is positional, not chosen.** Saving a map (and map-name edits) writes
    into the *last folder* in Data Paths — `findWritableDataPath` walks the list from the end and takes
    the first real directory (archives skipped). That's implicit and order-dependent: reordering Data
    Paths silently changes where saves land, and nothing in the UI shows which location is the writable
    one. Map saves now default to that folder's `maps/` (Save / Save As), so the choice matters.
    **Add an explicit default-writable marker:** a button in Settings → Data Paths to designate the
    selected folder as the default writable location, shown with a badge in the list and persisted in
    `Settings`; saves then target that folder regardless of list order. If none is marked, keep the
    current last-folder fallback and hint the user to pick one. DAT archives can't be marked (read-only).
10. **In-app log panel — SHIPPED; completeness summary remaining.** The dockable **Log** panel
    (View › Log, hidden by default like the Script Console) now surfaces every `spdlog` record in
    the UI: `LogModelSink` (installed on the default logger at startup) feeds a bounded, thread-safe
    `LogModel`, shown in a filterable table (minimum-level combo, message text filter, copy
    selection/all, clear) with warning/error rows colour-coded (`src/ui/logging/`,
    `src/ui/panels/LogPanel.*`). Load-time warnings — unresolved `tiles.lst` entries, missing object
    sprites, map parse failures — are visible in-app instead of stderr. **Remaining follow-ups:**
    (a) the per-map **completeness summary** computed from the same resolve checks the loader and
    `resource missing` already run — unresolved tiles (by id + name), missing object sprites,
    unresolved scripts, plus a mount / data-path sanity line — as a structured section rather than
    prose warnings (MapLoader already collects the name lists; retain + surface them); (b)
    jump-to-source where a record carries a hex/object (needs structured records, not text);
    (c) when the SSL-editing output panel lands, make compiler output a category of this panel
    rather than a second dock. Editor UX only; changes no map/format data.

### Generation-side exit placement — current state & smarter follow-up

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

> Status: See **§11 (improvement backlog)** for the current
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

**Still open.**
- **Seamless multi-tile floor** — the headline visual gap. A C++ terrain synthesiser
  (image-quilting / patch-sampling from a reference grid first, WFC with learned adjacency later)
  exposed as `autotile_floor` — the principled form of the autotiling item (**P2 §4**). Naive
  per-cell weighted-random is *not* this; arbitrary FO2 tiles don't blend.
- **The MCP server** (the intelligence layer) with the high-level tool surface above — see the MCP
  section and **P4 §12**.
- Placement polish for the curated scripts/tools: footprint-aware, iso-diamond-masked placement;
  recurring multi-object clusters extracted as **prefabs** (place a rock formation as one unit).

### P1 — Ergonomics: make scripts human-writable

2. **Human coordinates.** *(still open.)* Addressing by linear index (`hex = row*200 + col`, tiles `row*100 + col`)
   is unintuitive, and the two grids differ (200×200 hexes vs 100×100 tiles). Add `(col, row)`
   variants of the common ops (`paintFloorXY`, `placeProtoXY`, `getFloorXY`) plus index↔(col,row)
   converters (`hexIndex(col,row)`/`tileIndex(col,row)` + inverses) and a **tile↔hex bridge** (a
   tile covers ~2×2 hexes) so "paint this tile and put a tree on it" is one step. Reuse the engine
   geometry (`hexgrid::offsetToCube`/`columnOf`/`rowOf`). Optionally add normalized `[0,1]`
   helpers (`hexAt(fx, fy)`) so "centre"/"scatter across the map" are grid-size-agnostic. Decide
   and **document the orientation** so `(col,row)` matches what the editor displays (Fallout's hex
   numbering has a right-to-left quirk).

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

### Open questions

- **`findProtos` scope/cost:** scan all proto types into one cached index, or per-type
  (`findScenery`/`findWall`) to bound the first-call scan? Lean: one cached index, documented.
- **Coordinate convention:** expose `(col,row)` as the engine's storage layout or remap to
  match the editor's on-screen/displayed coordinates? Pick one and document it.
- **Collision policy** when a generator targets an occupied hex/tile (overwrite / skip / error).
- **Multi-elevation generation** (run a script across all three elevations in one invocation).

---

# Map semantics & intelligence (analysis MCP roadmap)

> Status: Remaining: the Phase-3 semantic render overlay (capability 5). The goal is to let an agent reason about a map's **purpose**, its
> **critters' AI**, and its **scripts**.

**Guiding principle.** Don't hardcode classification heuristics ("N critters ⇒ a fight"). Surface
the engine's own semantic sources faithfully and **cross-referenced**, and let the model infer
purpose from the evidence — the same data a designer reads. This is MCP best practice (small
composable tools + one orchestrator, structured join-able output) and the repo's engine-fidelity
rule (no invented label tables). Every result should carry the join keys (`pid`↔proto.msg,
`script_id`↔`scripts.lst`↔`.ssl`↔`.msg`, `ai_packet`↔`ai.txt`) and cite which file each fact came
from. Keep all new readers Qt-free (vault/cli) so the server stays headless.

**Capabilities (each notes the reader it needs):**

2. **Critters + AI — `critters` tool + new `ai.txt` reader.** Per critter: name (`pro_critters.msg`),
   hex, **team (`group_id`)**, **AI packet resolved via `ai.txt`** (aggression, disposition,
   `run_away_mode`, `area_attack_mode`, `best_weapon`, `distance`, `secondary_freq`), equipped
   weapon + inventory, attached script. `ai.txt` is INI (one section per packet; section header =
   name; `packet_num` = the critter's `ai_packet`). **Phase 1.**
3. **Scripts + behavior — `describe_script` (Phase 2 — shipped).** An object's `map_scripts_pid`
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
5. **Semantic render overlay** (extends the schematic). **Still open:** *shading the
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

6. **Exit-grid connectivity graph — `map_graph`.**
   Follow-up: cross with `reachability` to flag one-way edges.

6b. **Worldmap layer — `world_map` (city.txt).** **Remaining:** the `worldmap.txt`
   `[Tile NN]` sub-tile grid (per-position terrain → terrain-weighted travel cost, geographic
   encounter placement) and `worldmap.msg` encounter descriptions (area/city labels are map.msg, now
   surfaced as `world_map`'s `displayName`).

7. **Corpus / world index — evidence, not a solver.** The evidence layer now largely exists: the
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

- **`data/worldmap.txt`** — **Follow-up (minor):** per-position *encounter* placement (the subtile
  encounter chances) is still unparsed — only the terrain field is kept.
- **`game/worldmap.msg`** *(follow-up — narrowed)* — area/city labels turned out to live in **map.msg**
  (`[1500 + areaIndex]`, now surfaced as `world_map`'s `displayName`) and terrain names are already
  readable in worldmap.txt, so the only names genuinely in worldmap.msg are the random-encounter
  **descriptions** (`worldmap.msg[3000 + 50*tableId + entryId]`, fallout2-ce worldmap.cc:3595) — and
  those are runtime-index-tied to the encounter table/entry ordering, not a small add. Just that
  encounter-description join remains.
- **`data/endgame.txt`** — **Follow-ups:** `enddeath.txt` death endings (in master.dat) and the narration subtitle text
  (`text/<lang>/cuts/<narrator>.txt`).
- **`data/party.txt`** (companions), **`holodisk.txt`**, **`karmavar.txt`** — lore/state, lower
  priority.

---

# Map loader panel — remaining enhancements

The visual map picker shipped (`MapBrowserDialog`, File → Browse Maps…: thumbnail grid + filter
+ preview, lazy render, in-memory cache).
- ~~**Persisted cross-session thumbnail cache**~~ — **DONE**: thumbnails persist as PNGs under the
  app data dir, keyed by source identity + mtime (DAT-contained maps invalidate with the DAT,
  loose maps with their own file), and the lazy renderer continues through off-screen cells so one
  open browse warms the whole library.

---

# Visualize spatial scripts on the map (investigate)

> Status: **SHIPPED (visualize + select + edit + delete).** `View › Show Spatial Scripts` renders each
> placed spatial script — the engine's green `msef001` marker at its centre hex plus a translucent
> hex-distance radius disc (`hexgrid::hexesWithinRadius`, matching the engine's `tileDistanceBetween`
> trigger test), filtered to the current elevation, as a viewport-culled overlay in
> `RenderingEngine::renderSpatialScripts`. A placed spatial script can now be **selected** (click its
> marker on the map, or its row in the Scripts panel — one shared selection keyed on the SID, the
> selected marker/disc highlighted amber), **edited** (double-click / context menu → pre-filled
> `SpatialScriptDialog`, in-place so the SID survives) and **deleted** (`Delete` / context menu), all
> undoable via `ScriptEditService::editSpatialScript` / `removeSpatialScript`. Known-limitation #3 is
> now fully closed.

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
  Known limitations #3 (live hex marker + radius preview while placing).

## Rough effort
S–M for read-only visualization (marker + radius overlay + visibility toggle), reusing the
existing overlay-layer and hex-grid plumbing. Editing/deleting via map interaction is the M
part and overlaps the spatial-placement `EditorMode` follow-up.

---

# Feature-gap audit vs the reference mappers

> **Done (2026-07-03)** — full parity catalogue in
> [`docs/feature-gap-audit.md`](docs/feature-gap-audit.md), from a read-only audit of the fallout2-ce
> built-in mapper (`src/mapper/`) and the legacy Dims mapper (`reference/F2_Mapper_Dims-master/`)
> against Gecko. Headline: **Gecko already leads both references** in header/PRO/global-var editing,
> undo/redo, Luau area-fill + generation, MCP analysis, combinable layers, and prefab patterns (which
> Dims left stubbed). Only ~8 genuine parity gaps remain; the old TODO.md "Legacy Dims missing
> features" list was mostly wrong-premised (corrected in the audit).

**Surfaced backlog (adopt, prioritized — match the engine's behaviour, don't invent):**
1. **`.edg` map-edge support** *(M)* — vault reader/writer for the big-endian `'EDGE'` v1/v2 format the
   engine authors + enforces (`fallout2-ce map_edge.cc` / `map_edge_setup.cc`), plus a setup overlay.
   The one real format Gecko can't round-trip.
2. ~~**Spatial-script visualization** *(S–M)*~~ — **DONE.** `View › Show Spatial Scripts` draws the
   engine's green `msef001` marker + a hex-distance radius disc (`RenderingEngine::renderSpatialScripts`
   + `hexgrid::hexesWithinRadius`). Placed spatial scripts can now be **selected, edited, and deleted**
   from both the map (marker click / double-click / `Delete`) and the Scripts panel, with a shared
   selection and undo — fully closing Known-limitation #3 / the "Visualize spatial scripts" section below.
3. ~~**Eyedropper — pick proto/tile from the map** *(S)* + **edge-scroll panning** *(S)*~~ — **DONE.**
   Eyedropper shipped (PR #99); edge-scroll shipped (cursor near a viewport edge auto-pans the view,
   ramped by depth into a 32px margin, gated off during right-drag pan, with a View-menu toggle
   persisted in `Settings`). Pure geometry in `viewport/EdgeScroll`, driven from `EditorWidget::update`.
4. **Minimap / overview** *(M)* — click-to-navigate + elevation switch, with a viewport rectangle
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

## What it would involve, by piece (rough effort)

- **Idle animations — Medium.** We already decode FRM frames (the PRO dialog previews them)
  and `Object::setDirection` sets a frame's texture rect; `TextureManager` stitches FRM frames
  into sheets. The core work is a preview clock that advances each animated object's frame
  index over time (honouring the FRM `fps` / `framesPerDirection`, looping idle anims), plus
  per-object animation state and only animating culled/on-screen objects for perf at map scale.
  The per-frame FRM offset handling this needs is already correct (the PRO-dialog animation preview
  was fixed to anchor frames by their `shiftX/shiftY`), so idle playback won't wobble. No new assets
  needed.
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

> **Status:** Feature A (area fill) SHIPPED as a Luau-driven "Fill Selection" (`EditArea`,
> `FillPlan`, `PlacementBatch`, `FillDialog`, `FillLibrary` + bundled `scripts/fills/*.luau`,
> CLI/MCP `fill`). The Tier-1 declarative recipe path (A1) and the `autotileFloor`/`FloorTileSet`
> primitive (A2) were built and then **removed by design** — the Luau fills cover the need. What
> remains: **A5** (freehand Fill Brush) and all of **Feature B** (the Luau plugin system), both
> gated behind the still-unbuilt `ITool`/`ToolRegistry` seam. The design below is kept as the
> reference write-up for that remaining work.

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

- **A0–A4 — SHIPPED** (Luau-driven Fill Selection). The plan/apply core (`EditArea`, `FillPlan`,
  `setPlanSink` + the two sink points, `PatternStamper::planInto`, `PlacementBatch`, seeded
  `rng/rngInt/objectAt/noise3d`), the interactive `FillDialog` GUI with debounced live preview, and
  gated Tier-2 Luau fills all landed. A1 (declarative `FillRecipe` recipes + CLI/MCP `fill`) and A2
  (`autotileFloor`/`FloorTileSet` seamless floor) were built then **removed by design** — the Luau
  fills replaced them, so the `autotile_floor` gap is intentionally closed by scripting, not a set primitive.
- **A5 — Freehand Fill Brush** *(pending)*. A native `ITool` on the registry introduced by Feature B (§5), not a new bespoke mode.

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

