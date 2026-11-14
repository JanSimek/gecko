# Stamps

Ready-made gecko stamps (Edit → Stamp Pattern) cut from the maps of Fallout 1, Fallout 2 and four total conversions: whole buildings, graveyards, junkyards and ruins. There are no critters and no loose items.

| Folder | Stamps | Stamp ids are | Open this game in gecko |
|---|---|---|---|
| `Fallout 2 Stamps` | 424 | Fallout 2's (Fallout 1 pieces converted by matching art) | Fallout 2, with or without the Restoration Project |
| `Fallout 1 Stamps` | 124 | Fallout 1's own | Fallout 1 (this fork opens Fallout 1 maps and data) |
| `Sonora Stamps` | 437 | Fallout: Sonora's own | Fallout: Sonora |
| `Nevada Stamps` | 310 | Fallout: Nevada's own | Fallout: Nevada |
| `Resurrection Stamps` | 193 | Fallout 1.5: Resurrection's own | Fallout 1.5: Resurrection |
| `Olympus Stamps` | 79 | Olympus 2207's own | Olympus 2207 |
| `HogLord Props` | 21 | Fallout 2's | Fallout 2, with or without the Restoration Project. My own compositions: barrels, car junk, a reactor, relay walls, tents, a trash house, a warehouse, adobe huts, a clinic, crops, a farm, a pen, ruins, a tavern and a well |

A stamp stores the game's proto and art ids, not images. It only draws correctly in a gecko session with the game data listed above; stamped into another game, the same ids point at different pieces.

## Installing

Copy the folders you want into gecko's pattern library:

- Windows: `%LOCALAPPDATA%\gecko\gecko\patterns\`
- Linux: `~/.config/gecko/patterns/`
- macOS: `~/Library/Preferences/gecko/patterns/`

The library reads subfolders, so each set keeps its folders in the stamp browser.

## Layout

`<Game> Stamps/<folder>/<Game> <MAP> <n> <floor>.json`

**Buildings** are sorted by what they are built from: Adobe, Brick & Concrete, Caves & Rock, Corrugated Metal, Military & Metal, Stone & Plaster, Tents, Vault, and Wood & Scrap. The folder is decided by the wall art the building really uses, and every building was checked by eye. A building comes with its walls, doors, floor and roof tiles, furniture and containers. It is kept only if its walls go all the way round its roof, with gaps no longer than a doorway, and its roof still covers the ground inside its walls. The floor in the name is the map elevation it came from: to build up, stamp a 2nd or 3rd floor stamp on that elevation, over the 1st floor stamp with the same map name and number. Some maps use an upper elevation as a separate area, so not every "2nd Floor" stamp is an upper storey.

**Graveyards** are clusters of graves, headstones, crosses and coffins, with the fence round them when there is one, and the gates, posts, benches and dead trees inside it.

**Junkyards** are clusters of junk piles, tires, wrecked cars and crates, with their fence if they have one.

**Ruins** are broken walls: corners, L and U shapes, buildings with walls missing, with the debris and rubble beside them. Intact roofless buildings, tents, vault and cave walls, railings and compound fences are left out, and so are underground maps.

Graveyards, junkyards and ruins are objects only, with no ground tiles, so they drop onto whatever ground the map already has. Identical stamps that recur across maps appear once. Hidden script markers and "UNUSED ART" placeholder pieces are left out.

Every stamp carries a `credit` line and a `source` block with its game, map and elevation. Gecko ignores both.

## Credits

The layouts are the work of the people who made these games and mods. Please keep these credits with the stamps.

- **Fallout (1997):** Interplay Productions / Black Isle Studios.
- **Fallout 2 (1998):** Black Isle Studios / Interplay. The maps are taken as they stand in the Restoration Project by killap, now maintained as RPU by BGforge.
- **Fallout: Sonora (2020–2021):** Alexander "BLACK DESIGNER" Poshelyuzhin and Alexander "Red888guns" Berezin, with Osman "Wolfram" Adzhiusmanov, Grigory Sokolov, Denis "MASTER" Voloshin, Dmitry Kuznetsov, Foxx and Lexx.
- **Fallout: Nevada (2009–2015):** Alexander "BLACK DESIGNER" Poshelyuzhin and the Nevada team: Denis "Master" Voloshin, Anton "SAMIRAKUS" Smirnov, Osman "Wolfram" Ajiusmanov, Alexey Osadchiy, X'IL, Pixote, Mr.Stalin, Dmitriy Yermak, Pyran and Foxx.
- **Fallout 1.5: Resurrection:** the Resurrection team ([resurrection.cz](http://resurrection.cz/en)).
- **Olympus 2207:** Nebesa Games. Artem "Rainman" Samoilov, Alexander "Saur" Berezin, Sergey "Zoomer" Bokarev and Elena Samoilova; English translation by keyboard gecko.

Fallout is a trademark of Bethesda Softworks LLC. These stamps contain object ids and positions only, no game art.

Stamps cut by HogLord's `building_stamps.py` and `area_stamps.py`.
