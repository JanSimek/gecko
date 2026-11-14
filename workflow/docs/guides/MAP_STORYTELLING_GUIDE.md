# Map Storytelling Guide

How Fallout 1, Fallout 2 and the total conversions make a map look post-apocalyptic and tell its story without a word of dialogue, and how FOC's maps do the same. It comes from two studies:

- **Measured.** `tools/story_survey.py` classified every object on 1,297 maps (FO1, FO2, Sonora, Nevada, Resurrection, Story of the Jackal, Yesterday, Olympus) by its in-game name. It found 4,340 vignettes, the small composed scenes of three or more story objects. Results are in `build/story_survey/`.
- **Looked at.** Close-ups of those scenes (`tools/story_crops.py`) and whole-town renders: the Den, Klamath, Broken Hills, Modoc, Junktown, the Hub, Sonora's Inferno City, Nevada's Beatty, Resurrection's Albuquerque, Fort Desolation and others.

[MAP_LAYOUT_GUIDE.md](MAP_LAYOUT_GUIDE.md) covers sizes and spacing. This guide covers what the ground, the walls and the clutter say.

---

## 1. Three kinds of place

Every settlement in the originals is one of three, and its materials say which at a glance.

| Kind | What it looks like | Examples |
|---|---|---|
| **Living in the ruins** | Pre-war shells reused. Dark concrete and brick walls, roofs gone, broken asphalt. Survivors patch them with plank and sheet metal, put up shacks in the gaps, and plant crops on old lots. | The Den, Klamath, New Reno, the Hub, Inferno City |
| **Built from nothing** | No pavement at all. Plank buildings round an open common with the well or water tank in it. Pens, crops and dead-tree groves, with one stone or plaster building as the landmark. | Modoc, Beatty's shacks, Broken Hills' outskirts |
| **A compound** | A pre-war or military site held by one group behind a wall: scrap sheet, razor wire, sandbags, a gate with a guard and a painted sign. Inside, it is orderly and has a purpose. | Fort Desolation, Vault City's courtyard, Navarro, NCR's entrance |

**Rule:** decide which kind a FOC location is before laying a tile. Its walls, ground and fences all follow from that choice.

## 2. The language of decay

**Ground.**

- Pavement is never a clean strip. Asphalt survives as black islands, with sand drifting into its edges and through its cracks.
- Sidewalk survives in fragments, mostly against building walls.
- Roads fade into dirt away from the centre, and manhole covers sit in the old streets.
- Built-from-nothing towns have no pavement at all.

**Walls.**

- Ruins are roofless shells and free-standing wall stubs.
- In ruined cities, damage increases outward:
  - intact, furnished buildings at the centre
  - then roofless shells
  - then lone wall fragments
  - then bare desert (Inferno City, the Den's north edge)

**Rubble.**

- Rubble fills ruined rooms and spills from collapsed buildings into the street, sometimes over a car (Sonora, Diego).
- It does not sit in the open as scattered stones.

**Growth.**

- Dead grass grows against walls, fences and kerbs, not in the middle of open ground.
- Dead trees stand in groves at the edges and in the gaps between buildings (Modoc, the Den's corner, Klamath's ruins).
- Measured: plants are 66% "open", but that is the outer ground. Inside towns the near-building density is about 0.3 objects per square against about 0.05 on open ground.

**Fire damage.**

- Firestorm ruins are wall stubs with charred bones among them and a burnt car (Nevada SLSHADOW).

## 3. The language of survival

What survivors build is made from what was lying around, and it shows.

- **Walls of scrap.**
  - Mismatched corrugated sheets leaning on posts, with razor wire laid in front and a sandbag stack by the gate (Fort Desolation).
  - Junk cars stacked in a line as a town wall (Junktown, Resurrection's Albuquerque, the Den's wrecking yard).
- **Shelter.** Tarps stretched between walls into lean-tos, with mattresses under them (the Den, Raiders, Vipers).
- **Fences.** Chain-link for the old world, plank rail fences for pens and fields.
- **Food.** Crop rows on cleared lots between ruins (Klamath, Beatty). Brahmin pens with a trough, and the slaughterhouse next door (Modoc).
- **Work shows.** A town's living is built into the map:
  - Broken Hills' mine head and ore building
  - the Den's wrecking yard
  - Modoc's pens and slaughterhouse
  - NCR's market stalls made of shelving and tarp with hand-lettered signs ("GUNS")

## 4. Vignettes: the small scenes

Measured across all games:

| | Town map | Outdoor map | Cave or dungeon |
|---|---|---|---|
| Vignettes (median) | **6** (upper quarter: 8) | **1** (upper quarter: 2) | 1 (upper quarter: 3) |

- A vignette holds a median of **7 objects** (upper quarter: 13).
- Vignettes sit by a building (47%), indoors (25%), on a road (16%) or in the open (11%).

The scenes designers return to, as seen:

| Scene | What is in it | Examples |
|---|---|---|
| **The driver** | A wreck on the road where it stopped, a skeleton beside or under it, spilled barrels. One object, one story, in empty desert. | Sonora RNDPETRO (a jackknifed semi), Sonora Diego |
| **Ambush aftermath** | An overturned wagon, scattered cargo and tires, bones, the brahmin wandering loose. | Resurrection DESERT4 |
| **The camp** | Bedrolls in a ring round a fire or fire barrel, wagons parked, pots, brahmin nearby. | Sonora PXSIGN, SotJ VIPERS, Nevada NEWRST |
| **The gang's yard** | Bikes or cars parked in a row, a fire barrel with the gang round it, a body outside the gate, graffiti. | Nevada GSTCAV1, New Reno |
| **Squatters** | Tarp lean-tos against an old wall, mattresses, a fire barrel, junk. | The Den, Resurrection RH_SEV |
| **The farm that got hit** | Pen and crops, then a blood trail, a carcass and the creature. | SotJ ROKOO |
| **The collapse** | Rubble spilling out of a building onto a car, with bones. | Sonora Diego |
| **The checkpoint** | A scrap wall, razor wire, sandbags, a guard at a gate, a painted sign. | Fort Desolation, Vault City |
| **The burying ground** | A town cemetery as rows of headstones and mounds in a fenced plot at the edge. The mob's dump as an open field of crosses and unburied bones. | Den, Sonora SAGRAVE, New Reno |

Which objects go together, measured as pairs within 3 squares:

- crates and trash (1,635)
- people and crates (1,519)
- people and trash (1,393)
- fire and people (842)
- fire and trash (712)
- signs and trash (698)
- wrecks and trash (658)
- camp and people (607)
- people and remains (499)
- camp and fire (422)

So the scenes are social: **fire draws people, people draw stores and trash.**

## 5. Where things go (measured)

| Class | Indoors | By a building | Road | Open | Note |
|---|---|---|---|---|---|
| Furniture | 49% | 42% | 7% | 2% | Rooms are furnished; the outdoors almost never is. |
| Machines | 13% | 76% | 5% | 5% | Against the building they serve. |
| Defences | 6% | 72% | 12% | 10% | 30% sit in the map's outer third: walls, gates, perimeters. |
| Crates and barrels | 27% | 51% | 13% | 8% | Stacked against walls and at doors. |
| Trash | 27% | 38% | 19% | 15% | Along walls and kerbs. |
| Fires | 10% | 52% | 25% | 13% | Fire barrels in streets and yards: the social node. |
| Signs | 35% | 38% | 20% | 8% | On building fronts and roadsides (billboards). |
| Wrecks | 5% | 33% | 36% | 26% | Where they stopped: on the road, or by a garage. |
| Remains | 11% | 48% | 6% | 34% | Against walls, or alone in the open. |
| Graves | 1% | 34% | 0% | 64% | In rows on the town's edge. |
| Camps | 44% | 45% | 3% | 8% | Bedding indoors or against a wall, never loose in the open. |

## 6. Landmarks and function

- **One landmark per town, in a different material:**
  - Modoc's white stone church on the common
  - Broken Hills' white-plaster store
  - Klamath's brick building
  - Sonora PXCITY's statue on the square

  The player navigates by it.
- **Every building does one job**, and its neighbours fit that job: the slaughterhouse by the pens, the store with sacks and barrels at its door, the bar with a neon sign.
- **The town's economy is a physical thing on the map** (mine, wrecking yard, pens, stalls).
- **People stand in small groups at doors and round fire barrels**, not spread evenly.

## 7. Rules for FOC maps

1. Choose the kind of place (§1). Every material follows from it.
2. Decay works outward from the centre: intact core, then roofless shells, then wall stubs, then desert. Pavement survives in islands, kerbs and sidewalks against walls.
3. Growth and trash hug walls, fences and kerbs. Open ground stays bare (about 0.05 objects per square).
4. Survivors build from scrap: sheet walls, wreck walls, tarps, plank pens, crops on old lots.
5. A town has 6 to 8 vignettes, an outdoor map 1 or 2, each of about 7 objects. Each tells one thing that happened here, and at least one tells the place's story.
6. Wrecks sit where they stopped (on the road), graves in rows at the edge, fires where people gather, defences on the perimeter.
7. One landmark, in a different material. The economy is visible.
8. Ruined rooms hold rubble. Lived-in rooms are furnished for one job.

Every FOC site plan lists its vignettes ("stories" in the plan file), drawn as numbered markers. The map is built with them.
