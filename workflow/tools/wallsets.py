#!/usr/bin/env python3
"""
wallsets.py - build walls of a Fallout wall set by the originals' own grammar.

A wall set is described by what tools/wall_junctions.py and
tools/wall_sequences.py measure over every Fallout 1 and 2 map:
  * x-runs on odd hex rows, y-runs on even hex columns
  * the piece for each junction (lo, hi, up, dn): lo / hi the run continues to
    x-1 / x+1, up a y-run touches from above, dn a y-run hangs below a one-hex
    gap (with a blocker in the gap)
  * the order of the pieces along a run: the plain piece by column (or row)
    parity, and the detailed panels only as the pairs they are drawn as (the
    narrow piece on the odd hex, its wide partner on the next), at about the
    originals' rate; window frames only as a whole window (start pair, middle
    pairs, end pair) with the glass over them; an arch's lintel as its pair
  * how a door is framed, and the pieces that cap a run's free end (clean, or
    broken where a wall has been breached)

    from wallsets import WALLSETS, build
    cells, blocks, doors, glass = build(WALLSETS["dv"], xruns, yruns, doors, arches, windows, broken)

xruns: (x_high, x_low, y); yruns: (x, y_top, y_bottom); doors: (x, y, "x"|"y"),
doors at even x in an x-run; arches: the doors with no door in them;
windows: (y, x_from, x_to) on an x-run, x_from odd and x_to even; broken: hexes
next to a breach, whose run end is drawn broken off.

Sets:
  dv   Fallout 2's industrial panel walls (Gecko's power plant and tunnels, the
       EPA, the Glow, the Hub's stash). Doors are Gecko's round hatches
       (gekodor5 in an x-run, gekodor6 in a y-run) in their wall-piece frames
       (dorwygk1-4, with blockers), five hexes along the run, as GECKTUNL and
       GECKPWPL have every one of them.
  mmb  Fallout 2's military base walls (the EPA, the Sierra Army Depot, the
       military base; Fallout 1's Mariposa and the Master's lair). Doors are the
       vault doors: vdoorf in an x-run between mmb1037 and mmb1039 (with a
       blocker) and mmb1036, vdoors in a y-run between mmb1040 and mmb1041 and
       mmb1038, as every one of theirs is (84 and 52 doors).
"""

from __future__ import annotations

G = "g"

DV = dict(
    name="dv",
    junction={
        (G, G, None, G): "dv1050", (None, G, G, G): "dv1051", (None, G, None, G): "dv1033",
        (G, None, None, G): "dv1032", (G, None, G, G): "dv1049", (None, G, G, None): "dv1031",
        (G, None, G, None): "dv1053", (G, G, G, None): "dv1052", (G, G, G, G): "dv1054",
    },
    plain_x={0: "dv1000", 1: "dv1004"},                       # by x % 2
    # (odd x, even x+1) panel pairs and their counts in the originals (wall_sequences.py)
    pairs_x=[(("dv1002", "dv1001"), 72), (("dv1006", "dv1005"), 45), (("dv1056", "dv1055"), 24),
             (("dv1058", "dv1057"), 16), (("dv1008", "dv1007"), 10)],
    singles_x=[("dv1003", 159), ("dv1059", 15), ("dv1047", 13)],   # even-x pieces that stand alone
    plain_count_x=1301,                                      # plain dv1000s, for the rate
    window=dict(start=("dv1014", "dv1013"), middle=("dv1012", "dv1011"), end=("dv1010", "dv1009"),
                glass={"dv1013": 0x02000096, "dv1012": 0x02000095, "dv1011": 0x02000094, "dv1010": 0x02000093}),
    plain_y={0: "dv1019", 1: "dv1021"},                       # by y % 2
    pairs_y=[(("dv1018", "dv1017"), 73), (("dv1061", "dv1060"), 60), (("dv1016", "dv1015"), 20)],
    plain_count_y=1704,
    y_top_free="dv1048", y_bottom_free="dv1046",
    y_top_broken="dv1083", y_bottom_broken="dv1062",
    y_door_above="dv1037", y_door_below="dv1038", y_door_below2="dv1039",
    jamb_hi="dv1042", jamb_lo=("dv1041", "dv1040"),
    x_door=dict(pid=0x02000491, frame={-2: "dorwygk2", -1: "block", 1: "block", 2: "dorwygk1"}),
    y_door=dict(pid=0x02000492, frame={-1: "dorwygk4", 1: "dorwygk3", 2: "block", 3: "block"}),
    y_extra={1: ["dv1020"]},
)

MMB = dict(
    name="mmb",
    junction={
        (None, G, None, G): "mmb1034", (G, None, None, G): "mmb1033", (None, G, G, None): "mmb1032",
        (G, None, G, None): "mmb1050", (G, G, None, G): "mmb1047", (G, G, G, None): "mmb1049",
        (None, G, G, G): "mmb1048", (G, None, G, G): "mmb1046", (G, G, G, G): "mmb1051",
    },
    plain_x={0: "mmb1025", 1: "mmb1003"},                     # by x % 2
    x_extra={1: ["mmb1008"]},                                 # the other plain odd piece (765 to 1686)
    pairs_x=[(("mmb1001", "mmb1000"), 189), (("mmb1006", "mmb1005"), 84)],
    singles_x=[("mmb1002", 274), ("mmb1007", 60)],
    plain_count_x=1474,
    window=dict(start=("mmb1014", "mmb1013"), middle=("mmb1012", "mmb1011"), end=("mmb1010", "mmb1009"),
                glass={"mmb1013": 0x02000096, "mmb1012": 0x02000095, "mmb1011": 0x02000094, "mmb1010": 0x02000093}),
    plain_y={0: "mmb1020", 1: "mmb1022"},                     # by y % 2
    y_extra={0: ["mmb1016"], 1: ["mmb1021"]},
    pairs_y=[(("mmb1019", "mmb1018"), 125), (("mmb1024", "mmb1023"), 129), (("mmb1017", "mmb1015"), 44)],
    plain_count_y=704,
    y_top_free="mmb1045", y_bottom_free="mmb1043",
    y_top_broken="mmb1045", y_bottom_broken="mmb1043",
    y_door_above="mmb1040", y_door_below="mmb1041", y_door_below2="mmb1038",
    jamb_hi="mmb1037", jamb_lo=("mmb1039", "mmb1036"),
    x_door=dict(pid=0x02000012, frame={-1: "mmb1037", 1: "mmb1039", 2: "mmb1036"}, blocks=(1,)),
    y_door=dict(pid=0x02000013, frame={-1: "mmb1040", 1: "mmb1041", 2: "mmb1038"}),
)

WALLSETS = {"dv": DV, "mmb": MMB}


def _transitions(ws):
    """The originals' next-piece counts for this set (build/wall_sequences/<set>.json)."""
    import json
    from pathlib import Path
    p = Path(__file__).resolve().parent.parent / "build" / "wall_sequences" / f"{ws['name']}.json"
    if not p.exists():
        raise SystemExit(f"{p} is missing: run python tools/wall_sequences.py {ws['name']}")
    d = json.loads(p.read_text(encoding="utf-8"))
    return {"x": d["x"], "y": d["y"]}


def _hash(x, y, salt=0):
    return ((x * 73856093) ^ (y * 19349663) ^ (salt * 83492791)) & 0xFFFF


def build(ws, xruns, yruns, doors, arches=(), windows=(), broken=()):
    """{(x, y): piece} for the walls, the gap blockers, [(x, y, door_pid)] for the doors and
    [(x, y, glass_pid)] for the window glass."""
    door_at = {(x, y): k for x, y, k in doors}
    arch = set(arches)
    broken = set(broken)
    frame = {}
    hatch = []
    for (x, y), k in door_at.items():
        if (x, y) in arch:
            continue
        spec = ws["x_door"] if k == "x" else ws["y_door"]
        for d, piece in spec["frame"].items():
            frame[(x + d, y) if k == "x" else (x, y + d)] = piece
        hatch.append((x, y, spec["pid"]))
    xs, ys = set(), set()
    for xh, xl, y in xruns:
        assert y % 2 == 1, f"x-run on even row {y}"
        for x in range(xl, xh + 1):
            if (x, y) not in door_at:
                xs.add((x, y))
    for x, y0, y1 in yruns:
        assert x % 2 == 0, f"y-run on odd column {x}"
        for y in range(y0, y1 + 1):
            if (x, y) not in door_at:
                ys.add((x, y))
    for c in list(xs):
        x, y = c
        if not ({(x - 1, y), (x + 1, y)} & (xs | set(door_at))) and c in ys:
            xs.discard(c)
    gaps = set()
    for (x, y) in xs:
        ys.discard((x, y))
        if (x, y + 1) in ys and (x, y + 2) in ys:
            ys.discard((x, y + 1))
            gaps.add((x, y + 1))
    cells, fixed = {}, set()
    # junctions and door jambs first: they are fixed
    for (x, y) in xs:
        lo = G if (x - 1, y) in xs else ("door" if (x - 1, y) in door_at else None)
        hi = G if (x + 1, y) in xs else ("door" if (x + 1, y) in door_at else None)
        up = G if (x, y - 1) in ys else None
        dn = G if (x, y + 1) in gaps else None
        if up or dn:
            key = (G if lo else None, G if hi else None, up, dn)
            piece = ws["junction"].get(key)
            if piece is None:
                raise SystemExit(f"{ws['name']}: no junction piece for {key} at ({x},{y})")
            cells[(x, y)] = piece
            fixed.add((x, y))
        elif not (lo or hi):
            raise SystemExit(f"{ws['name']}: a lone x-run hex at ({x},{y})")
    for (x, y) in arch:
        if door_at.get((x, y)) == "x":
            for c, piece in (((x - 1, y), ws["jamb_hi"]), ((x + 1, y), ws["jamb_lo"][0]), ((x + 2, y), ws["jamb_lo"][1])):
                if c in xs and c not in fixed:
                    cells[c] = piece
                    fixed.add(c)
    for c, piece in frame.items():
        cells[c] = piece
        fixed.add(c)
        gaps.discard(c)
    # a door frame piece that has a blocker on its hex as well (mmb's vault doors)
    for (x, y), k in door_at.items():
        if (x, y) not in arch:
            spec = ws["x_door"] if k == "x" else ws["y_door"]
            for d in spec.get("blocks", ()):
                gaps.add((x + d, y) if k == "x" else (x, y + d))
    # windows: the whole frame, start pair, middle pairs, end pair, and the glass
    glass = []
    for y, x0, x1 in windows:
        assert x0 % 2 == 1 and x1 % 2 == 0 and x1 - x0 >= 3, (y, x0, x1)
        w = ws["window"]
        n_mid = (x1 - x0 + 1) // 2 - 2
        seq = list(w["start"]) + list(w["middle"]) * n_mid + list(w["end"])
        for i, piece in enumerate(seq):
            c = (x0 + i, y)
            if c not in xs or c in fixed:
                raise SystemExit(f"{ws['name']}: the window at row {y} runs over ({c[0]},{y}), not a plain x-run hex")
            cells[c] = piece
            fixed.add(c)
            if piece in w["glass"]:
                glass.append((c[0], y, w["glass"][piece]))
    # the runs between the fixed pieces: a chain over the originals' own next-piece table
    # (tools/wall_sequences.py), at its frequencies, so every neighbouring pair is one they use and
    # panels come where theirs do (beside a corner or T, or in a series)
    trans = _transitions(ws)

    def chain(cells_in_order, parity, along_x):
        """Fill a list of free hexes in run order; prev/next fixed pieces bound the chain."""
        if not cells_in_order:
            return
        x0, y0 = cells_in_order[0]
        before = (x0 - 1, y0) if along_x else (x0, y0 - 1)
        xl, yl = cells_in_order[-1]
        after = (xl + 1, yl) if along_x else (xl, yl + 1)
        prev = cells.get(before)
        nxt_fixed = cells.get(after) if after in fixed else None
        table = trans["x" if along_x else "y"]
        for i, c in enumerate(cells_in_order):
            par = (c[0] if along_x else c[1]) % 2
            allowed = parity[par]
            if prev is not None and prev in table:
                options = [(b, n) for b, n in table[prev].items() if b in allowed]
            else:
                options = [(a, 1) for a in allowed[:1]]
            if i == len(cells_in_order) - 1 and nxt_fixed is not None:
                closing = [(b, n) for b, n in options if nxt_fixed in table.get(b, {})]
                options = closing or options
            if not options:
                options = [(allowed[0], 1)]
            piece = pick(options, _hash(c[0], c[1], 7))
            # a piece whose own partners can't sit before the fixed piece two hexes on (a panel
            # pair started one hex before a door jamb): pick again from those that can
            if i == len(cells_in_order) - 2 and nxt_fixed is not None:
                after_par = (par + 1) % 2

                def leads(p):
                    return any(nxt_fixed in table.get(q, {}) for q in table.get(p, {}) if q in parity[after_par])
                if not leads(piece):
                    good = [(b, n) for b, n in options if leads(b)]
                    if good:
                        piece = pick(good, _hash(c[0], c[1], 7))
            cells[c] = piece
            prev = piece

    def pick(options, h):
        tot = sum(n for _, n in options)
        h %= tot
        for v, n in options:
            if h < n:
                return v
            h -= n
        return options[0][0]
    xx, yx = ws.get("x_extra", {}), ws.get("y_extra", {})
    xpar = {0: [ws["plain_x"][0]] + [b for (a, b), _ in ws["pairs_x"]] + [v for v, _ in ws["singles_x"]] + xx.get(0, []),
            1: [ws["plain_x"][1]] + [a for (a, b), _ in ws["pairs_x"]] + xx.get(1, [])}
    ypar = {0: [ws["plain_y"][0]] + [b for (a, b), _ in ws["pairs_y"]] + [ws["y_top_free"]] + yx.get(0, []),
            1: [ws["plain_y"][1]] + [a for (a, b), _ in ws["pairs_y"]] + yx.get(1, [])}
    rows = {}
    for (x, y) in xs:
        rows.setdefault(y, []).append(x)
    for y, xs_row in rows.items():
        run = []
        for x in sorted(xs_row) + [None]:
            if x is not None and (x, y) not in fixed and (not run or run[-1][0] == x - 1):
                run.append((x, y))
                continue
            chain(run, xpar, True)
            run = [(x, y)] if x is not None and (x, y) not in fixed else []
        chain(run, xpar, True)
    # y-runs: the door pieces and a top end with nothing above are fixed, the rest a chain
    for (x, y) in sorted(ys):
        above = (x, y - 1) in ys or (x, y - 1) in xs or (x, y - 1) in gaps or (x, y - 1) in door_at
        below = (x, y + 1) in ys or (x, y + 1) in xs or (x, y + 1) in door_at or (x, y + 1) in gaps
        if (x, y) in fixed:
            continue
        if door_at.get((x, y + 1)) == "y" and (x, y + 1) in arch:
            cells[(x, y)] = ws["y_door_above"]
        elif door_at.get((x, y - 1)) == "y" and (x, y - 1) in arch:
            cells[(x, y)] = ws["y_door_below"]
        elif not above and (x, y) in broken:
            cells[(x, y)] = ws["y_top_broken"]
        elif not below and (x, y) in broken:
            cells[(x, y)] = ws["y_bottom_broken"]
        elif not above and (x, y - 1) not in gaps and y % 2 == 0:
            cells[(x, y)] = ws["y_top_free"]
        else:
            continue
        fixed.add((x, y))
    cols = {}
    for (x, y) in ys:
        cols.setdefault(x, []).append(y)
    for x, ys_col in cols.items():
        run = []
        for y in sorted(ys_col) + [None]:
            if y is not None and (x, y) not in fixed and (not run or run[-1][1] == y - 1):
                run.append((x, y))
                continue
            chain(run, ypar, False)
            run = [(x, y)] if y is not None and (x, y) not in fixed else []
        chain(run, ypar, False)
    return cells, gaps, hatch, glass
