# ID registry

These files are the source of truth for every ID this mod allocates.
One entry per line, in allocation order. The first line of each file lands at
that space's `FOC_START` (see `tools/ids.py`), and each subsequent line is the
next index.

`tools/gen_ids.py` turns these into `scripts_src/foc/foc_ids.h`
(SCRIPT_* / GVAR_* / PID_* defines), and `tools/deploy.py` turns them into the
real `.lst` / `vault13.gam` the game loads, padding the gap between the end of
the base's entries and `FOC_START` with filler.

NEVER reorder or delete a line once content references it - that repoints every
ID below it. To retire an entry, blank its name but keep the line.

Blank lines and `#` comments are allowed; `#` lines do not consume an index,
blank lines DO (they are retired slots).

## Art filenames: 12 characters, no exceptions

The engine reads every `art/<dir>/<dir>.lst` line with `strncpy(dst, line, 12)`
(`art.cc::artReadList`). A longer name is silently cut, the file is never found,
`art_ptr_lock()` returns NULL and the object is invisible - or crashes the game
at `0x48d9c4` when it shares a hex with another object. Gecko does not truncate,
so a bad name looks fine in the editor. `port.py` therefore names imported art
`<source tag><stem>.frm` (8.3) via `ids.engine_art_name()`, and `validate.py`
fails on any FOC entry over 12 characters. `art_renames.csv` records the
2026-09-17 mass rename that brought the existing 8,839 entries into line; a FID
is an index, so no proto or map changed.

