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

### Hex palette

- [ ] mark special hexes - unwalkable, wall, wall s.t. (shoot through)

### Execute map

- [ ] revert the change when the game closes

### Map info panel

- [ ] All fields should be editable and saved when the map is saved (exported)
- [ ] After the Qt migration, map script, map global variables and map scripts are not displayed in the panel

### Panels
- [ ] panels should be displayed above the SFML widget, so that when a panel is closed it doesn't cause redrawing of the SFML widget

### Code Quality
- [ ] Refactor hex rendering into dedicated HexRenderer class
- [ ] Improve separation of concerns between HexagonGrid and EditorWidget

### Review Follow-ups
- [ ] Extract shared inventory item preview/name/icon/add-remove logic used across inventory UIs
- [ ] Make `MainWindow` action and dock setup data-driven to reduce duplicated boilerplate
- [ ] Add Qt/UI tests around `Pro*Widget`, inventory UI flows, and panel toggles before larger refactors

### Presets
- [ ] TBD: paint a pattern of tiles
- [ ] TBD: paste a preset into the map, e.g. hut from Arroyo. Presets should be stored in a JSON/YAML/...

### Performance
- [ ] Optimize hex grid rendering to only draw visible hexes

### Known bugs

- [ ] scroll block drawing mode draws the rectangle in the isometric projection (diagonal) instead of screen projection
- [ ] placing lights - light.frm
- [ ] file browser panel has issues with resizing. It is sometimes impossible to change its size or requires several clicks
- [ ] make sure changing elevations works with the recent changes (probably not)
