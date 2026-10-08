### session — a master is never refused for its timing: the masters' queue

- A master asked while another is made or queued, while the previous master's PCM waits for its transfer, before the
  first measurement ended, or with the panel open while the plan waits for what its devices read, is queued with the
  project as it is at that command — its recipe, target and a person's fields included — and starts by itself, in the
  order asked. An edit or a change of target after the command does not reach it. What it is asked with (its budget
  resolution, source, revision, a clipped source's consent) is checked at the command. Eight wait at most; a ninth is
  refused `Busy`.
- The snapshot lists the recent master jobs (`Snapshot.masterJobs`, at most 16, the oldest finished one dropped first): a
  `MasterJobRow` of job, state and position, the state `Queued` (position: how many jobs are ahead of it, the one being
  made counted), `Running`, `Done`, `Failed` or `Cancelled`. `cancel` of a queued job lists it `Cancelled`, `forget` of
  its id takes it out, and a load clears the queue and the list. `canMaster` says whether a master asked now would be
  taken, at once or queued.
- A refusal for the source's content stays a refusal. On a source whose first measurement ended without its mandatory
  readings (silence) a master is refused `NotMeasured`, and one queued before ends `Failed` with it. Where the first
  measurement stopped before its first readings, which come only if a person continues it, a master is refused by the
  state table's cell (`NotMeasured`), a queued one included.
- The state table's cells do not move: the queue stands before the table. A master the session decides is no longer
  refused `PlanPending` while the open panel's plan waits; it is queued.
