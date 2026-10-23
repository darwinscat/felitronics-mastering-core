### session — a master's bar runs once from 0 to 1, and a max master's no longer sits full

- The bar expects 4 renders of a manual master and 12 of every max mode (`[progress.master]` `expectedPasses` 4,
  `expectedPassesMaxClean` / `MaxDense` / `MaxExtreme` / `MaxNuke` 12; owner, 23.10). They were 3, 7 and 6, set on 06.10,
  before the second landing of "cleaner, not louder" (09.10). Measured on 5 songs (v0.22.1): a manual target takes 1–3
  passes (median 2); a max mode with the page's zone shares 8–12 in all — the landing alone, the landing with the zones and
  the steering (median 10–11). A max master's bar reached 0.8 at its 7th render and crept from there; now it fills over the
  whole search, and a manual master's fills more evenly.
- Never stuck at the end: the renders take 0.8 of the bar evenly up to the expected count, and each render past it takes
  half of what is left, spread along its walk (12 of 12 at 0.8, 13 at 0.9, 14 at 0.95). Before, a render past the count
  took 1/(count + k) of what was left. The bar never moves back — a master that waited for its measurements starts its
  renders where the wait left the bar, where before it dropped to 0 — stays under 1 until the master is done and is
  exactly 1 then. The landing without the wishes and the landing with the zones count into one bar and one pass number.
- A shell's time estimate: the master's total work units are never fewer than the units done and one render more, so
  past the expected renders the estimate no longer runs out (before: 6718 units done of 6349 on a max master's 9 renders).
- The config's version moves; the sound does not.
