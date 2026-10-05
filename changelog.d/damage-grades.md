### session — the damage grade is the shell's to ask, a queue of its own; the max modes by ear, without the guard (owner, 04.10)

- **`gradeDamage { masterId }`** (new command): grades a master kept as a job of its own — its `damage` events Pending,
  then the result, its progress, a row of the snapshot's new `damageJobs` (job, master, `Waiting` or `Running`,
  progress). **No grade starts by itself** after a master any more: the shell asks, and decides when and which. Refused
  for a master not kept (`UnknownMaster`), a grade already asked (`DamageQueued`, fact 137) or a settled damage
  (`DamageSettled`, fact 136: graded, or no plan for the source), or past the queue's room of 32 (`DamageQueueFull`, fact
  138); a grade a cancel stopped, or a turn refused for room, may be asked again. A grade is a measurement, not an edit: a project's journal does not hold it.
- **One at a time, in the order asked; a new master never ends a grade** (owner: "it is a process of its own"). A master
  job has the pump first; a new master parks the running grade — its walks freed before the master allocates, so the
  master's price is as before — and the grade waits first, to start again from its beginning when its turn comes back.
  `Superseded` is no longer said. A waiting grade holds an entry alone (`DamageJobEntry`); what its walks need to start
  is its master's own (`MasterRows`: the walks' plan and the parameters it was delivered with). At its turn its walks'
  room is checked against the capacity, and refused there, said: `Unavailable`, `Memory`, its line.
- **Only its master's cause ends a grade, at once**: `cancel` of its job id (Cancelled: the cancel's fact, its line, the
  `damage` event; a running grade's walks freed — the cancel's `releasedBytes`) and `forget` of its master (new
  `MeasurementReason::MasterForgotten`, "the master was deleted", no line; the forget's `releasedBytes` are a running
  grade's walks), each with one last `damage` event and nothing of it after; the queue moves on. A new source ends every
  grade too (`MasterForgotten`): each grade's last word in the load's own batch, in queue order, stamped with the source
  it belonged to and ahead of anything of the new one — the queue's fixed room (32) keeps them within the batch.
- **The max modes by ear** (owner: both went too far while PEAQ heard nothing): the PEAQ guard is out of the master's
  loop — a max master is a landing and a delivery, its damage graded as any master's when the shell asks — and out of the
  config (`floorOdg`, `guardStepDb`, `guardSteps`); `MaxStop` `Guard`, `GuardUnmet`, `Unguarded` and facts 609, 610, 614
  stay in the ABI, no longer said, and a max master's stop comes from its landing alone. The budgets are 0.5 dB (clean)
  and 1.75 dB (dense) of the active P95 — the budget-curve ladder's medians: −13.05 / −11.22 LUFS on the home mixes,
  −12.04 / −10.53 on the Cambridge corpus; the max rows' manual starts are −13 and −11.
- **The max floor at −14 LUFS** (`[landing.max] floorLufs`; owner: "always pulled up to −14"), whichever target the mode
  sits on: a first landing whose file (BS.1770) stands under it by more than the landing's tolerance is landed again
  there, with no budget. Delivered on the floor, new `MaxStop::Floor`: its verdict the mode and the level alone
  (`MasterMaxFloor`, 616), its numbers in a line for the log only (`MasterMaxFloorDetail`, 617: where the first landing
  stopped, the floor, the P95 the limiter took beside the budget — no claim of what held the first landing). A floor out
  of reach says what held it (`TruePeak`, `Passes`), never `Floor`.
- **The gate's level, worded as what it is** (`MasterLandingGate`, 601, both languages): "−9.0 LUFS without the quiet
  parts, −9.8 LUFS by the file's standard reading." — it was "by the loud part", but the number is the master's mean over
  the blocks the source's gate admits, not its loudest place. No argument moves.
- **`FC_SESSION_ABI_VERSION` 11**: the manifest appends the command's wire record, the rejections and facts, the reason
  and its term, `DamageJobState`, `DamageJobEntry`, `Snapshot.damageJobs`, `MaxStop::Floor` and facts 616, 617;
  `SURFACE[11]` adds no entry point. Manual sound does not move (the WAV contract's PCM is unchanged); a max master's
  does, and the `2026-10` defaults' config and sound versions move with it, restated in place; the event pins, the text
  corpus and the contract recordings move with the grades no longer started by themselves (the end-to-end scenario, which
  asks its grade as a page does, keeps its facts digest bit for bit); a new contract scenario (`damage-grades`) masters
  twice and grades both, and the contract scripts gain `release master`.
