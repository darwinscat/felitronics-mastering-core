<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. -->

# Law 10's volatile-store pin, worked on `analysis::StereoSums`

felitronics-core's law 10 ([`DSP-ARCHITECTURE.md`](https://github.com/darwinscat/felitronics-core/blob/main/docs/DSP-ARCHITECTURE.md) §2) states FP contraction for
every tree that compiles felitronics code — `-ffp-contract=on`, stated rather than inherited — and, as a rule, a
pin that is not an override: where a number is gated against a reference that never contracts, store each product
before adding it. This is the case that rule came from, moved here with the stereo band
(`modules/analysis_offline/include/felitronics/analysis/StereoColumns.h`). The text is as it stood in core's
architecture document.

   **AND A PIN THAT IS NOT AN OVERRIDE: `analysis::StereoSums` STORES its products before it adds them**.
   Its answers are gated against JavaScript, which never contracts, so the right number is the unfused one:
   `mid += m*m` fused on arm64 under this law's own `on` reads a playhead width of 0x3fdffffff7c00010 where the
   spec reads ...0012. A `volatile` store per product holds under every contraction mode, gcc's `fast` included,
   and needs no pragma. (Only the mid and side terms can move: a product of two float32 samples is exact in
   double, so `ll`, `rr` and `lr` could not be fused into a different number — they are stored anyway, so the
   rule reads "every product" and nobody has to re-derive which ones are safe.) The pin holds in every
   cell measured — Apple clang 21 arm64, gcc 14.2 x86-64 and gcc 14 arm64, `on` and `fast`, O2/O3, with and without
   `-march=native` — while the removed pin fuses exactly where each compiler fuses: one expression under `on` on the
   arm64 rows, two statements only under gcc's `fast` (with FMA available). The test target keeps the tree's `on`,
   which catches the natural removal. The two-statement form is visible only to a build with gcc-style cross-statement
   fusion AND FMA — no target, CI row or tier of either repository is one (felitronics-mastering-core's
   `fcore_measure` and wasm modules are `-ffp-contract=off`), so it is defended and not gated; the out-of-tree NULL's C++ side is built
   `-ffp-contract=fast -march=native` precisely to see it.
