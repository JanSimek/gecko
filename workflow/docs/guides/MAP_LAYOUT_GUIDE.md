# Map Layout Guide

How the town maps of Fallout 1, Fallout 2 and the total conversions are laid out, measured over every map in each install by `tools/map_survey.py`, and what that means for FOC's own maps.

**Sources surveyed:** Fallout 1 (72 maps), Fallout 2 with RPU (180), Fallout: Sonora (151), Fallout: Nevada (137), Olympus 2207 (113), Fallout 1.5: Resurrection (107), Story of the Jackal (40) and Fallout Yesterday (497). Each map elevation is classed as a **town** (outdoors, three or more roofed buildings), **outdoor** (fewer buildings), **cave** or **interior**. There are 310 town elevations.

A map is a 100 × 100 grid of **squares**; a **hex** is half a square each way.

---

## 1. What a town map looks like

Medians over the 310 town elevations, with the spread between the games:

| Measure | Median | Across the games |
|---|---|---|
| Floored area | 92 × 91 squares | nearly the whole grid in every game |
| **Play area** (inside the exit grids) | **39 × 48 squares** | 35–45 wide, 42–61 deep |
| Buildings (roofed, 24+ squares) | **5** | 4–7 |
| Building size (roofed squares) | 65 | 56–74 (Story of the Jackal: 28) |
| **Gap between neighbouring buildings** | **4 squares** | 3–5 |
| Road share of the floor | 12% | 4–22% |
| Exit-grid hexes | **~64, on 2 sides** | |
| Player start to the nearest exit | **10–15 squares** | |
| Objects near buildings (within 3 squares) | **0.29 per square** | 0.14–0.61 |
| Objects on open ground | **0.05 per square** | 0.03–0.11 (Fallout 2: 0.36) |
| Floor with nothing within 4 squares | **55%** | 38–62% |
| Critters inside / outside buildings | 6 / 8 | |

Fallout 2 is the dense one: its towns carry about 0.2 plants per square of open ground, where every other game carries 0.01–0.03. Its lush maps (Vault City, Modoc's gardens) pull that up.

## 2. The habits behind the numbers

1. **A small town on a big floor.** The playable town is a compact block of about 40 × 50 squares. The floor carries on well past it, almost to the edge of the grid, as plain ground, so scrolling never shows the black void.
2. **Scroll blockers ring the play area.** Almost every town has a ring of scroll blockers (misc proto 12, `0x0500000C`) just outside what the player may see: 4/4 sampled Fallout 1 towns, 11/11 Fallout 2, 13/13 Sonora, 9/9 Nevada, 5/5 Resurrection, 21/25 Yesterday (Olympus uses none). It is the engine's only camera limit (`tools/scroll_ring.py` explains the mechanics). That holds at 640x480. FOC runs 1920x1080 at 2x through Mash's Hi-Res Patch, whose view is far bigger and which ignores scroll blockers. It stops the camera with a per-map edge file instead, `maps/<name>.edg`: one box per elevation, given by four hexes for its left, top, right and bottom sides. With `EDGE_CLIPPING_ON=1` the patch stops scrolling at that box and draws everything outside it black. RPU ships one for every vanilla map. The box sits on painted ground around the play area: on Arroyo it fits just inside the tree line, and in the vaults and caves dark rock is painted round the rooms for the box to sit on. `tools/map_edges.py` makes FOC's. FOC's maps carry no black frame pieces: the edge box cuts the view off instead, and any void a box takes in is painted with a copy of the map's own ground beside it (`tools/paint_corners.py`). A map that sits off to one side of the grid is moved to the middle (`tools/shift_map.py`; the courthouse was). Underground maps (the Facility Below, RWFAC1-3) follow the vaults and caves instead: the box goes round every room, and the empty squares under it and a little past it are laid with Fallout 2's rock field, cav4009 with cav4010 on the squares of even rows and odd columns, the one pattern every vault, cave and bunker in Fallout 2 uses (`tools/rock_field.py`). Measured over about 1,200 boxes in Fallout 1, Fallout 2, Nevada, Resurrection, SotJ, Yesterday and Olympus (`tools/edge_survey.py`):
   - The box never covers void. The median share of void inside a box is 0.0% in every source, and the 90th percentile is 0.3% or less in all but Olympus and Yesterday.
   - The floor is painted well past the box: 30% (Fallout 2) to 68% of a map's painted floor lies outside it, and walkable ground carries on out there.
   - Exits sit at the box edge. A patch outside the box is a median 12–24 px (a hex or two) beyond it, so the player walks to the edge of the screen and leaves.
   - Fallout 2's black frame pieces line up with the box edges, within 16–48 px.
   - Each elevation has one box; extra zones are almost never used. An elevation with nothing on it gets the whole grid.
3. **Exits only where travel makes sense.** Exit grids are not a ring round the map. They sit in patches on about two sides, usually where a road leaves: roughly 64 hexes in all, the patch at a road end leading to the connected map and the rest to the world map.
   - **What a patch looks like.** Each exit-grid object draws a large translucent patch in the editor (`art/misc/exitgrd1-8`, green; the world map's are red, `ext2grd1-8`, and the originals store the red art on world-map exits). The eight variants are the four directions of a map edge, each in two halves that overhang to opposite sides: 1/2 a flat band, 3/4 a tall band, 5/6 a shallow diagonal along a hex row, 7/8 a steep diagonal along a hex column. Fallout 2 draws an exit as the *outline* of a zone hanging in from the edge box: each side of the outline uses the variant whose art overhangs into the zone, so the zone reads as one translucent patch, and the row lying on the box edge is spaced out. The rest of the box edge is an unbroken row of invisible block hexes ("Block Hex Auto Inviso", scenery `0x02000158`), so the player leaves the screen only by crossing an exit's outline. Mappers reused prefabs: Modoc and Gecko carry the very same world-map exit. The game itself never draws exit grids; it only asks whether the hex the player steps on holds one.
   - **FOC's exits** are those prefabs, copied object for object from Fallout 2 (`tools/exit_templates.py`: Modoc's world-map exit for a bottom edge, Modoc's exit to map 19 for a top edge, Gecko's for a left edge and Klamath's for a right edge). `tools/exit_layout.py` stamps one where each road meets the edge box, runs the block row along the rest of the edge, and puts each arrival hex a few steps in from the exit it pairs with, facing into the map.
4. **A road runs through.** The road crosses the play area and carries on off it; buildings front it, a few squares apart, often with a sidewalk strip between building and road.
5. **Clutter gathers.** Weeds, dead trees, barrels, crates and junk collect around buildings and along the road (about 0.3 objects per square). Open ground beyond stays mostly bare (about 0.05), and about half the floor has nothing on it at all.
6. **Start near an exit.** The player arrives 10–15 squares inside the edge they came in by, usually on the road.

## 3. Checking a map

`tools/map_survey_one.py <map> [...]` measures FOC maps the same way, beside the town medians. As of this writing:

| Measure | Towns | CLCOURT.MAP | Clairemont.map | RWCORE2.MAP |
|---|---|---|---|---|
| Floored area | 92 × 91 | 94 × 94 | 94 × 97 | 75 × 73 |
| Buildings | 5 | 3 | 7 | 3 |
| Building gap | 4 | 9 | 4 | 4 |
| Exit hexes / sides | ~64 / 2 | 72 / 2 | 166 / 2 | 602 / 4 |
| Near-building clutter | 0.29 | 0.79 | 0.31 | 0.29 |
| Empty floor | 55% | 65% | 17% | 6% |
| Scroll blockers | ring | 928 (two loops) | **none** | 433 |

**Clairemont.map has no scroll-blocker ring**, so the camera can be dragged out past the town. `tools/scroll_ring.py` can add one when the owner wants it.

## 4. Tools

| Tool | What it does |
|---|---|
| `tools/map_survey.py` | Surveys every map in every source into `build/map_survey/maps.csv` and `summary.json`. |
| `tools/map_survey_sheet.py` | Renders each game's most typical town to `build/map_survey/typical_<source>.png`. |
| `tools/map_survey_one.py` | Measures FOC maps beside the town medians. |
| `tools/build_clcourt.py` | Builds the Clairemont courthouse map by these rules. |
| `tools/foclib/dat1.py` | Reads Fallout 1's DAT1 archives, so `fo1` is a source like the others. |
| `tools/map_edges.py` | Writes each map's edge box (`maps/<name>.edg`); underground maps are boxed round everything walkable. |
| `tools/rock_field.py` | Lays the rock field round an underground map's rooms, under and past its edge box. |
| `tools/restore_map.py` | Puts a set-aside map back into `mod/data/maps`, re-pointing its art by name after any art repack. |
