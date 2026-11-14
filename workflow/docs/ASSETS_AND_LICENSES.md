# Assets and licences

## What's in this repo

* **gecko** is Apache-2.0, by Jan Šimek and contributors ([upstream](https://github.com/JanSimek/gecko)).
  The changes in this fork are under the same licence.
* **The tools in `workflow/tools`** are my own work, under the same Apache-2.0 licence.
* **The stamps in `resources/stamps`** hold object ids and positions only, with no art.
  The layouts are the work of the people who made the original maps.
  [`resources/stamps/README.md`](../../resources/stamps/README.md) credits them; keep those
  credits with the stamps.

## What's not in this repo, and never should be

No files from Fallout, Fallout 2, the Restoration Project, or any total conversion: no
`.dat`, `.frm`, `.pro`, `.map`, `.msg` or `.acm`. Fallout is a trademark of Bethesda
Softworks LLC. The tools read these from the copies you own and rebuild everything
derived from them on your machine.

## Before you ship a mod made with this

When you port a proto, a piece of art or a whole map from another game, `port.py` writes a
line to `mod\assets.csv` and `ASSETS.md` saying what came from where. That ledger is your
credits list. Each game's entry in `tools/sources.json` says who made it.

Whether you may *redistribute* something is a separate question from whether you can
technically port it:

* **Fallout 2 / Restoration Project.** Mods that build on Fallout 2's own data are the
  normal case. killap's RP readme asks derived work to mention it in its readme.
* **Fallout: Sonora (Extended Release).** Its `license.txt` allows personal,
  non-commercial use only. Ask its authors before releasing anything that contains its
  art.
* **Fallout: Nevada, Olympus 2207, Resurrection, Story of the Jackal, Yesterday.** Credit
  them, and ask their authors before releasing their art inside your mod.
* **FOnline 2** art (via `fonline2_source.py`) is browse-and-reference material.
  Treat it the same way.

When in doubt, ask. These are small teams, and most of them answer.
