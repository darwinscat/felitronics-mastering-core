### session — the needles' clipper cuts an amount off the peaks

- **The two class numbers are amounts, not thresholds** (owner, 30.09): the peak clipper takes at most 3 dB (short
  needles) or 1.5 dB (between) off the peaks and the limiter does the rest. Its threshold stands max(0, need − cut)
  above the ceiling, the need the input's (`plan.limiter.needDb`). Before, 3 and 1.5 dB were thresholds above the
  ceiling, so on a need of 7.9 dB the cautious class clipped 6.4 dB and the short one 4.9. A person's `manual X` is
  X dB off the peaks the same way. Where the need is not known, or need − cut lies beyond the limiter's 12 dB working
  range, the clipper stays off rather than cut more than its amount.
- `[limiter.peakClipper] shortOverDb`, `betweenOverDb` → `shortCutDb`, `betweenCutDb` (`config::PeakClipper` likewise);
  `plan.limiter.overDb`, `proposedOverDb` and the vinyl report's `overDb` carry the amount. The limiter's lines and the
  vinyl departure say "up to X off the peaks" (`{cut}`), the knob reads "Cut off the peaks".
- The defaults stay `2026-10` (not yet released); their sound version moves.
- The core carries only the current defaults table: the empty `previous` slot is gone.
