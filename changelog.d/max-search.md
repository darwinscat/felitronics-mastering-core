### mastering · session — a max master's limiter budget search aims at its crossing: it settles in about half the passes (live, 05.10)

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
- **The max modes alone** (`LoudnessRequest::budgetAimsAtCrossing`, set by the session for a max master): a manual
  landing keeps the previous search to the bit (`previousBudgetClamp`) — its passes, its file, the WAV contract's PCM
  (9a601c4c5e044b00), the end-to-end scenario's digests and the report tests' damage pin are all unchanged.
