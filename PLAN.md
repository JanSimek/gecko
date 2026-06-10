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

---

## Engine (fallout2-ce) MAP compatibility

Audited our `.map` read/write against the in-engine mapper (`map.cc`, `object.cc`,
`proto.cc`, `scripts.cc`, `src/mapper/`). The **format incompatibilities are fixed**
(`MapReader.cpp`, `MapWriter.cpp`, `Map.h`; regression tests #89/#90 in
`test_map_roundtrip.cpp`):

- **Object section is always 3 elevations.** The engine's `objectSaveAll` /
  `objectLoadAll` unconditionally loop `0..ELEVATION_COUNT` (=3). We now read/write
  exactly three per-elevation count blocks regardless of enabled elevations. (This was
  the critical bug: single/two-elevation maps — most F2 interiors — desynced on read and
  would not load back into the engine.)
- **Tiles keyed by true elevation.** Each elevation's tile block is gated on its flag bit
  (`Map::elevationIsPresent`, `0x2 << elev`; engine `_map_data_elev_flags = {2,4,8}`,
  `_square_load` / `_map_save_file`) and stored under its real elevation index.
- **MISC exit-grid trailing data only for exit-grid PIDs** (MISC ids 16–23, engine
  `isExitGridPid` = `0x5000010..0x5000017`, via `MapObject::isExitGridMarker()`). Ordinary
  MISC objects no longer emit 16 stray bytes.

**Intentional non-goal — save-time flag recompute.** The engine recomputes per-elevation
enable flags at save (`_map_save_file` lines 1389–1424), auto-pruning elevations that are
all-empty with no savable objects. We deliberately **do not** do this: our output is always
internally consistent and engine-loadable (tile blocks match `header.flags`, object blocks
are always three), and auto-pruning risks silently dropping an elevation the user intends to
keep. Revisit only if exact byte-parity with engine-saved maps becomes a requirement.

---

## Missing in-engine-mapper features (instance & map editing)

> **Status (implemented):** F10–F17 below are now implemented and build cleanly
> (96 base tests + the 2 MAP-compat regression tests pass). Summary:
> - **F12 flags** — `ObjectFlagsDialog`, undoable via `registerInstanceEdit`.
> - **F13 light** — `LightPropertiesDialog`, undoable.
> - **F14 critter** — `CritterPropertiesDialog` (AI packet numeric, team, HP/rad/poison),
>   undoable; inventory Add/Remove/quantity now mutate the model (persist on save).
> - **F15 scenery destination** — `SceneryDestinationDialog` (stairs/ladder built-tile,
>   elevator type/level), undoable.
> - **F16 interaction** — `InstancePropertiesDialog` locked/jammed routed to the correct
>   per-type field (doors → `walkthrough`/openFlags, containers → `unknown11`/data.flags),
>   undoable.
> - **F17 map ops** — `MapInfoPanel` "Map Operations": Clear Elevation Objects, Copy
>   Elevation (tiles + deep-cloned objects via `MapObject::cloneDeep`).
> - **F10 script attach** — `ScriptSelectorDialog` from scripts.lst; creates a `MapScript`
>   with engine-correct linkage (per-type SID, fresh object OID, program index,
>   `local_var_count=0`/`offset=-1` exactly as `scriptAdd`).
> - **F11 spatial script** — `SpatialScriptDialog` from `MapInfoPanel` creates a spatial
>   `MapScript` (built-tile + radius).
>
> **Known limitations (follow-ups):**
> - **Undo:** instance edits (F12/13/14-critter/15/16) are undoable; **inventory edits,
>   script attach/detach, spatial scripts, and map-wide ops are direct mutations without
>   undo** (matching `MapInfoPanel`'s existing elevation add/remove). Routing these through
>   `ObjectCommandController` is the main follow-up.
> - **AI packet / script program** are entered as raw engine numbers / scripts.lst names
>   (no invented label tables, per the engine-fidelity rule). Local variables for newly
>   attached scripts are left to the engine at runtime (as the mapper's own `scriptAdd` does).
> - **F11** is dialog-driven (enter tile/elevation/radius) rather than click-to-place with a
>   live hex marker + new `EditorMode`; that interactive flow is the remaining enhancement.
> - **F16 deferred extensions** (per-instance kill-type / custom name) remain out of scope —
>   they need new serialized fields and are not in the engine mapper.

These are the editor capabilities the fallout2-ce in-engine mapper has that Gecko lacks.
**Key finding: nearly all of the underlying data already round-trips through our `.map`
format** — `MapObject` already carries `flags`, `light_radius`/`light_intensity`,
`map_scripts_pid`/`script_id`, the critter block (`ai_packet`, `group_id`, `current_hp`…),
`inventory`, and the scenery fields (`elevhex`, `map`, `elevtype`, `elevlevel`,
`walkthrough`), and `MapScript` already carries `spatial_radius`/`script_oid`. So these are
predominantly **UI-only features with no format change** (the two exceptions are called out
explicitly under F16). Everything must route edits through `ObjectCommandController` for
undo/redo.

### Shared infrastructure (build first)

These are reused across most features; building them first avoids eight one-off buttons:

1. **Instance section in `SelectionPanel`.** A selection-driven, type-aware collapsible
   area (alongside the existing object info / "Edit PRO…" / "Edit Exit Grid…" controls)
   that hosts the per-type editors below. Hub for F10/F12/F13/F14/F15/F16.
2. **`ObjectFlagsDialog`** — checkbox grid over the engine's user-togglable flags (engine
   `regModFlagsDialog`, `mp_instance.cc`: Flat, NoBlock, MultiHex, the `Trans*` set,
   ShootThru `OBJECT_SHOOT_THRU`, LightThru `OBJECT_LIGHT_THRU`, WallTransEnd, item-only
   NoHighlight), with type-based visibility. Used by F12/F14/F16.
3. **`ScriptSelectorDialog`** — lists scripts from `scripts.lst`, filtered by the 5 script
   types (System/Spatial/Timer/Item/Critter). Used by F10/F11/F14.
4. **`LightPropertiesDialog`** (or inline spinboxes) — distance 0–8, intensity 0–100%.
   Used by F13 (and any instance editor showing light).
5. **`ObjectCommandController` edit commands** — a generic before/after instance-property
   command (mirroring the existing `registerExitGridEdit`/`registerObjectRotation`
   pattern) so each feature is a thin wrapper, plus a cascading-delete hook so deleting an
   object also drops its attached `MapScript` (avoids dangling scripts on re-save).

### F10 — Object-instance script attachment · effort M · no format change
- **What:** attach/detach a script (SID) to a placed object via the UI.
- **Engine:** `scripts.cc` `scriptAdd` (sid = `(type<<24)|id`), `scriptGetScript`,
  `scriptGetNewId`; `object.cc` `objectLoadAll` re-links `Object::sid → Script`;
  `mapper/mp_scrpt.cc` `scr_choose`.
- **Already have:** `MapObject.map_scripts_pid` (SID) + `script_id`, and the five
  `Map::MapFile.map_scripts[...]` sections — all read/written today; map scripts are
  currently read-only in the editor.
- **UI:** "Attach Script…" in the instance section → `ScriptSelectorDialog` (filtered by
  object type); creates/updates a `MapScript` entry in the right section, sets its
  `script_oid` to the object's OID, sets the object's `map_scripts_pid`. "Detach" clears it.
- **Undo:** `registerScriptAttachment` / `registerScriptDetachment` restoring both the
  `MapObject` SID and the `MapScript` entry. Wire cascading delete (shared infra #5).
- **Risks:** SID/OID allocation must not collide; orphaned scripts on object delete.

### F11 — Spatial (hex) script placement · effort M · no format change
- **What:** place a spatial (trigger-zone) script at a hex with a radius.
- **Engine:** `mapper/mp_scrpt.cc` `map_scr_add_spatial` / `map_scr_remove_spatial` /
  `map_scr_toggle_hexes`; `Script.sp.built_tile` + `sp.radius` (0–50);
  `obj_types.h` `builtTileCreate`/`builtTileGetTile`/`builtTileGetElevation`
  (`BUILT_TILE_ELEVATION_SHIFT = 29`, tile mask `0x3FFFFFF`).
- **Already have:** `MapScript.spatial_radius` and `script_oid` (the packed tile+elevation)
  already round-trip; the SPATIAL section exists.
- **UI:** new `EditorMode::PlaceSpatialScript` (mirror `ExitGridPlacementManager`) + a
  toolbar action; on hex click, `SpatialScriptDialog` (script pick + radius). Show a marker
  object at the hex (engine uses an INTERFACE-type marker). **Use hex coordinates
  (0–39999), not tile coordinates** — pack via the engine's built-tile encoding exactly.
- **Undo:** placement / deletion / radius-change commands; treat placement + marker as one
  command.

### F12 — Object-instance flag editing · effort M · no format change
- **What:** toggle per-instance flags (Flat, NoBlock, MultiHex, ShootThru, LightThru,
  `Trans*`, WallTransEnd, item NoHighlight).
- **Engine:** `obj_types.h` `enum ObjectFlags` (exact bit values, `0x01..0x80000000`);
  `mapper/mp_instance.cc` `regModFlagsDialog` / `regModInstFlags` (note the `OBJECT_FLAT`
  special case triggers a visual `_obj_toggle_flat`).
- **Already have:** `MapObject.flags` round-trips; our `Pro::ObjectFlags` mirror the engine.
- **UI:** `ObjectFlagsDialog` (shared infra #2) from an "Edit Flags…" button; type-based
  visibility matching the engine.
- **Undo:** `registerObjectFlagsChange` (before/after); toggling Flat must refresh the
  sprite/outline.

### F13 — Per-instance light editing · effort M · no format change
- **What:** edit light distance (0–8 hex) and intensity (0–100%) per object.
- **Engine:** `object.cc` `objectSetLight` (clamps distance ≤ 8, intensity ≤ 65536);
  `mp_instance.cc` constants `kInstMaxLightDistance = 8`, `kInstMaxLightPct = 100`,
  `kInstLightScale = 0x10000`. Display intensity as `intensity * 100 / 0x10000` %.
- **Already have:** `MapObject.light_radius` (= engine `lightDistance`) and
  `light_intensity` (raw 0–65536) round-trip; `isLightSource()` helper exists.
- **UI:** `LightPropertiesDialog` (shared infra #4) from an "Edit Light…" button, shown for
  light-source objects.
- **Undo:** `registerLightEdit` (before/after) + light/sprite refresh.

### F14 — Critter-instance editing · effort M · no format change
- **What:** edit AI packet, team/group, HP/rad/poison, and inventory add/remove.
- **Engine:** `mapper/mp_instance.cc` `protoInstCritterEdit`, `protoInstAddToInven`,
  `protoInstChooseItemsForInven*`; `combat_ai.cc` `combat_ai_name(packet)`;
  `item.cc` `itemAdd`/`itemRemove`.
- **Already have:** all critter fields + the `inventory` vector round-trip;
  `SelectionPanel` already renders the inventory tree and has the **TODO-stub** handlers
  `onAddInventoryClicked` / `onRemoveInventoryClicked` (the inventory Add/Remove gap noted
  in CLAUDE.md).
- **UI:** critter-only "Combat Properties" group (AI-packet picker via `combat_ai_name`,
  team, HP/rad/poison spinboxes) + implement the inventory add (item PID picker) / remove /
  quantity handlers.
- **Undo:** per-field commands; inventory add/remove/qty as commands (mind lambda-captured
  `unique_ptr` ownership).

### F15 — Scenery transition-destination editing · effort M · no format change
- **What:** edit destination Tile/Elevation/Map for stairs and ladders, and Type/Level for
  elevators.
- **Engine:** `proto.cc` scenery `objectDataRead`/`objectDataWrite` (stairs:
  `destinationBuiltTile` + `destinationMap`; ladders: built-tile / map, version-dependent
  order; elevator: type + level); `mapper/mp_instance.cc` `protoInstSceneryEdit`;
  built-tile helpers as in F11.
- **Already have:** `MapObject.elevhex`, `map`, `elevtype`, `elevlevel` round-trip — with
  the **stairs-vs-ladder field-order difference already handled** in `MapReader`/`MapWriter`.
- **UI:** `SceneryDestinationDialog` from an "Edit Destination…" button; spinboxes for
  dest tile (0–39999), dest elevation (0–2), dest map; swap to Type/Level for elevators.
  Decode/encode `elevhex` with the engine's built-tile packing (validate elevation 0–2 even
  though 3 bits allow 0–7).
- **Undo:** `registerDestinationEdit` (before/after `elevhex`/`map` or `elevtype`/`elevlevel`).

### F16 — Door/interaction state · effort M (kill-type/name deferred)
- **In scope, no format change:** **door walkthrough** (open/closed — `MapObject.walkthrough`
  already serialized) and **locked/jammed** flags (`OBJECT_LOCKED 0x02000000`,
  `OBJECT_JAMMED 0x04000000` in `MapObject.flags`). Engine: `mp_instance.cc` instance flag
  toggles (`'l'` key toggles locked), `proto_instance.h`
  `objectLock`/`objectUnlock`/`objectJamLock`. This resolves the door-state overlap with
  F15: **F15 = transition destinations, F16 = interaction state.**
- **UI:** door/container interaction controls (walkthrough open/closed; locked/jammed
  checkboxes) in the instance section, disabled for types that don't support them.
- **Deferred — beyond engine-mapper parity (require decisions / format work):**
  - **Per-instance critter kill-type override.** *Not* exposed in the engine mapper's
    instance editor (kill type is proto-level, `proto_types.h` `KILL_TYPE_*`,
    `KILL_TYPE_COUNT`). Adding it is a **Gecko extension that needs a new serialized
    `MapObject` field** (with a `-1 = use proto` sentinel and a compatibility story). Defer.
  - **Custom per-instance display name.** Needs a **format extension** (the vanilla
    fixed-size object record has no name slot). Defer pending a storage decision.
  - **Can-use** is proto-level (`PROTO_EXT_FLAG_CAN_USE`), not instance-overridable — out of
    scope.

### F17 — Map-wide operations · effort M (Shift-Map interactive part deferred) · no format change
- **What:** Clear Level, Copy Elevation, Shift/Move Map, and editable map-header fields
  (darkness, entering position/elevation/orientation).
- **Engine:** `mapper/map_func.cc` `map_clear_elevation`, `mapper_copy_map_elev`,
  `mapper_shift_map` (+ `mapper_shift_map_once` / `mapper_shift_map_elev`); header fields in
  `map.cc`.
- **Already have:** all `MapHeader` fields round-trip; `MapInfoPanel::onElevationCheckboxChanged`
  already clears an elevation's tiles/objects (but **without undo** — factor this into a
  proper command). Tiles/objects are stored per-elevation in `MapFile`.
- **UI:** a "Map Operations" group in `MapInfoPanel` — Clear Level (confirm dialog),
  Copy Elevation (N→M), editable Darkness; make the existing checkbox path go through undo.
  **Shift/Move Map**: implement the simple numeric-offset form first (interactive
  arrow-key shift deferred). Shift/Copy must rewrite spatial-script built-tiles and destroy
  objects that move off-map (0..39999).
- **Undo:** batch bulk deletions/moves into a single command (don't register per object);
  custom header-change commands via `pushCommand`.

### Suggested sequencing

1. **Shared infra** (SelectionPanel instance section, `ObjectFlagsDialog`,
   `LightPropertiesDialog`, `ScriptSelectorDialog`, generic instance-edit + cascading-delete
   commands).
2. **Core instance editing — highest value:** F12 (flags), F13 (light), F10 (scripts),
   F14 (critter + finish the inventory TODO stubs).
3. **Scenery / spatial:** F15 (destinations), F11 (spatial scripts; needs the new EditorMode).
4. **Lower priority:** F16 door/interaction state (defer kill-type/name), F17 map-wide ops
   (defer interactive Shift-Map).
