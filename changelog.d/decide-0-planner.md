### session — the planner places the devices; the plan says what they wait for

- **One planner, eight devices.** Every device (high-pass, mono bass, glue, saturation, tilt, limiter with its needles,
  dither, low) proposes its machine fields and says what it reads; the planner is the one place the machine decides, and
  the session places the devices when the first measurement ends — by the pump and by a sidecar's facts alike, where
  before only a fixture's seam did. The machine layer each target gets is the previous placement's, bit for bit.
- **The plan** (`SnapshotView::plan`): its status (None, Pending, Stopped, Ready, Unavailable), what the project's devices
  read and which of it has not ended, the one waited for first with the device that reads it and its progress, whether
  the panel is read-only, whether the machine layer is an imported file's, and each device's plan — what held it back
  and which fields its target decided. Its key hashes every input; the planner runs again only when it moves.
- **What a master waits for.** The tempo is waited for where a glue compresses (cd's machine glue, or a person's) — no
  longer on cdDynamic, whose glue is off — and the needles at the project's ceiling. With the panel open a master the
  session decides is refused (`Rejection::PlanPending`) until they have ended; with it hidden it is taken and measures
  them first, a needed tempo ahead of the optional findings. Its recipe is the project when it was asked for: a target or
  edit made while it waits no longer leaks into it. A stopped needles job ends the wait.
- **Device edits wait for the plan**: the table's Unplaced columns now also stand for a plan that waits (the panel
  read-only); export needs only the placement.
- **`adoptMachine`**, a new command: after an import the file's machine layer is kept, the planner's decisions for the
  file's target on this source are shown beside it (`machineDifferences`), and `adoptMachine` takes them, a person's
  layer kept.
- **The one EQ stage.** The high-pass, tilt and low each write their own band of the chain's EQ; the curve is drawn from
  the bands written — the previous curve bit for bit, core's response of those bands, and the engine's output on sines.
- Facts `PlanWaiting` and `RejectedPlanPending`, the terms of the needles and of the eight devices, in ru and en.
