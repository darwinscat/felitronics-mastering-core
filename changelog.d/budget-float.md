### session — the loud target's limiter budget 7.5 dB, a budget in dB with a fraction (owner, 04.10)

- **`[landing] limiterBudget` takes a fraction**: `quietDb`, `middleDb` and `loudDb` are numbers of dB from 1 to 60, a
  whole number or not (`Config::Landing` holds them as `double`; the order quiet ≤ middle ≤ loud still holds). The
  verdict that names the budget (`MasterLandingBudget` 600, `MasterLandingOverBudget` 602) prints it with a decimal only
  where it has one: «7,5 дБ», «7 дБ».
- **The loud step is 7.5 dB, was 10**: `limiterBudget = { quietDb = 4, middleDb = 7, loudDb = 7.5, middleLufs = [-10, -8] }`.
  1058 renders of 23 songs without a budget put the limiter's cost and the PEAQ damage breaking together near 4, 6.5 and
  8 dB of the active P95. Asked for −6 LUFS, the median song at 7.5 dB reaches −7.83 with an ODG of −1.46, 3 of 23
  "annoying"; at 10 dB it reached −7.36, −2.30 and 11 of 23. Targets at −8 LUFS and quieter do not move.
- **Sound moves** for a master louder than −8 LUFS whose limiter would take more than 7.5 dB: it is held there. The
  `2026-10` defaults' sound version moves, restated in place (no project of those defaults has been saved); the event
  pins, the damage pin of the demanding master and the contract recordings move with it.
