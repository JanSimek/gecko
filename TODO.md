# GECK Map Editor TODO

## Known bugs

- [ ] on opening a map, check that we have all the necessary files and if not show an error message box instead of crashing, e.g. critters.lst
- [ ] file browser panel has issues with resizing. It is sometimes impossible to change its size or requires several clicks 
- [ ] make sure changing elevations works with the recent changes (probably not)
- [ ] handle non-existing elevations / adding new elevations

## Hex palette

- [ ] mark special hexes - unwalkable, wall, wall s.t. (shoot through)

## Draw scroll blockers

- [ ] user should be able to draw a rectangle on the map and the borders of the rectangle will be filled with scrollblocker hexes

## Map info panel

- [ ] All fields should be editable and saved when the map is saved (exported)
- [ ] Default player orientation should be a select box and instead of integers have string values, e.g. north-west
- [ ] After the Qt migration, map script, map global variables and map scripts are not displayed in the panel
- [ ] Default player position should be selectable on the map, e.g. there is a little button with a crosshair icon and when user clicks it he will be able to click on a tile that will be selected - also the tile should have a marker like we have for scroll blockers?

## Panels
- [ ] panels should be displayed above the SFML widget, so that when a panel is closed it doesn't cause redrawing of the SFML widget
- [ ] There should be a View -> Panels -> ... which would control what panels are visible. If user closes a panel it would be unched in the View submenu, so then if user would click on it the panel would appear again

## Object palette panel

- [ ] Create a new panel where user can browse available objects and place them on the map

## UI

- [ ] icons for toolbar buttons - use IconFontCppHeaders with Lucide / FontAwesome

## Configuration file

- [ ] choose a format - yaml, json, Qt native configuration? Prefer not to add a new dependency

## Configuration window

- [ ] when no configuration exists on application start, open the window automatically
- [ ] add Preferences menu that opens this window
- [ ] let user add data paths that will be added to the VFS
- [ ] detect common Fallout 2 installation paths, paths from Window registry (GOG) and Steam installation and offer to add them into the available data paths

## Hex Highlighting System Improvements

### Performance
- [ ] Optimize hex grid rendering to only draw visible hexes
- [ ] Implement viewport culling for large maps

### Code Quality
- [ ] Refactor hex rendering into dedicated HexRenderer class
- [ ] Improve separation of concerns between HexagonGrid and EditorWidget

## Presets
- [ ] TBD: paint a pattern of tiles
- [ ] TBD: paste a preset into the map, e.g. hut from Arroyo. Presets should be stored in a JSON/YAML/...