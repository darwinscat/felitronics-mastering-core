<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Third-party notices — felitronics-mastering-core

The repository itself is AGPL-3.0-or-later (see `LICENSE`). Everything in it is original code; nothing is
vendored and nothing is fetched but felitronics-core itself (`CMakeLists.txt`), which records its own
third-party notices (the optional pffft backend) in its `THIRD_PARTY_NOTICES.md`.

The wasm modules are built with Emscripten (`tools/wasm/build.sh`); the runtime it links into them (musl,
libc++, emmalloc) is Emscripten's, under its own permissive licences.
