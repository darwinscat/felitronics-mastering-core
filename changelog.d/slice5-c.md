### session · mastering — no render under the ceiling still delivers the file, marked

- **A landing whose measured renders all break the true-peak ceiling delivers its file** (owner, 01.10; a caller's chain
  without the limiter): `LandingSearch` keeps the gentlest render measured — the smallest overshoot of the ceiling, the
  nearer loudness on a tie — restores it on the reserved last pass when the search ended elsewhere, verifies it and
  delivers it. The verdict stays fact 89 with the true-peak ceiling named; the master is marked: `LandingSummary` and
  `MasterReport` gain `peaksAboveCeiling` (appended, plain fields), and `MasterPeaksAboveCeiling` (98, ru/en) says the
  true peak and the ceiling beside the verdict. The miss's line (which says the true peak held) is not said of it.
  Delivered means under the ceiling except in exactly this marked case — the decoder's invariants say so. A ceiling-safe
  landing is unchanged.

### analysis · session — hum is a line heard in the quiet passages too

- **A line present only while the music plays is music, not hum** (owner, 01.10). `HumDetector`'s quiet gate leaves the
  candidate bands out (so a hum cannot censor itself), which let a passage where a loud 50/60 Hz line plays over an
  otherwise quiet programme count as quiet — a 60 Hz musical tone was reported as hum. The detector now pools the
  frames where the whole programme is quiet, the candidate bands included, and a stationary line must show there too
  wherever that pool holds two frames: otherwise `valid = false, LineOnlyWithMusic` (11), which the session answers as
  the hum not found. Only those frames INSIDE the programme speak — strictly between its first and last frame above
  the gate — so a dithered lead-in, a tail after the hum's source stops or room tone at an edge never vetoes a hum loud
  enough to keep every frame it plays in above the gate; a pause inside the song does. A line through the pause, loud or
  faint, is still hum; where no still frame lies inside, nothing contradicts the line and it stands. The span is kept
  in the one walk over the frames: the detector holds two more rows of the stretch's width per channel (the pool and
  the still frames waiting for the next programme frame).

### ci · tests — gcc 14 on arm64

- **`felitronics_session_master_abi_tests` compiles with `-ffp-contract=off`, as the library does.** The unit runs its
  own `LandingSearch`, so it emits the same inline mastering code as the library; under the directory's
  `-ffp-contract=on`, gcc 14 — the first gcc that contracts under `on` (gcc 13 treats it as off) — fused multiply-adds
  into its copies on arm64, the linker kept those for the whole binary, and the library's landing re-measured its
  render differently and refused it: 7 checks red on gcc 14 arm64 only. The gcc 14 CI row now runs on
  `ubuntu-24.04-arm` too (`ubuntu-arm64-gcc-14`; the x86-64 row keeps its name).
- Two faults that row found on the way: a lambda's `Reader r` shadowing the command `r` in `Wire.cpp` (gcc's
  `-Wshadow`, an error), and a glue test asking the rules for a target named `vinyl` — the target is `lp` — whose empty
  `optional` was read as a row; the fixture now says an unknown target as a failed precondition.
