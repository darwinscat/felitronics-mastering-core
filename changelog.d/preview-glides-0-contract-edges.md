### analysis_offline — the two contract edges the split-invariance suite found, fixed

Neither moves a number. `BandCrest::process (nullptr, 0, n)` is accepted now: law 11a lets a clock-only call pass a
null plane array, so a null array is refused only where a plane will be read (a null array at a live width still is).
`StereoBandBursts` and `PeakExcursions` latch their width on the first call that carries AUDIO, below the `n == 0`
exit and the plane check: law 11d makes `n == 0` a no-op, and an empty or a refused call used to latch it, so an
empty width-1 call ahead of the programme turned every stereo call after it into a refused width change. A width
that differs from a latched one is still refused, an empty call included. The split-invariance suite asserts both
now, with their negative halves, where it printed them as notes; every cut row is unchanged and INVARIANT.
