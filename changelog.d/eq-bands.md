### session · tools — the EQ bands by hand, the glue's P95 point

- **The EQ bands** (`Device::Bands`, appended as device 8; `BandsFields`: `body`, `mud`, `forward`, `brightness`,
  `air`, dB): five static bands of the one EQ stage from the new `[bands]` config section — body a 160 Hz bell (Q 0.7),
  mud a 300 Hz bell (Q 0.8, a cut only: −6…0 dB), forward a 3 kHz bell (Q 0.7), brightness and air high shelves at 8 and
  12 kHz (Q 0.6) — in bands 3–7, each knob ±3 dB by 0.1 dB within a ±6 dB domain. A person's only: the machine leaves
  them at 0 dB on every target; a band at 0 dB is no band, so a master without them is the master made before (bit for
  bit, held against the previous `writeEq`). No tick: the plan's `bands.on` is true where any band sounds. No dynamics
  and no norm advice yet; the curve's `warnDb` still judges tilt and low only.
- **Everything a device gets**: `EditDevice` / `RevertEdits` alternatives (a gain outside its domain refused
  `OutOfDomain` on its own field, named by the new terms `FieldBandsBody` … `FieldBandsAir`; the device's term
  `DeviceBands`, ru «Полосы EQ»: «Тело», «Грязь», «Вперёд», «Яркость», «Воздух»), the project file's `[bands]`, the
  codec (`Devices.bands`, `DevicePlans.bands`, defaults for older snapshots), the summed `eqCurve` and the kit's curve.
- **Capabilities**: `FC_SESSION_DEVICE_EQ_BANDS` (256); `FC_SESSION_DEVICES_ALL` keeps the eight devices before it, so a
  shell that does not know the bands is not offered them. C++ `kAllDevices` is all nine.
- **Kit**: the five gains are kit fields (`FC_SESSION_KIT_FIELD_BANDS_*`, travel, parse, no heat window);
  `fc_kit_eq_curve_bands` takes `FC_SESSION_KIT_EQ_BANDS_PARAMS` (12) — the seven of `fc_kit_eq_curve` and the five
  gains. Appended to the v1 manifest; `FC_SESSION_ABI_VERSION` is unchanged (the release moves it).
- **The glue's P95 point**: `GlueFinding::p95DetectorDb`, the input's short-term P95 on the detector's scale (P95 +
  `[glue] detectorOverP95Db`) — the level the threshold stands on and where the static curve takes `upToDb`, for the
  transfer curve's dot. Set whenever the threshold is.
- **Config**: the sound version of the `2026-10` defaults moves with the added `[bands]` numbers (none changed; a
  2026-10 project has no band and sounds as before).
