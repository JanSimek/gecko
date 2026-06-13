# GECK Map Editor TODO

## Legacy F2_Mapper_Dims Missing Features

### Minimap
Real-time minimap with click-to-navigate functionality showing entire map layout with viewport indicator.

### Brush System
Brush tools for painting objects and terrain features with customizable brush sizes and random object placement from predefined sets.

### Batch Property Editing
Extend existing multi-selection system with batch property editing for multiple selected objects via the PRO editor.

### Script Integration
Script assignment UI for object properties with dropdown, validation, and preview capabilities.

### Template System
Preset object configurations for complex placement patterns (furniture sets, defensive positions, etc.). Save and load object placement patterns.

### Advanced Search & Filter
Property-based filtering and complex query support for finding specific objects across the entire map.

### Progress & Status Improvements
Detailed progress dialog with task descriptions and completion estimates for long operations.

---

## Current TODO Items

### Usability

- [ ] Ctrl+C - copy object, Ctrl+V paste object
- [ ] Proto view
- [ ] Ctrl-toggle deselection: Ctrl+clicking an already-selected item, or Ctrl+dragging an
      area that covers already-selected items, should *remove* those items from the selection
      (toggle them off) instead of re-adding them. Today Ctrl only adds to the selection.

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

### Map info panel

- [ ] All fields should be editable and saved when the map is saved (exported)
- [ ] After the Qt migration, map script, map global variables and map scripts are not displayed in the panel

### Panels
- [ ] panels should be displayed above the SFML widget, so that when a panel is closed it doesn't cause redrawing of the SFML widget

### Presets
- [ ] TBD: paint a pattern of tiles
- [ ] TBD: paste a preset into the map, e.g. hut from Arroyo. Presets should be stored in a JSON/YAML/...

### Performance
- [ ] Optimize hex grid rendering to only draw visible hexes

### Code Quality / Architecture
- [ ] Split `ResourceManager` into narrower engine-facing services instead of a global singleton handling VFS, FRM decoding, message lookup, and texture caching
- [ ] Break up `EditorWidget` into smaller controllers/services for editing, input/tool orchestration, and rendering coordination
- [ ] Move application/workspace lifecycle out of `MainWindow` and into dedicated controllers or services
- [ ] Refactor map reading and writing into smaller section-level parsers/serializers and add round-trip coverage
- [ ] Finish the `ProEditorDialog` decomposition so type-specific behavior, previews, and persistence are not coordinated from one oversized dialog
- [ ] Replace placeholder inventory mutations with a shared model-backed inventory editing service used by all inventory UIs
- [ ] Remove remaining `const_cast` usage by fixing const-correctness and ownership boundaries
- [ ] Fix tile hit testing so object/tile selection is accurate near tile/object boundaries

### Known bugs

- [ ] placing lights - light.frm
- [ ] clicking on an object is not pixel perfect and sometimes a tile underneath or an object close by is selected instead
- [ ] scroll block drawing mode draws the rectangle in the isometric projection (diagonal) instead of screen projection
- [ ] placing lights - light.frm
- [ ] file browser panel has issues with resizing. It is sometimes impossible to change its size or requires several clicks
- [ ] make sure changing elevations works with the recent changes (probably not)
