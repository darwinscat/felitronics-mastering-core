### session · mastering — the saturation's type, picked by hand; felitronics-core v0.57.0; the bricks never include the session

- **felitronics-core v0.57.0** is the pin and the floor (CMake, `tools/wasm/build.sh`): its `WaveShaper::Shape` appends
  Tube = 4, Transistor = 5, Transformer = 6 and Tape = 7; Tanh, Atan, Cubic and Asym are bit-identical to v0.56.0.
- **The saturation device has a type** — `SaturationFields::type` (`SaturationType`, the core's shapes in their order and
  values), a person's field next to the knobs: tanh, tube, transistor, transformer or tape. The machine never picks one:
  its layer holds the config's `[saturation] shape` (tanh), so nothing sounds different until a person picks a type, and
  the defaults label stays `2026-10`. Atan, cubic and asym stay the config's (research): an edit or a project file that
  gives one by hand is refused whole, `NotOneOf` at the type's place (field 4), with the field's name «Тип сатурации» /
  "Saturation type". Like every device edit a pick sounds (the device's tick follows a touched field), is counted by the
  warning before a change of target (fact 81) and is taken back by that change. The project file writes
  `type.hand = "tape"`; the snapshot and the commands carry the type as a number (codec and `.d.ts` from the one schema;
  a snapshot or command without it decodes as tanh, untouched).
- **The type's names** are catalogue terms (`terms.saturationType`), Russian first: Мягкая / Soft, Ламповая / Tube,
  Транзисторная / Transistor, Трансформаторная / Transformer, Ленточная / Tape. The report's «сатурация срезала пики до N
  дБ» is measured on the stage as before, whatever the type.
- **fc_master**: `FC_SHAPE_TUBE` 4 … `FC_SHAPE_TAPE` 7; the shape's upper bound moves to 7 there, in `fc_session`'s master
  parameters and in the master job's check; `fcore_master clip.shape` takes the four new names.
- **The bricks never include the session.** `DeclaredBudget.h`, the declared-memory harness the mastering suites shared
  with the session's, moves to `tests/` (namespace `felitronics::declared`), and `tools/lint/check-brick-includes.mjs` —
  with its self-test and controls planted in a real file of every brick, run in CI — refuses any file under
  modules/mastering, analysis_offline, tempo or storage that includes a session header or source or links the
  session's target.
