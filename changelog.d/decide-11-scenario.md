### session — the scenario end to end, in independent sessions

- **One scenario, one master** (`felitronics_session_scenario_tests`): load, measure, plan, a hand with a 1.25 dB edit,
  master, export, import into another session, master — the same snapshot JSON, recipe, facts, defaults and sound
  versions, PCM and WAV bytes. On the same file: a same-core and a foreign-core import keep the file's machine layer and
  sound alike; `AdoptMachine` gives the fresh session's master; step budgets of 1, 7 and a large one give the same bytes;
  a cancelled waiting master and a stale needles job publish nothing; import → master → release → forget, repeated with
  refusals between, keeps `liveBytes` flat after the first cycle.
- **Native and wasm, one scenario.** The test prints its input hash, its versions and the digests of the plan, the facts,
  the PCM and the WAV; `tools/wasm/scenario-parity.mjs` checks the wasm run against the native lines.
