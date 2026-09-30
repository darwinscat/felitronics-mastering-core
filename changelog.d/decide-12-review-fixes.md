### session — a clipped source does not wait for its needles, the vinyl report names what departs, a half-measured finding is not measured

- **A clipped source is ruled out before its needles.** `needlesAnswer` tests the confirmed clips per minute before it
  asks for the needles, so a source clipped ≥ 10 times a minute answers `Clipped` while the needles job still runs, and
  the limiter's plan reads no needles for `Clipped` or `LittleNeed` (decision 3.1: wait only for what is needed).
- **The vinyl report says what departs, with numbers.** `MasterMedium` keeps `ready` and gains one flag per rule of the
  medium (`foldDeparts`, `cutDeparts`, `ceilingDeparts`, `needlesDeparts`) with the chain's numbers and the rule's;
  `MasterReportText::vinylDepartures` gives one line per departed rule, published after `masterVinylDeparts`. New facts,
  appended: `masterVinylNoFold` (82), `masterVinylFoldDeparts` (83), `masterVinylNoHighPass` (84),
  `masterVinylHighPassDeparts` (85), `masterVinylCeilingDeparts` (86), `masterVinylNeedlesDeparts` (87), ru and en.
  The codec's `MasterMedium` grows by the new fields (generated); the pinned rendering corpus moves.
- **Not measured is not "not found" and not 0.** Polarity with only one of its two readings, edge silence with one edge
  invalid, a DC offset read on one channel of two, and "already limited" with a PLR that is not dense and the clips not
  counted are now `NotMeasured` with the missing input's reason; the edge line no longer prints an unmeasured edge as
  "0.0 s". The unused low bits already required every channel.
- **The vinyl needles warning keys on the medium.** `needlesAgainstMedium` reads `vinyl`, not `noClipper`.
- **Docs.** `SESSION.md`: cancelling the needles job of a waiting master renders it without the clipper
  (`limiterUnmeasured`); cancelling the source's measurement it waits for ends it.
