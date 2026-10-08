### session · mastering — the waterfall: a person's share of the peak work for the glue, the saturation and the needles' cut

- Three fields a person may set (ABI 16): the glue's `share`, the saturation's `share` and the limiter's `cutShare` (the
  needles' clipper), each 0…1 with no step — the share of the peak work at the landing that stage takes; the limiter
  takes the rest. They are a person's fields alone: the machine's layer holds 0 and is never read, and a wish moves no
  other field. A value outside the domain is `OutOfDomain`, one that is no number `NotFinite`, each naming its field
  (terms `glueShare`, `saturationShare`, `limiterCutShare`, in ru and en); `revertEdits` drops the wish.
- A master the session decides with a wish steers its landing: each pass reads the four takes — the glue's P95 through
  its mix, the saturation's usual cut, the clipper's P95 over what it clipped, the limiter's P95 on its active windows (a
  max mode's at its budget) — and moves each wished stage to its share of their total: the glue's and the saturation's
  mix, the clipper's cut in dB within `[limiter.peakClipper] manualDomain`. Shares that add up past 1 are scaled to 1. A
  pass that moved the stages is never a candidate: every render before it is forgotten, and the next pass is aimed from
  its budget excess, lowered by the peak the move adds. At most four moves, none once fewer than four passes are left.
- The saturation's take is foreseen from the clipper's peak counters and solved in one move — its mix first, then its
  drive by at most 6 dB a move — with a secant on the foresight's own error from the last move; the first (probe) pass
  steers the glue and the saturation too.
- Once the saturation's mix is at 1, the waterfall raises its drive, on the knob's dB, up to `[saturation]
  steerDriveMaxDb` (new, 10 dB, by ear, not measured on the ladder) — never a person's drive. The comfort's red warns a
  person; it does not stop the machine.
- A zone at 0 % leaves its stage out of the chain: a glue or a saturation share of 0 (at or under 0.0005; the page sends
  shares rounded to 0.001) does not sound, and a cut share of 0 takes the needles' clipper out. A zone asked 0 % whose
  stage is out reached it.
- The master's report says what came of it (`MasterReport.waterfall`, absent without a wish): per zone the share asked
  (the limiter's: the rest), the share reached, the dB it took, the setting the landing steered to (the mixes, the cut in
  dB), the saturation's steered drive, and what stopped it (`WaterfallStop`: `Reached`, `MixAtOne`, `MixAtZero`,
  `CutAtEnd`, `CutAtZero`, `ComfortRed`, `NotSounding`, `Passes`, `Rest`, `NoWish`, `DriveAtCeiling`), the four takes'
  total and the extra passes the steering took. The exported as-worked report prints each zone's asked, reached, dB and
  stop; its table of stop names is checked against the enum at compile time, so `driveAtCeiling` is named rather than
  read past the table's end.
- A master with no wish is byte for byte the master before the waterfall, on every target. The `2026-10` sound version
  moves in place: the waterfall's keys and Maximum · nuke are added, and no number of an existing target changes.
