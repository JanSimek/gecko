# Feature-gap audit vs the reference mappers

*Produced 2026-07-03. Read-only audit of two reference editors against Gecko's current feature
surface. Goal (per PLAN.md): catalogue the authoring features the reference mappers have that Gecko
lacks, and turn the gap into a prioritized backlog — adopting the engine's behavior, not inventing.*

## Sources

| Reference | Path | What it is |
|---|---|---|
| **fallout2-ce mapper** | `/Users/jansimek/Development/fallout2-ce/src/mapper/` | The engine's own authoring tool — the **source of truth** for behavior. Note: a large part of its menu surface is **stubbed / no-op in CE** (spray brushes, proto-editor dialog, text-map I/O, bookmark & category windows, proto-rebuild/librarian ops). |
| **Legacy Dims mapper** | `reference/F2_Mapper_Dims-master/Mapper/` | The community Borland C++Builder mapper Gecko is modelled on. |
| **Gecko** | `src/` (this repo) | The editor under audit. |

---

## TL;DR

- **Gecko already leads both references** in header/PRO/global-var editing, undo/redo, procedural
  authoring (Luau area-fill + generation), analysis tooling (MCP/CLI), and combinable selection
  layers. Several of these are things the references **stubbed and never built**.
- There are only **~8 genuine parity gaps**. The top three worth adopting: **`.edg` map-edge
  support**, **spatial-script visualization**, and a **minimap/overview**.
- The existing TODO.md "Legacy F2_Mapper_Dims Missing Features" list is **mostly wrong-premised**:
  batch-property-editing and advanced-search are features *neither* reference has (net-new ideas, not
  parity gaps); script-assignment and a template/prefab system Gecko **already ships**. Only the
  **minimap** is a real Dims parity gap. Corrected table in §4.

---

## 1. Where Gecko already leads (no action — parity already exceeded)

| Feature | fallout2-ce | Dims | Gecko |
|---|---|---|---|
| In-app **map header / info editor** (name, darkness, script id, start pos, flags) | **stub** (`kInfo` is a no-op, `mapper.cc:1571`) | none (mapname derived from filename) | **`MapInfoPanel`** — full editor |
| **PRO (.pro) editing UI** | **stub** (`protoEdit` returns -1, TODO) | none (`.pro` read-only) | **`ProEditorDialog`** (tabbed, writes with `.bak`) |
| **Global-var (.gam) editing** | load-only on play | LVar/GVar counts only | **`MapInfoPanel`** vars tree, lossless `.gam` round-trip |
| **Undo / redo** | **none** (destructive) | **last-delete only** (`undo.dat`) | **`UndoStack`**, labeled, batched |
| **Procedural authoring** | Create/Use Pattern **stubbed** | random-obj brush: **uniform, 1 obj/click, no weight/density** | **Luau area-fill** (weighted/noise/density) + full map `generate` |
| **Prefab / pattern system** | region copy = de-facto stamp | **empty stub** (`objtempl.h`) | **`PatternBuilder` + StampPattern** + `extract_pattern` |
| **Analysis / intelligence** | none | read-only "Items map info" dump | **MCP/CLI**: `analyze`, `describe_map`, `reachability`, `map_graph`, `world_map`, `quests`, … |
| **Combinable selection layers** | single type | single type | **Floor/Roof/Objects** checkboxes, additive |
| **Exit-grid authoring** | mark by numeric id | instance fields only | **named destination**, edge-line draw tool, directional art |

---

## 2. Genuine parity gaps (prioritized backlog)

Feature the reference actually has (not a stub) and Gecko lacks. Effort: S ≈ days, M ≈ 1–2 weeks.

### Priority 1 — real, engine-authored, already-wanted

| # | Feature | In reference | Gecko today | Effort | Notes |
|---|---|---|---|---|---|
| 1 | **`.edg` map-edge support** | fallout2-ce `map_edge_setup.cc` — full two-tier authoring UI (Hi-Res tile-rect zones + Angled square edges w/ per-side clip modes), overlay, and a big-endian `'EDGE'` v1/v2 file written beside the `.map` (`map_edge.cc:309/405`), enforced by the `edg_support` gate | **LACKS** — no `.edg` read/write/UI anywhere | **M** | The engine *authors and enforces* these; a real format Gecko can't round-trip. Adopt: reader/writer in vault + a setup overlay. Match the CE format exactly (engine-fidelity). |
| 2 | **Spatial-script visualization** | fallout2-ce `'h'` toggle draws interface-art markers at each spatial-script tile (`mp_scrpt.cc:110`) | **DONE (read-only)** — `View › Show Spatial Scripts` draws the engine's green `msef001` marker at each script's centre hex plus a translucent hex-distance radius disc, current-elevation-filtered; `RenderingEngine::renderSpatialScripts` + `hexgrid::hexesWithinRadius`. Select/edit/delete via the map is the remaining stretch (Known-limitation #3). | **S–M** | Shipped. |

### Priority 2 — strong QoL, both references have it

| # | Feature | In reference | Gecko today | Effort | Notes |
|---|---|---|---|---|---|
| 3 | **Minimap / overview** with click-to-navigate + elevation switch | Dims `DrawMiniMap` + `imgMiniMapMouseDown` (click re-centers & picks elevation); fallout2-ce `automapShow` (TAB) | **LACKS** — only static per-map thumbnails in `MapBrowserDialog` | **M** | The one *real* Dims parity gap. Dims' locator is a cursor sprite, not a scaled viewport rect — a viewport rectangle would improve on it. |
| 4 | **Eyedropper — pick proto/tile from the map** | fallout2-ce `'p'` jumps the toolbar to the proto/tile under the cursor (`mapperPickObject`/`mapperPickTile`) | **DONE** (PR #99) — `P` picks the object/tile under the cursor into the matching palette | **S** | Shipped. |
| 5 | **Edge-scroll panning** | both (cursor at iso edge scrolls one hex; Dims arrow-scroll + hand-pan) | **DONE** — cursor near a viewport edge auto-pans (ramped by depth into a 32px margin), suppressed during right-drag pan, View-menu toggle persisted in `Settings`; `viewport/EdgeScroll` + `EditorWidget::update` + `ViewportController::panBy` | **S** | Shipped. |

### Priority 3 — defer (substitute exists or niche)

| # | Feature | In reference | Gecko today | Effort | Notes |
|---|---|---|---|---|---|
| 6 | **Object clipboard copy/paste** | fallout2-ce region copy (filtered / all-types, w/ stackable auto-merge) | **LACKS** clipboard — substitute is *Save Selection as Pattern* → *Stamp* | S–M | Defer: pattern-stamp already covers "duplicate a region". A true Ctrl-C/Ctrl-V is nicer but not blocking. |
| 7 | **Shift / move / copy entire elevation by hex delta** | fallout2-ce "Move Map", "Move/Copy Map Elev" (`map_func.cc:1006/1151/1244`) | **PARTIAL** — Gecko has *clear* + *copy* elevation; no whole-map hex shift | M | Niche. Mind the CE hex-shift quirk (a 1-col user shift = 2 hex cols; spatials translate by `400*dy − 2*dx`). |
| 8 | **Set object to a specific rotation** | fallout2-ce Ctrl+Up = rot 0, Ctrl+Down = rot 3 | **PARTIAL** — Gecko's `R` cycles rotation | S | Minor: add absolute-rotation setters. |

---

## 3. Intentional non-goals / not-really-gaps

Present in a reference but **deliberately not adopted** — with the reason.

- **Target/exec `.tgt` + `target.dat` system** (fallout2-ce) — a CE-internal playtest *protection* mechanism
  that flips maps into non-saveable "TESTING" state. Dev-workflow plumbing, not a map-authoring feature.
- **Per-map toolbar `.cfg` sidecar** (fallout2-ce) — persists 6 toolbar scroll offsets + digit bookmarks
  next to each `.map`. Gecko's searchable palettes + panels supersede it.
- **Text-map import/export** (fallout2-ce) — the menu items are **disabled/stubbed even in CE**.
- **Proto rebuild / librarian / "art⇒protos" ops** (fallout2-ce) — **stubs in CE**; batch content-pipeline
  ops, out of scope for a map editor.
- **`use_art_not_protos` raw-art mode** (fallout2-ce) — a debug authoring mode; not user-facing.
- **Batch property editing** — a TODO.md aspiration, but **neither reference has it** (Dims explicitly
  disables property controls on multi-select, `properts.cpp:128`). A *net-new* Gecko idea, not a parity
  gap — keep in the general backlog if wanted, but it is not driven by this audit.
- **Advanced property-based search/filter across the map** — likewise **neither reference has it**.
  Net-new idea, not parity. (Gecko already has per-palette + file-browser + scripts-panel filters.)
- **Global ambient-light nudge** (`[` / `]`, fallout2-ce) — that is a runtime *preview* aid; Gecko already
  edits the map's **darkness** in `MapInfoPanel`, which is the authored equivalent. Consider covered.

---

## 4. Corrected "Legacy F2_Mapper_Dims Missing Features" (from TODO.md)

The TODO list asserted Dims has these and Gecko lacks them. Verified against the Dims source:

| TODO claim | Reality (evidence) | Verdict for Gecko |
|---|---|---|
| **Minimap** (real-time, click-to-navigate, viewport indicator) | **CONFIRMED** in Dims (`DrawMiniMap:234`, `imgMiniMapMouseDown:1301`); locator is a sprite, not a viewport rect | **Real gap → adopt** (§2 #3) |
| **Brush system** (custom sizes + random from sets) | **PARTIAL** — Dims' brush is uniform, 1 obj/click, **no size/weight/density** (`rndobj.cpp:29`). Gecko's Luau fill already exceeds it | **Gecko ahead**; only "footprint/brush size" remains → that's the planned freehand Fill Brush (area-fill A5) |
| **Batch property editing** (multi-select) | **REFUTED** — Dims disables property edits on multi-select (`properts.cpp:128`) | **Not a parity gap** — net-new idea if desired |
| **Script-assignment UI** (dropdown/preview) | **CONFIRMED** in Dims (`cbScript` + `lblScriptDesc`) | **Gecko already has it** (`ScriptSelectorDialog` + attach/detach) |
| **Advanced search & filter** (property query) | **REFUTED** — Dims has only category tabs + a read-only items dump | **Not a parity gap** — net-new idea if desired |
| **Template system** | **REFUTED** — `objtempl.h` is an empty stub | **Gecko ahead** — patterns/prefabs delivered what Dims stubbed |
| **Progress & status dialogs** (task descriptions) | **CONFIRMED** in Dims (`pbar.cpp` caption+status) | Minor; Gecko has a loading widget. Low priority |

**Net:** of seven TODO items, only the **minimap** is a genuine Dims parity gap. Two are net-new
ideas neither reference has, two Gecko already ships, one Gecko exceeds, one is minor. This list should
be retired from TODO.md in favor of this audit.

---

## Recommended sequencing

1. **`.edg` map-edge support** (§2 #1) — the one true format Gecko can't round-trip; engine-authored.
2. ~~**Spatial-script visualization** (§2 #2)~~ — **DONE (read-only overlay).** Map select/edit/delete remains.
3. ~~**Eyedropper pick-from-map** (§2 #4) + **edge-scroll** (§2 #5)~~ — **DONE** (eyedropper PR #99; edge-scroll shipped).
4. **Minimap/overview** (§2 #3) — larger, high-visibility.
5. Defer #6–#8 unless requested.
