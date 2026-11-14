# Troubleshooting

Run `python foc.py doctor` first. It checks everything setup is supposed to have done,
and says how to fix each thing that's missing.

The rest of this page covers the mistakes that cost the most time. Each was diagnosed
wrong at least once before the real cause turned up.

---

## gecko

**gecko opens on the wrong game, or my map won't open any more.**
A browse session didn't get to restore your profile (gecko crashed, or the window was
killed). Run `python foc.py edit`: it points gecko back at your mod and keeps your window
layout.

**"gecko is already running".**
Only one gecko can run at a time, because they all share one settings file. Close the
other window first.

**gecko crashes loading a map ("invalid vector subscript").**
gecko is missing `rpu.dat`. Fallout 2's own tile list has 3102 entries and RPU's has 4077,
so a map that uses a tile above 3101 crashes gecko without RPU, while the game runs it
fine. `python foc.py edit` mounts RPU and its add-ons in the game's own order.

**A total conversion won't open: "LST file contains non-text data".**
That game's `.lst` files have Cyrillic comments in them. The launchers fix this on the fly
(see the top of `tools/build_gecko_browsers.py`). Re-run that script if you added the game
to `config.json` by hand.

**A stamp from another game draws as a random mess.**
A stamp stores the ids of the game it was saved in. Stamps saved through
`python foc.py browse` are converted when gecko closes. To convert by hand:
`python tools\port_stamp.py --all --source sonora` (use the game's key from
`tools/sources.json`). Originals are kept in `_safety_backups\stamp_originals\`.

## The map in the game

**The floor is black, but the map loads.**
Check the elevation flags. In the map header, a **0** bit means the map *has* that
elevation: `0x2` is level 0, `0x4` is level 1 and `0x8` is level 2. So `0x0C` is a
single-level map on elevation 0. Don't work this out by comparing maps: that gives the
right number of levels with the labels shuffled, and every map still parses.

**An object is invisible in the game, or the game crashes at `0x48d9c4` when two things
share a tile.**
An art file name longer than 12 characters. The engine keeps only the first 12, so the
file is never found, while gecko shows it fine. `tools/fix_art_names.py` renames them.
`port.py` already picks short names.

**Art looks right in gecko and wrong in the game.**
The art's index is above 4096. The engine reads only 12 bits of it, while gecko reads all
24. Run `python tools\repack_art.py`, which moves everything your maps and stamps use
below the cap. `validate.py` fails if anything used sits above it.

**The mod "does nothing" and there's no error.**
Fallout 2 deletes loose `.pro` files in `data\proto\items\` and `data\proto\critters\` at
startup unless they're read-only or sfall's `DontDeleteProtos=1` is set.

**An empty square isn't empty.**
An empty floor tile is id **1**, not 0. Index 0 is a real index pointing at art that
doesn't exist.

**Names point at the wrong art after copying between games.**
Art lists (`art\<dir>\<dir>.lst`) count from **0**. Proto lists (`proto\<dir>\<dir>.lst`)
count from **1**. Get both wrong and they cancel out when you read and write the same
game, so it only shows up when something crosses between games. Never identify a proto
by its file name (`00001417.pro` is just its index): match on its type and art name.

## Ids

**`ids.py --check` fails.**
Your working copy uses more entries in some list than this toolkit expects, usually
because of a different RPU version or extra mods in `base\mods\mods_order.txt`. Mod ids
start at fixed points (`FOC_START` in `tools/ids.py`), with filler in between, so a bigger
base would shift every one of your ids. Remove the extra mods from `mods_order.txt`, or
raise the start numbers in `tools/ids.py` **before** you allocate anything.

**Never reorder or delete a line in `mod\ids\*.txt`** once something uses it: every id
below it moves. To retire an entry, blank its name and keep the line.

## Checking your work without launching the game

* `python foc.py render <map> -o out.png` draws a map in about a second.
* `python tools\render.py --sheet art/scenery/<prefix> -o out.png` contact-prints art,
  so you can see what a name really is.
* `gecko-cli map reachability --map <map> --data <base>\master.dat` lists the hexes,
  exits and objects the player can't reach.
* `python foc.py validate` cross-checks ids, scripts, protos, art and map registration.
