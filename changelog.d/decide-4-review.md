### session — the first review of the plan: no wait for ever, sentences of what sounds, a calibrated glue

- **A dropped waiting master hands the needles back.** A hidden master waiting at its own ceiling and then cancelled —
  or stopped with the source's measurement, or ended by a contract fault — left the needles at its ceiling: the
  project's plan waited for ever and the open panel refused every master. The needles are asked for again at the
  project's ceiling. A master stopped with the measurement now says so under its own job's id (fact `cancelled`).
- **A sentence states what sounds.** `plan.hpf` and `plan.monoBass` are the planner's proposal; each says whether that
  proposal is what sounds (`sounding`: Proposal, Hand, File, Off; `soundingHz`). `PlanText` gives the planner's
  reasons only for its own values; a person's or a file's value is named as that (`hpfByHand`, `hpfKept`, `hpfOff`,
  `monoBassByHand`, `monoBassKept`).
- **The glue's scale is calibrated to music** (`[glue] detectorOverP95Db = 1.5`): the threshold stands 1.5 dB above
  P95 + the travel's offset, so that the knob is the gain reduction really taken on the loud places — the median of 11
  mixes: 1.27 dB at 1.25, 2.61 at 2.6, 3.02 at 3 (it was 1.68, 3.24, 3.70). cd's 2.6 dB now takes 2.6 dB. The sound
  version of defaults `2026-10` moves.
- **Mono bass is not judged on a part of a piece**: where the low-end run holds only the first 10.9 minutes the verdict
  is `Incomplete` — left out, with its own sentence (`monoBassIncomplete`).
- `[stages]` is documented as the defaults layer of the ticks, not the chain's topology.
