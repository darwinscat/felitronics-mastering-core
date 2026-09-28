<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Third-party notices — felitronics-mastering-core

The repository itself is AGPL-3.0-or-later (see `LICENSE`). Everything in it is original code; nothing is
vendored. Two repositories are fetched (`CMakeLists.txt`), each resolved by the product that owns the graph:

- **felitronics-core** — AGPL-3.0-or-later; it records its own third-party notices (the optional pffft backend) in
  its `THIRD_PARTY_NOTICES.md`.
- **felitronics-toml** (<https://github.com/darwinscat/felitronics-toml>, pinned `v0.3.0`) — the TOML parser, writer
  and schema reader `felitronics::session` reads its config with, and `felitronics_toml2cpp`, which compiles the config
  into the library at build time. MIT License, Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Its headers are
  compiled into `felitronics::session` and into the `fcsession` wasm module, so a distribution of either carries its
  licence notice: the `LICENSE` file of that repository.

The wasm modules are built with Emscripten (`tools/wasm/build.sh`); the runtime it links into them (musl,
libc++, emmalloc) is Emscripten's, under its own permissive licences.
