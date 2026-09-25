<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Changelog

## v0.1.0 — 2026-09-25

### mastering · analysis_offline · the C ABIs — split out of felitronics-core

The mastering chain (`felitronics::mastering`), the twelve offline programme analyzers
(`felitronics::analysis_offline`), the two C ABIs with their native CLIs and suites, and the wasm build with
every native-vs-wasm comparison, from felitronics-core v0.51.0; target names, namespaces and header spellings
are unchanged (`<felitronics/analysis/...>`, `<felitronics/mastering/...>`). `felitronics::analysis_offline`
now owns its include root. New here: the law-11b case for `OfflineRenderer` and the chain through core's shared
harness (`felitronics::test_support`), the analyzers' half of the math-policy gate, a header-hygiene TU that
includes every public header (`PeakExcursions.h` and `StereoBandBursts.h` were outside core's), and this
repository's det-math zone and manifest (`tools/lint/`), linted by core's `check-det-math.mjs --satellite`.
