<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# felitronics-mastering-core

[![CI](https://github.com/darwinscat/felitronics-mastering-core/actions/workflows/ci.yml/badge.svg)](https://github.com/darwinscat/felitronics-mastering-core/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](#)
[![core: felitronics-core](https://img.shields.io/badge/core-felitronics--core-brightgreen.svg)](https://github.com/darwinscat/felitronics-core)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](LICENSE)
[![Latest release](https://img.shields.io/github/v/tag/darwinscat/felitronics-mastering-core)](https://github.com/darwinscat/felitronics-mastering-core/tags)

The mastering chain and the offline programme analyzers of the Darwin's Cat products, on top of [felitronics-core](https://github.com/darwinscat/felitronics-core).

| Module | What |
|---|---|
| `mastering` | the chain — gain, EQ with dynamic points, mono-bass, compressor, clipper, true-peak limiter, dither — block-independent, with an offline renderer, a target-loudness solver and delivery at another rate |
| `analysis_offline` | whole-programme analyzers: programme report, source forensics, hum, low end, band bursts, band crest, peak excursions, clipped runs, waveform peaks and the stereo band |

`tools/` holds the two C ABIs over them (`fc_master`, `fc_probe`), their native CLIs, and the wasm build.

## Build

```sh
cmake --preset desktop && cmake --build --preset desktop && ctest --preset desktop
```

Licence: AGPL-3.0-or-later. Third-party code: `THIRD_PARTY_NOTICES.md`.

Part of the Felitronics line by [Darwin's Cat](https://darwinscat.com).
