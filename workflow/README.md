# Fallout 2 map workflow

The tools I use to make maps for my Fallout 2 mod, set up so anyone can use them. It's a
toolkit around the [gecko](../README.md) map editor: you browse the original games and the
total conversions, borrow their buildings as stamps, plan a site, build and finish the map,
then put it in the game.

Everything runs from a terminal with plain commands, and nothing needs an AI agent. There's
a menu if you don't want to remember any of it:

```bat
python foc.py
```

---

## What you need

| | |
|---|---|
| **Windows 10 or 11** | gecko and the game are Windows programs |
| **Fallout 2** (GOG or Steam) | your own copy. Nothing from the game is in this repo |
| **[Restoration Project Unofficial](https://github.com/BGforge/fallout2-rpu)** installed into it | the tools expect its tile list and map fixes (`mods\rpu.dat`) |
| **[Python 3.10+](https://www.python.org/downloads/)** | tick "Add python.exe to PATH" when installing |
| **gecko** | the zip from [Releases](https://github.com/HogLord69/gecko/releases/latest). Unzip it anywhere |

**Optional:** any other Fallout-engine games you own, to borrow buildings from: Fallout 1,
Fallout: Sonora, Fallout: Nevada, Nevada Mod (Extended), Olympus 2207, Fallout 1.5:
Resurrection, Story of the Jackal, Fallout: Yesterday.

## Set up (once)

1. Get it. The easiest way is the zip from
   [Releases](https://github.com/HogLord69/gecko/releases/latest): gecko, the stamps and
   this `workflow` folder in one download. Unzip it anywhere. (Or clone this repo, if you
   build gecko yourself.)
2. Open a terminal in the `workflow` folder.
3. Run:

   ```bat
   python setup.py
   ```

   It installs three Python packages (pillow, numpy, scipy), finds your games (press
   Enter to accept what it finds, or type a folder), asks where `gecko.exe` is, and offers
   to make the **working copy**: a copy of Fallout 2 + RPU in `workflow\base` (about
   1.2 GB). Your real install is only read, and the copy step re-counts it afterwards and
   stops if anything changed.
4. Run `python foc.py doctor`. Every line should say `[ok]`. Anything that doesn't
   says how to fix it.

Your answers go into `workflow\config.json`. It's a short text file: open it to change a
path, or run `setup.py` again.

---

## How a map gets made

The tools are grouped by stage. [docs/TOOLS.md](docs/TOOLS.md) lists all of them in the
same order, and `python foc.py tools` prints the list in the terminal.

### 1. Look at how the originals did it

```bat
python foc.py extract maps/arvillag.map build\arvillag.map
python foc.py render build\arvillag.map -o build\arvillag.png
```

`extract` copies a map out of the game's archives, and `render` draws any map to a PNG in
about a second. [docs/guides/MAP_LAYOUT_GUIDE.md](docs/guides/MAP_LAYOUT_GUIDE.md) holds the
measured habits of every town map in eight games: play area about 40 × 50 squares, about 5
buildings 4 squares apart, exits on two sides, and so on.
[MAP_STORYTELLING_GUIDE.md](docs/guides/MAP_STORYTELLING_GUIDE.md) covers what makes a map
read as post-apocalyptic.

### 2. Borrow buildings: stamps

A **stamp** is a piece of a map (a building with its walls, floor, roof and furniture)
that you drop into your own map in gecko with **Edit → Stamp Pattern**.

* **Ready-made:** [`resources/stamps`](../resources/stamps) (`stamps` in the zip) has
  1,567 stamps cut from Fallout 1, Fallout 2 and four total conversions, sorted by what
  they're built from, plus 21 props of my own (`HogLord Props`). To use
  them in gecko, copy the folders you want into `%LOCALAPPDATA%\gecko\gecko\patterns\`.
  Only the `Fallout 2 Stamps` folder draws correctly in your mod as it is. The others hold
  their own game's ids, so see the next point.
* **Your own, from any game you own:**

  ```bat
  python foc.py browse
  ```

  Pick a game and gecko opens on that game alone. Find a building, select it, save it as
  a stamp, then close gecko. The launcher puts your normal profile back and **converts the
  stamps you saved into your mod's ids**, porting any art your mod lacks. Don't use
  File → Save on the other game's maps: gecko would write into that game's install.

### 3. Plan the site (optional, but it's how the good maps happen)

`site_plan.py` draws a plan to scale from a small JSON file and checks it (overlaps, gaps,
buildings off the road, distance from the start to the exits). `real_site.py` pulls a real
place's layout from OpenStreetMap to base it on. See
[docs/guides/SITE_PLANS.md](docs/guides/SITE_PLANS.md).

### 4. Build the map

Two ways, and most maps use both:

* **In gecko:** `python foc.py edit` opens gecko on your mod (your working copy plus
  anything you've deployed). Make a new map, lay ground, stamp buildings, place scenery.
  Save it to `workflow\mod\data\maps\YOURMAP.MAP` (8-letter name, `.MAP`).
* **From a recipe:** `compose_map.py` builds a map from a JSON recipe that lifts whole
  levels or buildings from other maps and places stamps by coordinates. `sitekit.py`
  writes such recipes from code. Run either one with `--preview` first.

### 5. Finish it the way Fallout 2 does

| Job | Tool |
|---|---|
| Camera stops at the edge of the map (Hi-Res Patch edge file) | `map_edges.py` |
| Paint ground into the corners the edge box leaves black | `paint_corners.py` |
| Exit grids where roads meet the edge, with block hexes along the rest | `exit_layout.py` |
| Join two maps with exit grids | `map_link.py`, then check with `check_links.py` |
| Cars and carts solid, big scenery blocked out | `car_blockers.py`, `map_vehicle_blockers.py` |
| A town sitting off to one side moved to the middle | `shift_map.py` |
| Player start and map script | `map_header.py` |

Render after each step. It's quicker than launching the game.

### 6. Put it in the game

1. Give the map an id: add a line to `mod\ids\maps.txt`. The comments at the top of that
   file explain every field. The short version:

   ```
   200 YOURMAP.MAP Your Town world_pos=625,750 size=Small music=12junktn
   ```

2. Check and deploy:

   ```bat
   python foc.py validate
   python foc.py deploy
   ```

   `deploy` writes your mod into `base\mods\foc\` (never into the game's own files) and
   adds your map to Fallout 2's world map. The vanilla towns, world-map art and random
   encounters stay as they are. Making a total conversion with your own world map? Put
   `"own_worldmap": true` in `config.json`. Deploy then hides the vanilla towns and
   expects your art in `mod\data\art\intrface\WRLDMP00.FRM` to `WRLDMP19.FRM` (see
   `tools/worldmap.py`).
3. Play: run `base\PLAY.bat`. The map appears at its `world_pos` on the world map.

---

## Folders

```
workflow\
  foc.py              the menu and shortcuts
  setup.py            one-time setup
  config.json         where your games are (written by setup; yours alone)
  tools\              the tools: python tools\<name>.py --help
    foclib\           shared code: reading .dat, .map, .pro, .frm, .lst
    gecko_browse\     the per-game gecko launchers (generated by setup)
  docs\
    TOOLS.md          every tool, one line each
    TROUBLESHOOTING.md
    ASSETS_AND_LICENSES.md
    guides\           how the original maps are built, measured
  mod\
    ids\              your mod's id registry (maps, scripts, protos, art)
    data\             your maps and ported art (made by you and the tools)
  base\               the working copy of the game (made by setup)
  build\              renders and reports (safe to delete)
  corpus\             searchable map fragments (python foc.py corpus index)
```

`config.json`, `base\`, `build\`, `corpus\` and `mod\data\` are never committed. They
belong to your machine, and `mod\data\` fills up with art ported from other games, which
isn't yours to publish.

## What isn't included, and why

No game files. Not Fallout 2's, not RPU's, and not any total conversion's: no `.dat`,
`.frm`, `.pro` or `.map`. They belong to their makers, and several total conversions ask
to be asked first. Everything heavy is rebuilt from **your** installs instead:

| Thing | Rebuild it with |
|---|---|
| working copy of the game | `python setup.py --build-base` |
| fragment corpus (13,000+ pieces of real maps, searchable) | `python foc.py corpus index` |
| art index (what art you already own, by picture) | `python tools\art_index.py` |
| stamps from a total conversion, converted to your ids | `python foc.py browse` |

The stamps in `resources/stamps` are the exception: they hold object ids and positions
only, no art, and carry a credit line for the people who made the original maps. Read
[docs/ASSETS_AND_LICENSES.md](docs/ASSETS_AND_LICENSES.md) before you ship a mod made with
any of this.

## When something goes wrong

Start with `python foc.py doctor`, then
[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md), which lists the mistakes that cost
the most time and how to spot each one.
