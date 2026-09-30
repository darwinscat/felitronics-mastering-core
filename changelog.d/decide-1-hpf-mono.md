### session — the high-pass and mono bass decide from the first phase; defaults 2026-10

- **The high-pass stands always** (owner decisions 3.2–3.4): every target, a quiet input included, at max(the cutoff the
  sure lowest note allows, the floor — now 32 Hz on every target), never above 50 Hz. A note is sure at 2 dB over the
  occupancy line, 10 % of the frames, above 20 Hz and 3 s in all; a programme under 10 s is not searched. The cutoff is
  found on the chain's own high-pass response so that the note loses exactly the target's `noteLossDb` (1 dB, club 0.3),
  unrounded. "Nothing below the note" is gone, with `hpfAlways`, `hzDefault`, `nothingBelowNote` and `shortConfidence`.
- **Mono bass by the harm** (owner decision 3.5): the loss the low end takes folded to mono, where the bass sounds, at the
  target's crossover (120 Hz, vinyl 150). Under 1 dB placed; 1 to 3 dB placed with the number; above 3 dB left out, a
  person's switch obeyed and flagged (`plan.monoBass.againstMachine`); a loss it cannot weigh leaves it out with its
  reason. The stereo correlation no longer counts as a device switch.
- `PlanView::hpf` / `::monoBass`, the typed findings (the LowEnd result no longer publishes `hpfFloorRequired`, which nothing read); `HeldBack::Measured` and `::Quiet`; `PlanText` and ten facts (ru,
  en) for their report lines.
- **Defaults `2026-10`**: the numbers above change a master, so they are a new set.
- **The whole file's spectral wall** in the forensics result: `wall.*` without a channel index, beside `wall.*[c]` — the
  analyzer's aggregate, appended to the result's numbers.
