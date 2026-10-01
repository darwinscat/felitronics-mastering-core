### session — the master report states its verdict, its crest and honest bounds

- **The landing's verdict is a fact.** `MasterReportText::landing` states one fact per landing status, published with
  the master ahead of the miss: solved says the achieved loudness against the target and the landing's tolerance
  (`MasterLandingSolved`, 88 — never "hit" without numbers); unreachable, pass limit and between say why against the
  tolerance (`MasterLandingUnreachable` 89, `MasterLandingPassLimit` 90, `MasterLandingBetween` 91), the achieved number
  and the gap staying the miss's own line (`MasterLandingMiss`/`Above`, 11/23); a technical failure says so
  (`MasterLandingFailed`, 92). An unavailable or cancelled landing says none. A shell composes no verdict of its own.
- **The crest's line goes out once, from one source.** `MasterReportText::crest` is now published with the report when
  the job settles the crest (joined inside the job, or unavailable); a crest still pending is said by the late join, as
  before, and a settled one is never said twice.
- **The limiter promises no exact cut.** `{cut}` in `limiterShort`, `limiterBetween`, `limiterManual` and
  `masterVinylNeedlesDeparts` is a cap, `Bound::AtMost` ("≤ 1.5 dB"), and the lines say the landing decides how much;
  `limiterLittleNeed` names the need as the one at the target.
- **`DefaultsConverted` (9) is retired.** Nothing converts a project, so its shape and message are gone; the id stays
  reserved (the ABI manifest freezes it, no other fact takes it), renders as its number and is refused by the snapshot
  decoder.
- The recordings move by the new facts alone: four contract scenarios gain the verdict and the report-time crest line
  in one event record each (with their hashes in the manifest); the event-test and text-corpus pins and the scenario's
  facts digest move with them. No ABI or version change.
