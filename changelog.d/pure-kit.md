### session · tools — the pure kit: stateless answers for a shell's UI thread

- **`felitronics::session::Kit`** (`Kit.h`): a published fact as text (`Text::write` behind the codec's fact reader); a
  typed number read for a field (`Text::parse`, a bare number negative on a travel below zero, the knob's grid in decimal
  digits, the command's domain; the slope a whole multiple of 6); a knob's travel, position ↔ value on its grid, and heat
  against the config's window (`[edit]` green, `[hpf] comfort`, `[tilt]`/`[low] normal`); mono bass's zones; the EQ curve
  preview with the shelves' peak against `warnDb` (the EQ stage's own `writeEq` / `eqCurve` / `eqFinding`); a low-end dB
  curve from band energies. Allocation-free, no state.
- **C ABI** `fc_kit_text`, `fc_kit_parse`, `fc_kit_travel`, `fc_kit_position`, `fc_kit_value_at`, `fc_kit_heat`,
  `fc_kit_mono_zones`, `fc_kit_mono_zones_at`, `fc_kit_eq_curve`, `fc_kit_low_end_curve` and the `FC_SESSION_KIT_*`
  constants, appended to the v1 manifest; exported by the fcsession module. `FC_SESSION_ABI_VERSION` is unchanged (the
  release moves it).
- The plan's comfort and zones advice reads the kit's comparison; no fact, snapshot or sound changes.
