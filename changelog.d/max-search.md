### mastering — the limiter budget's search aims at its crossing: a max master settles in about half the passes (live, 05.10)

- **`LandingSearch::budgetClamp`** no longer steps back three times the excess with no render inside the budget, nor holds
  its secant within the middle three fifths: it aims at where the limiter's active-window statistic crosses the budget —
  from the lowest drive over it, back by its excess over the statistic's slope (about a dB per dB of drive above the
  knee, measured between the two lowest drives over it, else 0.75), and half the proof's resolution more; with a safe
  render inside the budget below, the chord of their excess (the statistic is convex in drive, so the chord crosses at
  or under the true crossing), and, once within the proof's resolution of that render, a test just under the resolution
  above it, so a render over the budget there proves it at once. A far, steep over end counts half each time a render
  lands inside the budget against it (the Illinois rule). The budget's proof (`kBudgetResolutionDb`) is unchanged.
- **Passes, 11 home mixes and 6 Cambridge-MT mixes** (median, before → after): max clean 10 → 5, max dense 8 → 6; every
  stop is still the budget's (or the −14 floor's). The site's live case (max clean, 11 passes, 44.5 s) is the shape this
  fixes: a 20 dB step back and the climb.
- **Manual targets**: where the budget does not bind nothing moves (allStreaming on the corpus: the same passes and
  files); where it binds (club, youtubeMusic on the corpus) the passes are the same or one fewer and the file within
  0.01 LU — the landing settles at another drive inside the proof. The WAV contract's PCM is unchanged; the demanding
  master of the report tests lands 0.06 dB of drive higher (its damage pin restated); the end-to-end scenario's master
  (allStreaming, held short by the budget) settles in 6 passes instead of 7, at −14.81 LUFS instead of −14.71, so its
  facts, PCM and WAV digests move (`tools/wasm/scenario-parity.mjs`; the plan does not).
