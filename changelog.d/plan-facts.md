### session — the plan states its reasons; a contract with the panel open while the plan waits

- **The plan publishes its reasons.** `PlanView::facts` (a `BoundedList<PlanFact, 14>`: a fact and the device it is said
  of, held in place, no heap) carries what `PlanText` states of the plan's own findings, so a shell shows each device
  card's "why the machine set it so" without composing a line — and without the config numbers some of them carry
  (`shortSeconds`, `littleNeedDb`, `longPlrDb`, the dither's bit limit). A Ready plan states every line `PlanText`
  gives, in the order of `Device` and, within a device, of `PlanText`'s members: the high-pass; mono bass, its polarity
  warning, its coverage; the glue's refusal, fallback tempo and held release; the limiter, the warning beside a manual
  threshold, vinyl's ceiling, needles and top; the dither. Where the plan names what a master waits for (`awaited`,
  `awaitedBy`), the waiting fact `PlanWaiting` (29) — the device, the measurement, `awaitedFraction` as its percent —
  comes last and follows the progress between plans; a plan that is not Ready states that fact alone, or nothing. A new
  plan states its reasons anew. One source: the planner calls `PlanText`, nothing else composes them.
- **On the wire a plan's fact is every other fact's**: `{"device": n, "fact": WireFact}`, the fact written by the one
  encoder the events' facts now share (`FactId` and `args`, each argument whole). The codec and `.d.ts` come from the one
  schema, which learns `T[<=N]` (a bounded list: `ReadonlyArray<T>` on the wire) and `Fact` (`WireFact`). The decoder
  refuses a plan fact with an unknown id, an argument out of its range or a person's text (a snapshot holds none).
- **The session grows by one `PlanView` of facts** (3032 bytes on wasm32): the declared memory, the snapshot JSON and the
  recordings move with it, and by nothing else.
- **A new contract scenario, `plan-pending`** (native and wasm byte-identical): the panel open on cd while the tempo the
  glue reads is still measured — `plan.status` Pending, `awaited` / `awaitedBy` / `awaitedFraction`, the waiting fact
  alone; a master refused whole with 35 `PlanPending` and the snapshot unchanged; the waiting fact following the
  progress; then the plan Ready with its reasons, and the master taken.
