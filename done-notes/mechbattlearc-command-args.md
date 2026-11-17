# MechBattleArc: agent commands are cut off after 16 words

**From:** MechBattleArc (HEATLINE), Nov 2026

## Problem
`arc_core.c` splits an agent command into `const char *argv[16]`, so any words after the 16th are dropped without a
warning. The `loadout` command takes 18 words (`loadout I FRAME W0..W3 M0..M3 ST SL RAD PF PL PR PB COAT`), so its
last values were lost silently. The game worked around it with a separate `designcoat` command.

## Ask
- Raise the limit (32 or 64 would cover every game so far), or
- at least log `ARC error command has more than 16 words` so the cut is visible.

## Why it matters
A silent cut looks like a rules bug: the test passes the value, the game never sees it, and the dump shows the default.

---

## Done in ARC 0.6.6 (5 Nov)

- Commands may now have up to **64 words** (was 16) and 4,095 characters. `loadout` with all 18 words arrives
  whole, so `designcoat` is no longer needed (it does no harm to keep it).
- A command that is still too long is **not run at all**, and ARC prints
  `ARC error command has more than 64 words, not run: loadout ...` (or `... longer than 4095 characters ...`).
  A test that sees an `ARC error` line fails, so a cut can no longer look like a rules bug.
- The script splitter used to cut lines at 1,023 characters without a word; that is fixed too.
- Self-test: a 21-word command reaches the game whole.
