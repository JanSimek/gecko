"""
critter_stats.py - write critter proto stats the way the engine expects.

When the engine creates a critter it recomputes the proto's base derived
stats from SPECIAL (proto.cc _proto_update_init -> stat.cc
critterUpdateDerivedStats), overwriting them in the shared proto. Vanilla
protos are built for this: base is always the derived value and the tuning
lives in the bonus block (Pig Rat: base HP 22, bonus -13, total 9). A total
written straight into base is undone the first time one is spawned.
"""
from __future__ import annotations

import struct

STATS_OFF, BONUS_OFF = 48, 188
ST, PE, EN, CH, IN, AG, LK = range(7)
HP, AP, AC, MELEE, SEQ, CRIT = 7, 8, 9, 11, 13, 15


def _get(buf, off, stat):
    return struct.unpack_from(">i", buf, off + 4 * stat)[0]


def derived(buf) -> dict[int, int]:
    """stat.cc critterUpdateDerivedStats for the stats FOC tunes. Max HP uses
    BASE strength and endurance (critterGetBaseStatWithTraitModifier); the
    rest use base + bonus (critterGetStat)."""
    base = lambda s: _get(buf, STATS_OFF, s)                       # noqa: E731
    tot = lambda s: base(s) + _get(buf, BONUS_OFF, s)              # noqa: E731
    return {HP: base(ST) + 2 * base(EN) + 15, AP: tot(AG) // 2 + 5, AC: tot(AG),
            MELEE: max(tot(ST) - 5, 1), SEQ: 2 * tot(PE), CRIT: tot(LK)}


def total(buf, stat) -> int:
    return _get(buf, STATS_OFF, stat) + _get(buf, BONUS_OFF, stat)


def set_total(buf: bytearray, stat: int, value: int) -> None:
    """Make `stat` come out at `value` after the engine's recompute: base =
    derived, bonus = value - derived. Set SPECIAL first."""
    d = derived(buf)[stat]
    struct.pack_into(">i", buf, STATS_OFF + 4 * stat, d)
    struct.pack_into(">i", buf, BONUS_OFF + 4 * stat, value - d)
