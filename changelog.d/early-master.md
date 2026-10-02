### session — Master as soon as the loudness and the true peak are known; edits before the machine has measured

- **`Measured1` comes with the loudness and the true peak** (owner, 02.10), before any of the source's own runs: the
  devices are placed then and a master is taken. The infra-low reading,
  forensics and stereo no longer stand before it.
- **The high-pass and mono bass read the low end.** The high-pass's cutoff needs the 120 Hz run, mono bass's tick the run
  at the target's crossover (`DevicePlan::needs`). With the panel open a master waits for them (`PlanPending`); with it
  hidden it waits in its own job, its recipe's machine fields placed again as each run ends — it gives the same WAV bytes
  as a master asked after everything ended. Mono bass's tick may change the chain's geometry when its run ends; the
  wait's end checks the demand again.
- **A field not measured yet** holds a placeholder (the floor, mono bass off), its bit in `DevicePlan::pending`
  (appended), the device's `heldBack` `Pending` (appended), and a waiting plan states the card's line
  `deviceUnmeasured` (fact 510): "not measured yet", with no number. The machine fills the field when its run ends.
- **Edits from placement on**: `editDevice` and `revertEdits` are taken in the table's Unplaced columns (a plan still
  waiting), and `plan.readOnly` now means the devices are not placed. A touched field is the person's; the machine never
  moves it. An import and `adoptMachine` still wait for the plan.
- **No file carries a placeholder**: while any device field is not measured yet, a project export and an import are
  refused with `PlanPending` — an import also while the file's own target has such a field on this source. The
  refusal's words no longer name the master: they say the measurements are still running.
- **Order**: the two low-end runs lead, then the findings; a needed tempo goes ahead of a finding, never ahead of a
  low-end run a device reads.
- **Progress** in the `Analyzers` phase moves by `[progress.analysis.weights]` (the infra-low run weighs `lowEnd120`):
  the stereo pass is a sliver, crest a long stretch.
