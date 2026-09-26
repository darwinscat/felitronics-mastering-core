### tests — split invariance, one harness for every analyzer and the mastering path

`tests/split_invariance.h` and two suites (`felitronics_analyzer_split_invariance_tests`,
`felitronics_mastering_split_invariance_tests`) ask every streaming class what a caller that cuts its own chunks
needs answered first: is every output the stream decides — scalars, rows, event lists, histograms, traces, tap
streams, statistics — the same bits under one call, blocks of 64, 480 and 4096, seeded ragged cuts of 1..8192 with
zero-length calls, a second prepared `maxBlock`, the width timelines the contract allows (a clock-only gap in the
middle, at the head and at the end, a narrower stretch), and a finish on (and a read before finish of) a prefix?
For all twelve analyzers, the chain in two topologies with and without taps, `OfflineRenderer` at eight blockings
and ten short lengths, and the solver's `LoudnessMeter` + `ReferenceTruePeakMeter` pair, it is: 360 rows, on Apple
clang arm64, gcc 14.2 x86-64, MSVC 19.44 and wasm32. A one-ulp nudge at every call boundary is caught for every
class, and `ReferenceTruePeakMeter::truePeakLinearBlock()`, the last call's peak by definition, is reported as not
invariant. So are a `ProgressClock`'s events, which follow the pieces the work is cut into while the audio does not.
Measured and not fixed: `BandCrest::process (nullptr, 0, n)` is refused, where law 11a lets a clock-only call pass a
null plane array; `StereoBandBursts` and `PeakExcursions` latch their width on an empty call, where law 11d makes
`n == 0` a no-op.
