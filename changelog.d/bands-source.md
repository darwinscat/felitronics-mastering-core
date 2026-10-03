### session — the seven EQ knobs' geometry and names come from felitronics-bands

- **felitronics-bands v0.1.0 is the one source** of the named EQ bands (`tilt`, `low`, `body`, `mud`, `forward`,
  `brightness`, `air`). The root `CMakeLists.txt` fetches it by its pinned tag (`FELITRONICS_MASTERING_BANDS_TAG`), or
  takes a sibling `../felitronics-bands` / `FELITRONICS_MASTERING_BANDS_DIR`; a product that declares
  `felitronics_bands` first is the one used. `tools/wasm/build.sh` reads `FELITRONICS_BANDS_DIR` (CI passes the
  checkout its configure resolved).
- **Geometry.** `bands.toml` is embedded beside the config as its third document: tilt's pivot, low's corner and Q, and
  each band's type, centre or corner and Q are read from it by the config schema (`Document::Bands`) and built into the
  chain from it. `engine.toml` keeps only the knobs (`band`, `domain`, `normal`, `hard`, `step`); a `freqHz`, `q` or
  `type` written back there is an unknown key, and a band one document has and the other lacks is refused. The numbers
  are the ones engine.toml held: no master moves (the WAV contract and every PCM hash hold).
- **Versions.** `bands.toml` is part of both config versions (all of it is sound), so the config's versions moved with
  no number changed: the 2026-10 defaults' sound version is restated in place. `Config::bind` and
  `Config::versionsOf` take the bands document as a third argument; the two-argument forms use the one compiled in.
- **Names.** The catalog no longer writes the seven knobs' names (`terms.field` tiltDb, lowDb, bandsBody…bandsAir;
  `terms.device` tilt, low): each such term prints felitronics-bands' `text/<lang>.toml` `name`. The text gate refuses a
  copy in the catalog (`FromBands`), a language of the core whose bands text lacks a name, and a `languages.toml` that is
  not the catalog's languages (`LanguagesDiffer`). "EQ bands", the device of the five, stays in the catalog.
