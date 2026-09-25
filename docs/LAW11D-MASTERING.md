<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
<!-- Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. -->

# Law 11d, worked on the mastering chain and its C ABI

felitronics-core states law 11d — *memory that cannot be had is not a refusal: exhaustion is fatal, and the core
publishes its demand instead* — in its [`DSP-ARCHITECTURE.md`](https://github.com/darwinscat/felitronics-core/blob/main/docs/DSP-ARCHITECTURE.md) §2, in general terms.
This is the case the law was written from, moved here with the chain: `mastering::MasteringChain`,
`mastering::TargetLoudnessSolver` and the `fc_master` C ABI (`tools/fc_master_abi.h`), with the measurements
behind each clause and the suites that pin them. The text is as it stood in core's architecture document.


   **11d. MEMORY THAT CANNOT BE HAD IS NOT A REFUSAL: EXHAUSTION IS FATAL, AND THE CORE PUBLISHES ITS DEMAND
   INSTEAD.** Law 11b refuses an ARGUMENT that cannot be honoured. It does not refuse memory that cannot be had —
   and on the wasm tier it could not: under `-fno-exceptions` a throwing `new` that fails aborts inside the call
   (emsdk 6.0.9: `bad_alloc` → `abort()` → a JavaScript `RuntimeError`). Natively `bad_alloc` escapes the call — or
   ends the process where it meets a `noexcept` boundary, and the two are one line apart: `MasteringChain::prepare`
   allocates the EQ engine itself (an escape), then calls `EqEngine::prepare`, which is `noexcept` and allocates its
   scratch (a `terminate`). So exhaustion is **outside the refusal contract on every row**: it ends the module's
   usefulness and is never answered with `false` — save where a third-party backend throws (the NAM backend,
   in felitronics-guitar-core, catches what its preparation throws and stays unprepared), a refusal this law
   neither asks of the other modules nor forbids there. This is the explicit exception to 11b, and it was chosen over nothrow
   storage plus a status in every allocating module on measured grounds: what the core holds is a constant of its
   CONFIGURATION plus a small fraction of the programme, a caller can read it before committing (below) and stay
   clear of exhaustion by arithmetic, and a later move to the nothrow form is ADDITIVE — a new status code, no
   struct moves — while making it now would cost every module the desktop products share. Two obligations replace
   the refusal:

   * **THE DEMAND.** An allocating call on the worker path can state, before it is made, a bound on how much of the
     heap its OWN requests will occupy at once — not what the object already holds — computed by the very functions
     its `prepare()` sizes itself with, so the bound cannot drift from the allocation; the C ABI forwards it
     (`fc_master_need`). In 64 bits. What each number bounds is written where it is defined, because "at once" is
     not one formula: a call that keeps what it asks for is bounded by the sum of its requests, exact on a FRESH
     object (one already prepared keeps storage that still fits); a call that builds and frees per pass
     (`TargetLoudnessSolver::solve`) by one pass. REQUESTED bytes, not a promise that a heap can serve them:
     allocator headers, the standard library's own alignment (MSVC's STL, in a release build, asks for
     `sizeof(void*) + 31` more on a block of 4096 bytes or more), fragmentation and the runtime's growth step are
     the caller's margin.

     **THE FUNCTION THAT SIZES IS THE FUNCTION THAT VALIDATES, and that is what makes "cannot drift" a
     construction rather than a promise.** Each allocating `prepare()` in this tree now has a static
     `storageFor(...)` beside it that answers FALSE on exactly the arguments the preparation refuses and
     otherwise fills in the element counts the buffers are built from — and `prepare()` calls it as its own
     gate. A budget is that function's `bytes()`. A stage cannot change what it refuses, or what it allocates,
     without changing both at once. Where a composite needs a stage's LATENCY to size something of its own, the
     stage publishes a static `latencyFor(...)` on the same terms and its own `prepare()` runs through it, so the
     number a budget reads and the number the prepared object reports are one expression.

     **A CALL THAT REFUSES MAY HAVE ASKED FOR PART OF ITS BOUND ON THE WAY — except where it can be decided for
     nothing, and then it must be.** `MasteringChain::admits()` reaches the whole verdict, every stage's included,
     without a single allocation, so `fc_master_create` refuses an impossible geometry having touched no heap at
     all. It used to ask for 394 456 bytes on its way to saying no on the default geometry, and 1 668 312 at
     sixteen channels and an 8192-sample quantum — on the tier where an allocation that cannot be served is
     not a refusal but the end of the module, which is what this law is about.

     **THE DEMAND IS A NUMBER, NOT A PERMISSION.** `fc_master_need` answers what a call would REQUEST and does not
     consult the handle's state: a solve and a configure are both budgeted while a stream is in progress, though
     either would be refused with `FC_ERR_STATE` in that moment. The cost of a call does not depend on when it is
     made, and a caller deciding whether to reset a stream and re-configure needs the number precisely then. The
     one exception is the call that has no handle to ask: `fc_master_need_create` is a DRY RUN, returning every
     status the create would return before its first allocation, because the configuration it is handed has never
     been admitted anywhere and whether it is admissible is the question only that entry point can answer — and
     because a budget of 0 must keep meaning one thing.
   * **A MODULE WHOSE CALL NEVER RETURNED ANSWERS EVERY STATUS CALL WITH "DISCARD ME".** The runtime does not stop a
     module that aborted; it answers the next call with objects wherever the abort left them. Measured on v0.30.0 in
     wasm32: after an abort inside `fc_master_solve`, `fc_master_process` answered `FC_OK` at the search's pass-1
     gain — +12 dB in that replay, the `initialGainDb` it asked for, which `MasterAbiTests` repeats — and seven such
     aborts in all (`kMaxHandles − 1`) left the handle table full for good. So a boundary that cannot outlive an
     abort marks every call in progress and, finding the mark on entry, answers `FC_ERR_POISONED` for good and
     touches nothing — ahead of every other check, for every handle, and for a re-entrant call too, which it cannot
     tell apart. Entry points that read no instance state (build identity, the defaults writers) stay callable.

   Not promised: that a demand will be admitted, that anything survives exhaustion, or that a native host which
   catches `bad_alloc` holds a usable object. RT law 2 is unchanged — `process()` allocates nothing — so none of
   this reaches the audio path. Gated: the C-ABI suites pin the poison (natively, through an escaped exception) and
   every published budget against the bytes its call requests, byte for byte — over a matrix of four rates, three
   widths and NINE topologies for the chain's own storage, every optional stage absent on some row of it (with
   only one stage moving, a budget that charged for an EQ engine a chain never builds was green on every row),
   with a counter that installs EVERY form of `operator
   new`, the over-aligned one included (without it the EQ engine's 331 KiB — the largest single request a create
   makes on the default geometry — is invisible to the counter and both sides of the comparison silently omit
   it). The suites also pin that
   `admits()` is `prepare()`'s own verdict, that a refused preparation allocates nothing, that every stage's
   `latencyFor()` is the latency the prepared stage reports, and — by null, over three topologies and across a
   chain moved to another rate and quantum — that re-preparing
   a chain, which now re-uses its EQ engine instead of building a second one, does not move a sample. The re-entry
   suite runs on the wasm tier too; the abort path itself is measured, not gated.
