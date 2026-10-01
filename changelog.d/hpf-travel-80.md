### session — the high-pass knob travels to 80 Hz; the machine's top stays 50 Hz

- **Owner, 01.10.** `[hpf] hzMax` is the knob's travel alone and goes to 80 Hz (a voice with a guitar from a microphone
  takes a cut that high); the new `[hpf] machineTopHz = 50` is the machine's top. `HpfCut::Top`, its cutoff and the
  report's "the cutoff stopped at 50 Hz" read the machine's top; no machine cutoff moves. The schema holds the new key
  required, finite and on the travel (above `hzMin`, at most `hzMax`). The comfort window is unchanged, so the knob's
  field is red from 50 to 80 Hz. A hand's cutoff keeps its domain (above 0, under the source's Nyquist).
- **Moved records:** the 2026-10 sound version (updated in place) and the pure kit's corpus pin (`KitTests.cpp`,
  `tools/wasm/session-check.mjs`): the high-pass travel's end and the positions along it.
