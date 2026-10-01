### session — pre-release review fixes

- **The hum is not "too short" when the analyzer never ran.** A hum analyzer the session refused before any work (no
  price for the programme, or memory it may not take) carries no number; the hum and the wandering hum are now not
  measured with that refusal's own reason (`Memory`, `Unsupported`) instead of `TooShort`. The observation's codes are
  tied to `analysis::HumReason` by `static_assert`.
- **A between landing always delivers its master.** A `TargetBetweenAchievable` landing whose solver side were not a
  number keeps its verdict without `belowLufs`/`aboveLufs` and returns the file, instead of failing the job as a
  contract breach. No known path makes such a side: `LandingSearch` takes a side only from a finite, ceiling-safe pass
  (`felitronics_mastering_landing_search_tests` drives a between verdict and checks both).
- **The ABI gate's reset is authorised, not self-declared.** `tools/session-abi-append-only.mjs` accepts a new declared
  base only from its own list of authorised resets — exactly v0.6.0 over a manifest that declares none; any other base
  (v0.6.1, a removal under a fresh base) is refused. `tools/session-abi-v1.txt` now holds the full compiled surface of
  the v0.6.0 base (lines only added), and the manifest generator reads `uint32_t a, b;` as two fields, each with its
  own offset.
- **Two compatibility slots leave the C boundary** (owner, 01.10). `fc_session_measurement_storage` loses
  `reservedBytes` and `reserved`: 88 bytes, `workspaceBytes` at 24 and every later field 8 bytes earlier.
  `fc_session_capabilities`' base size is 40 with `leanSummary`: a 32-byte record is `STRUCT_TOO_SMALL` like any record
  below its base, and `FC_SESSION_CAPABILITIES_V1_BYTES` is gone. A build still accepts sizes from a record's base up to
  its own.
- **Docs**: the header and `docs/SESSION.md` state every record's base size; the `[bands]` comment in `engine.toml`
  describes the device's tick.
