### session — the glue is a parallel compressor: its mix, 40 % by default, a knob 0…100 % (owner, 05.10)

- **The glue's mix** — the compressed share of its output, the dry signal under it — is a field of the glue device:
  `[glue] mix` 0.4 wherever the glue is on (the machine's cd glue and a person's tick alike), a person's knob over
  `[glue] mixDomain` 0…1; the page's slider runs 0…1 by 0.2 (`mixRange`, `mixStep`: 0, 20, … 100 % — a finer step is
  placebo), the core takes any share in the domain as written. It is snapshotted and edited as the glue's other knob
  (`GlueFieldsValue/Touched.mix`, `editDevice`, `revertEdits`), kept by the project (`mix.hand` in its `[glue]`), reset
  by a change of target, said by the plan as it sounds (`GlueFinding.mix`), named by its term (`FieldGlueMix`:
  «Смешивание» / "Mix"), and parsed, snapped and travelled by the kit (`Kit::parse`, `travel`: 0…1 by 0.2).
- **At 100 % the glue is the downward compressor it was**, to the bit: `felitronics_session_glue_saturation_tests`
  pins v0.16.0's cd master of its test mix and gets it back with the mix set to 1. `[compressor] mix`, the 1 the glue
  was written at, is no longer read; it stays in `engine.toml` and `config::Compressor` until its removal is decided.
- **What moves**: every master whose glue engages — cd's (the machine glues there) and any master where a person
  ticks the glue — now compresses 40 % in parallel, so the glue takes less off the loud places; no other master
  moves: a glue out of the chain leaves the stage at 1, as before (a blend of the input with itself at another share
  would round it). The config's sound version moves (the config golden and the event pins are restated in place).
- **ABI 12** (`FC_SESSION_ABI_VERSION`): the glue's mix in the codec and the wire, the field term; no entry point.
