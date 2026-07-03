# GECK Map Editor TODO

## Legacy F2_Mapper_Dims Missing Features

Audited and folded into **[docs/feature-gap-audit.md](docs/feature-gap-audit.md)** (2026-07-03).
Most of the old items here were wrong-premised: batch-property-editing and advanced-search are
features *neither* reference mapper actually has, and script-assignment + a template/prefab system
Gecko already ships. The one genuine Dims parity gap is the **minimap**. See the audit for the full
corrected list and the prioritized adopt backlog.

---

## Current TODO Items

### Usability

- [ ] Ctrl+C - copy object, Ctrl+V paste object

### Hex palette

- [ ] mark special hexes - unwalkable, wall, wall s.t. (shoot through)

### Execute map

- [ ] revert the change when the game closes
- [ ] (future maybe) Steam launch support — removed. Only the executable/GOG install type is
      supported now. Launching FO2 via `steam://run/<appid>` worked, but playtesting the *edited*
      map did not: the Steam path couldn't locate the install dir to copy the map into and patch
      `ddraw.ini`. Reviving it means resolving the Steam install path (parse
      `steamapps/libraryfolders.vdf` + `appmanifest_38410.acf`), then reusing the executable
      path's map-copy + ddraw.ini step. (Detection of a Steam-*installed* copy as a regular
      executable install is still supported.)

### Panels
- [ ] panels should be displayed above the SFML widget, so that when a panel is closed it doesn't cause redrawing of the SFML widget

### Presets
- [ ] TBD: paint a pattern of tiles

### Code Quality / Architecture
- [ ] Break up `EditorWidget` into smaller controllers/services for editing, input/tool orchestration, and rendering coordination
- [ ] Move application/workspace lifecycle out of `MainWindow` and into dedicated controllers or services
- [ ] Refactor map reading and writing into smaller section-level parsers/serializers and add round-trip coverage
- [ ] Replace placeholder inventory mutations with a shared model-backed inventory editing service used by all inventory UIs
- [ ] Remove remaining `const_cast` usage by fixing const-correctness and ownership boundaries

### Known bugs

- [ ] placing lights - light.frm
- [ ] scroll block drawing mode draws the rectangle in the isometric projection (diagonal) instead of screen projection
- [ ] file browser panel has issues with resizing. It is sometimes impossible to change its size or requires several clicks
- [ ] make sure changing elevations works with the recent changes (probably not)
