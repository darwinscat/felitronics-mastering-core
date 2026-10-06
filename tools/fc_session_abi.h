// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#ifndef FC_SESSION_ABI_H
#define FC_SESSION_ABI_H

// fc_session — the C ABI over `felitronics::session`, the mastering session: the surface a shell that cannot link C++
// (a browser worker, through the fcsession wasm module) talks to the brain through. It is the DECLARATION only: the
// implementation is tools/wasm/fc_session.cpp, which compiles natively as well (EMSCRIPTEN_KEEPALIVE degrades to a
// plain extern "C"), so its guards run under ctest, ASan and UBSan like everything else. fc_master and fc_probe stay
// what they are — the chain's and the analyzers' own surfaces, for their parity suites and CLIs.
//
// ====================================================================================
// THE FACADE IS THIN — fc_master's law, kept here
// ====================================================================================
// A desktop application links `felitronics::session` AS C++, past this file entirely, so nothing may be reachable
// only through it. What it adds is exactly what a C boundary needs and the C++ API does not: handles instead of
// pointers, a status per call, argument checks on addresses a page computed, and the poison. No arithmetic and no
// state of the session's own: every answer is the session's, read out of it.
//
// ====================================================================================
// VERSION HISTORY — ONLY ADD
// ====================================================================================
// Once published, a signature, value, constant or field offset stays. New entry points and new
// values may be appended. tools/session-abi-check.mjs compares a compiled probe to the v1 manifest
// on every tier, including wasm32; its mutation control must fail on a changed or missing line.
//
// FC_SESSION_ABI_VERSION IS A FLOOR, as FC_MASTER_ABI_VERSION is: after the first release that carries this surface,
// each batch of additions that lands together in one release — entry points, fields, values — moves the number up by
// one and adds one row below, so a page's "module version >= page version" gate is its protection against calling an
// export the module lacks. The generated snapshot.d.ts and snapshot.mjs state the number read from this line. The
// manifest's `define FC_SESSION_ABI_VERSION=4` is therefore checked as "at least 4", like a boundary struct's size;
// a lower number is a change. The manifest itself only grows from its declared base (`base v0.6.0`): CI refuses a pull
// request that removes or edits a line under the same base, or declares an older one.
//
//   fc_session   the surface
//   ----------   ------------------------------------------------------------------------------------------------
//   1            capabilities/config creation and demand, commands/load/project, step, events and snapshot copies;
//                measurement and needles demand, measurement queries, summaries, the sidecar load and its audio
//                attachment (no release carried fc_session before them, so they are version 1).
//   2            v0.4.0, Decide: leanSummary appended to fc_session_capabilities; query kinds MasterReport and MasterAxes,
//                a master's Momentary/ShortTerm and the spectrum choice; the plan's and the landing's snapshot fields.
//   3            v0.5.0: the saturation's type (SaturationType, a snapshot and command field, and the master parameters'
//                clipper shapes 4-7, tube to tape); the plan's reasons on the wire (PlanView::facts, PlanFact).
//   4            v0.6.0, THE MANIFEST'S NEW BASE (owner, 2026-10-01): the pure kit (fc_kit_*), the EQ bands (device 8)
//                and their tick; the dead entries left the manifest (no project, snapshot or file of an older version
//                exists, and the one consumer vendors the exact core) — live ids keep their numbers; two compatibility
//                slots left the C boundary: the measurement storage's reserved fields (88 bytes now) and the 32-byte
//                capabilities record (its base is 40, leanSummary included).
//   5            v0.7.0, slice 5: a rejected answer carries its fact last (RejectedAnswer.fact, ContractAnswer.fact — the
//                field, its refused number and the domain it left, facts 180 and 181); a null in editTarget clears the
//                field (the target row's number again); fc_kit_saturation_curve; the plan's limiter settings and dither
//                shaping, a glue out of the chain with its numbers, and the snapshot's eqOnlyCurve; the gentlest render
//                delivered when none keeps the ceiling, marked (LandingSummary/MasterReport.peaksAboveCeiling, fact 98).
//                Not on this surface, in the same release: the C++ Answer's value, low and high, and the hum
//                detector's LineOnlyWithMusic (11), which the session answers as the hum not found.
//   9            v0.14.0: a landing held short says so — facts 600 (the limiter's budget that held it), 601 (the level
//                landed on the source's gate beside the file's BS.1770 reading) and 602 (no render kept the budget), and
//                each render over the limiter's budget marked in the pass log (LandingPass.overBudget); the master's
//                damage (MasterReport.damage — PEAQ in windows against the chain at rest, the loudness range's change;
//                facts 603-607, the damageGrade terms), graded after the master by a job of its own (the damage event,
//                DamageChange; Snapshot.damageJob and damageProgress; the phases Reference and Damage; MeasurementReason
//                Superseded and NoJobId); Phase.stepFraction; fc_session_storage appends releasedBytes past its 32-byte
//                base — a shell sends size 40 only to a module whose abi is 9 or more.
//  10            v0.15.0: the maximum loudness modes — a target's loudnessMode (manual, maxClean, maxDense) on its row,
//                in editTarget (TargetPatch.loudnessMode, null gives the row's back) and a project's target layer, the
//                mode in effect in the snapshot (Snapshot.loudnessMode); a max master's report says its mode, what
//                stopped it and the guard's steps back (MasterReport.loudnessMode, maxStop, guardSteps; facts 608-615,
//                the loudnessMode terms, the field term FieldTargetLoudnessMode), and its damage is the guard's grading.
//  11            v0.16.0: the damage grade asked by the shell — the command gradeDamage (masterId), its own job and damage
//                events, refused DamageSettled or DamageQueued (facts 136, 137); grades run one at a time in the order
//                asked, a new master parks the running one, forget of its master or a new source ends it
//                (MeasurementReason::MasterForgotten); the snapshot's list of grades (Snapshot.damageJobs, DamageJobEntry,
//                DamageJobState); a max master without a guard (its damage graded as any master's; MaxStop Guard,
//                GuardUnmet and Unguarded and facts 609, 610 and 614 no longer said) and pulled up to the modes' floor
//                (MaxStop::Floor, facts 616 and 617). No entry point.
//  12            v0.17.0: the glue in parallel — the glue device's mix (GlueFieldsValue/Touched.mix, in editDevice and
//                revertEdits), the plan's share as it sounds (GlueFinding.mix), the field term FieldGlueMix. No entry
//                point.
//  13            v0.18.0: analyzer progress, damage wait reasons, delivery modes for already-mastered sources and the
//                read-only as-worked TOML (`fc_session_worked_report_*`); each landing pass's active P95 and reason,
//                and MasterCommand.budgetResolutionDb. All appended.
#define FC_SESSION_ABI_VERSION 13u
#define FC_SESSION_SIZES_V1_BYTES 12u
#define FC_SESSION_CAPACITY_V1_BYTES 24u
#define FC_SESSION_STORAGE_V1_BYTES 32u
#define FC_SESSION_STEP_UNITS 16u
#define FC_SESSION_MIN_RATE_HZ 8000u
#define FC_SESSION_COMMAND_JSON_BYTES 4096u
#define FC_SESSION_ANSWER_BYTES 32768u
#define FC_SESSION_DEVICE_HPF 1u
#define FC_SESSION_DEVICE_MONO_BASS 2u
#define FC_SESSION_DEVICE_GLUE 4u
#define FC_SESSION_DEVICE_SATURATION 8u
#define FC_SESSION_DEVICE_TILT 16u
#define FC_SESSION_DEVICE_LIMITER 32u
#define FC_SESSION_DEVICE_DITHER 64u
#define FC_SESSION_DEVICE_LOW_SHELF 128u
#define FC_SESSION_DEVICES_ALL 255u
// Appended after v0.5.0: the EQ bands (five static bands a person turns). FC_SESSION_DEVICES_ALL keeps the eight devices
// before it, so a shell that does not know the bands is not offered them; a shell that draws them adds this bit.
#define FC_SESSION_DEVICE_EQ_BANDS 256u

// HOW MANY SESSIONS ONE MODULE INSTANCE HOLDS AT ONCE (law 5: the capacity is stated, not discovered). A create past it
// answers FC_SESSION_ERR_EXHAUSTED.
//
// AND HOW MANY IT ISSUES IN ITS LIFE. Each slot issues FC_SESSION_SLOT_GENERATIONS handles — one per create — and then
// RETIRES rather than wrap its generation, which would hand an old handle's number to a new session and let the stale
// handle destroy it. After FC_SESSION_MAX_HANDLES x FC_SESSION_SLOT_GENERATIONS creates in one module instance every
// create answers FC_SESSION_ERR_EXHAUSTED, for good: the way on is a new module instance.
#define FC_SESSION_MAX_HANDLES 8u
#define FC_SESSION_SLOT_GENERATIONS 16777215u

#include <stdint.h>
#include <fc_master_abi.h>

#ifdef __cplusplus
extern "C" {
#endif

// A session handle: a slot of the module's table and that slot's generation, packed into 32 bits. 0 is never a handle,
// so a zeroed variable in JavaScript is refused; a destroyed handle stays refused, because the slot's generation has
// moved on. A handle is not an address and means nothing outside the module instance that issued it.
typedef uint32_t fc_session;

// THE STATUS OF EVERY CALL THAT CAN FAIL. One malformed call has one answer, in the argument order below:
// POISON -> outputs -> handle -> inputs -> overlap -> minimum capacity -> session (details below).
typedef enum fc_session_status
{
    FC_SESSION_OK            = 0,
    FC_SESSION_ERR_POISONED  = 1,   // an earlier call into this module never returned — see POISON below
    FC_SESSION_ERR_NULL      = 2,   // a required pointer was null
    FC_SESSION_ERR_ALIGNMENT = 3,   // a pointer that is not aligned for what it points to
    FC_SESSION_ERR_SPAN      = 4,   // an address range wraps, or leaves the wasm heap; native addresses have no heap bound
    FC_SESSION_ERR_HANDLE    = 5,   // 0, never issued, already destroyed, or from a previous generation of its slot
    FC_SESSION_ERR_EXHAUSTED = 6,   // FC_SESSION_MAX_HANDLES sessions are alive already, or every free slot has retired
    // The session's own refusals (felitronics::session::Status), passed through:
    FC_SESSION_ERR_FP_ENVIRONMENT = 7,  // the calling thread flushes to zero, reads subnormals as zero, or does not round
                                        // to nearest — restore the environment (or call from another thread)
    FC_SESSION_ERR_CONFIG_VERSION = 8,  // page build's config hash differs from this module's
    FC_SESSION_ERR_CAPABILITIES = 9,   // invalid ceiling, rate or device bits
    FC_SESSION_ERR_MEMORY = 10,         // create demand exceeds the supplied ceiling
    FC_SESSION_ERR_TOO_SMALL = 11,      // caller output capacity is insufficient
    FC_SESSION_ERR_CONTRACT = 12,       // invalid transfer value
    FC_SESSION_ERR_OVERLAP = 13,        // output overlaps another output, an input or a retained master PCM
    FC_SESSION_ERR_TRAP = 14,           // shell maps a thrown wasm trap/abort to this status; see POISON
    FC_SESSION_ERR_STRUCT_TOO_SMALL = 15, // size is below the v1 record size
    FC_SESSION_ERR_STRUCT_TOO_LARGE = 16, // size exceeds the record this build understands
    FC_SESSION_ERR_NO_SOURCE = 17,        // export: the session has no source
    FC_SESSION_ERR_NOT_PLACED = 18,       // export: the first measurement has not placed devices
    FC_SESSION_ERR_STALE = 19,            // transfer identity no longer names pending audio
    FC_SESSION_ERR_UNKNOWN_MASTER = 20    // no completed master is kept under this id
} fc_session_status;

// ====================================================================================
// POISON — the one flag, and it is for ever
// ====================================================================================
// Under -fno-exceptions a heap that cannot serve a request does not come back as a status: the module ABORTS inside
// the call (law 11d, felitronics-core docs/DSP-ARCHITECTURE.md). The abort does not stop the module — emscripten lets
// the page call again, with every object wherever the abort left it — and such an instance does not fail, it LIES
// (fc_master measured one answering FC_OK at the gain of a search that never returned). So every entry point that
// returns a status marks a call in progress and clears the mark only on a normal return. Finding it set on entry
// means an earlier call never returned — an abort, a trap, or natively an exception that escaped — and from then on
// every such call answers FC_SESSION_ERR_POISONED, writes nothing and touches nothing. The way back is outside the
// instance: a new module instance, and the work replayed into it. fc_session_abi_version reads no state and stays
// callable.
//
// THE MODULE IS NOT RE-ENTRANT, and the poison is what says so: an entry point called while another is still running
// (from a new_handler, from anything the runtime runs inside an allocation) cannot be told apart from the first call
// after an abandoned one, and is answered FC_SESSION_ERR_POISONED — for good.

// STRUCT LAYOUTS are measured by a compiled probe, never inferred from source. Capabilities are
// input from the shell. heapCeilingBytes is an exact integer >= 0 and < 2^53; maxRateHz >= 8000.
// offeredDevices is a bit set of FC_SESSION_DEVICE_*. The session enforces all three, in C++ too.
// leanSummary is 0 or 1. With 1,
// fc_session_summary_* leave every master's heavy rows out — the limiter and peak-clip traces, the crest rows and mask,
// the waveform buckets — and say so (masterRowsIncluded false); a master's scalars, pass log and cost sections stay.
// One master whole is a query, kind MasterReport. fc_session_snapshot_* is the same either way.
typedef struct fc_session_capabilities
{
    uint32_t size;
    double heapCeilingBytes;
    uint32_t maxRateHz;
    uint32_t offeredDevices;
    double largestFreeBlockBytes;
    uint32_t leanSummary;
} fc_session_capabilities;

typedef struct fc_session_sizes
{
    uint32_t size;
    uint32_t jsonBytes;
    uint32_t rowBytes;
} fc_session_sizes;

// All boundary structs are size-prefixed: capabilities (40), sizes (12), capacity (24), storage (32), measurement
// storage (88) — each its base size at v0.6.0.
// Set size to the caller's sizeof before EVERY call, including output queries. It is preserved.
// Fields are append-only. A build accepts every size from a record's base size up to its own, writes only that prefix,
// and documents the default of each absent later field. A new field never changes the meaning of a base prefix;
// appended reserved fields default to zero and are ignored. Today one record has grown past its base: the storage
// record, 40 bytes since 9 (releasedBytes; a 32-byte caller is not written past its size); every other record is its
// base size. size < the base size is STRUCT_TOO_SMALL; size > this build's size is STRUCT_TOO_LARGE.
// No C++ implementation records or binary row structs cross this C boundary.
typedef struct fc_session_capacity
{
    uint32_t size;
    double heapCeilingBytes;
    double largestFreeBlockBytes;
} fc_session_capacity;

typedef struct fc_session_storage
{
    uint32_t size;
    uint32_t rejection;             // session Rejection; zero when priced, no mutation or events
    double bytes;                   // allocating demand, independent of current capacity
    double largestBlockBytes;       // largest allocation; import uses a conservative bound
    double liveBytes;               // current declared bytes; caller can assess live - released + bytes
    // Appended (v0.14.0): live bytes the command frees before its first allocation — the running damage grade's walks: a
    // master parks it, a cancel of its job or forget of its master ends it. The heap must hold liveBytes - releasedBytes
    // + bytes, as the command's own check counts it. Written where the record's size reaches it.
    double releasedBytes;
} fc_session_storage;

// Detailed source measurement demand. Counts include one retained result copy and its codec buffers.
// Codec buffers are the snapshot transport's JSON metadata plus binary f64 rows. A C++ plain-JSON
// export has a separate exact Codec::encodedBytes query; it is not reserved for every web measurement.
// Needles have a separate job demand.
typedef struct fc_session_measurement_storage
{
    uint32_t size;
    uint32_t rejection;
    double sourceBytes;
    double resultBytes;
    double workspaceBytes;
    double copyBytes;
    double codecBytes;
    double allocatorBytes;
    double loadPeakBytes;
    double workPeakBytes;
    double peakBytes;
    double largestBlockBytes;
} fc_session_measurement_storage;

typedef enum fc_session_step_state
{
    FC_SESSION_MORE = 0,
    FC_SESSION_DONE = 1
} fc_session_step_state;

// ARGUMENT ORDER for every call below: poison; each output in signature order (null, alignment,
// span, byte-capacity alignment, struct size); handle if present; inputs in signature order (null, alignment, span); overlap; session.
// Capacity values are session checks; row byte-capacity alignment belongs to its output check.
// Entry refusals write and allocate nothing. A call poisoned during work may have allocated
// already, but publishes nothing. Create checks for a free slot before the session checks.
// The session's command rejection is an OK call with a rejected JSON answer; import's declared
// parse work may allocate on rejection.
// Byte buffers have alignment 1, uint32/handles/sizes 4, capabilities/capacity/storage and doubles 8, planar pointer
// tables alignof(pointer). Null is allowed only for a zero-byte rows buffer or a zero-length input.
// Every output is disjoint from all other buffers. A query/copy pair describes the same batch only
// while no command or step intervenes. No pointer into session memory survives a call except the
// explicitly scoped master audio view below, which expires at the next module call or memory.grow.
// Every entry that writes a caller buffer, the kit's included, refuses an output inside any live
// session's retained master PCM with ERR_OVERLAP, so a stale view never becomes a destination. A call that may free
// that PCM (load, load_measured, attach_audio, command) refuses an input inside it the same way, before freeing anything.
//
// Additive v1 measurement data: reading payloads include a fixed source-frame grid, window reasons,
// detailed clip rows and total/stored counts. Reports live in snapshot measurements with per-field reasons.
// Events include source/revision/state/phase and deterministic work. continueMeasurement resumes a stopped
// measurement; snapshot canContinueMeasurement and measurementResumeState describe availability.
// A wasm trap cannot return through C: the shell catches it as ERR_TRAP and discards the instance.
// Every later status call returns ERR_POISONED, publishes nothing, and cannot destroy even a handle.
// Recovery is a new instance, load, then importProject. abi_version alone remains callable.
uint32_t fc_session_abi_version (void);

// Demand is available BEFORE creation, including when the ceiling cannot afford it. Requested
// bytes, not allocator overhead. Config halves carry the version in the generated declaration.
fc_session_status fc_session_create_bytes (const fc_session_capabilities* capabilities, double* out);
fc_session_status fc_session_create (const fc_session_capabilities* capabilities, uint32_t config_low,
                                     uint32_t config_high, fc_session* out);
fc_session_status fc_session_destroy (fc_session session);
// Shell updates available capacity between calls. Each allocation checks live + demand against
// heapCeilingBytes and largestBlockBytes against largestFreeBlockBytes before work or sample scans.
// Values are exact integers >= 0 and < 2^53. Invalid values do not replace the previous capacity.
fc_session_status fc_session_set_capacity (fc_session session, const fc_session_capacity* capacity);
// Demand queries use the session's storageFor, allocate nothing, and replace no event batch.
// They use the SAME JSON/meta/project bytes as the call. A domain/protocol refusal is in rejection.
fc_session_status fc_session_command_bytes (fc_session session, const char* json, uint32_t json_bytes, fc_session_storage* out);
fc_session_status fc_session_load_bytes (fc_session session, uint32_t channels, uint32_t frames, uint32_t rate,
                                         const char* meta, uint32_t meta_bytes, fc_session_storage* out);
// Additional demand for one ceiling over the retained source, before any needles work.
fc_session_status fc_session_needles_bytes (fc_session session, double ceiling_db, fc_session_storage* out);
fc_session_status fc_session_measurement_bytes (fc_session session, uint32_t channels, uint32_t frames,
                                                uint32_t rate, fc_session_measurement_storage* out);
fc_session_status fc_session_import_project_bytes (fc_session session, const char* project, uint32_t project_bytes,
                                                   fc_session_storage* out);
fc_session_status fc_session_config_version (uint32_t* out); // two halves, low first

// The named-field JSON codec. No terminator is read or written; written is the exact byte length.
// Commands take at most COMMAND_JSON_BYTES. The answer capacity must be at least ANSWER_BYTES
// BEFORE the command runs, including for malformed JSON. Unknown/missing/duplicate fields yield
// rejected{code:"contract",reason,field}; domain rejections carry the stable Rejection code.
fc_session_status fc_session_command (fc_session session, const char* json, uint32_t json_bytes,
                                      char* answer, uint32_t capacity, uint32_t* written);
// frames is uint32: the wasm heap (at most 2 GiB) cannot hold more. Native callers needing
// uint64 frames use C++ Pcm; frames exceeding addressable memory are refused before sample reads.
fc_session_status fc_session_load (fc_session session, uint32_t command_low, uint32_t command_high,
                                   const float* const* pcm, uint32_t channels, uint32_t frames, uint32_t rate,
                                   const char* meta, uint32_t meta_bytes, char* answer, uint32_t capacity, uint32_t* written);
fc_session_status fc_session_import_project (fc_session session, uint32_t command_low, uint32_t command_high,
                                             const char* project, uint32_t project_bytes,
                                             char* answer, uint32_t capacity, uint32_t* written);
fc_session_status fc_session_export_project_size (fc_session session, uint32_t* out);
fc_session_status fc_session_export_project_copy (fc_session session, char* output, uint32_t capacity, uint32_t* written);
// The immutable flattened TOML for a completed master: target, delivery and every device's final value and origin.
fc_session_status fc_session_worked_report_size (fc_session session, uint32_t master_id, uint32_t* out);
fc_session_status fc_session_worked_report_copy (fc_session session, uint32_t master_id,
                                                 char* output, uint32_t capacity, uint32_t* written);

// Work units, never milliseconds. Zero polls; one call takes at most the session's kStepUnits.
fc_session_status fc_session_step (fc_session session, uint32_t budget, uint32_t* out);
fc_session_status fc_session_events_size (fc_session session, fc_session_sizes* out);
fc_session_status fc_session_events_copy (fc_session session, char* json, uint32_t json_capacity,
                                          double* rows, uint32_t row_capacity);
fc_session_status fc_session_snapshot_size (fc_session session, fc_session_sizes* out);
fc_session_status fc_session_snapshot_copy (fc_session session, char* json, uint32_t json_capacity,
                                            double* rows, uint32_t row_capacity);
// Full source evidence with masters' heavy rows/traces omitted. The ordinary snapshot is unchanged.
fc_session_status fc_session_source_snapshot_size (fc_session session, fc_session_sizes* out);
fc_session_status fc_session_source_snapshot_copy (fc_session session, char* json, uint32_t json_capacity,
                                                   double* rows, uint32_t row_capacity);
// row_capacity is BYTES, divisible by 8. JSON descriptors {byteOffset,length,stride} address the
// separate LITTLE-ENDIAN IEEE-754 f64 buffer (big-endian builds are refused at compile time): points [index,value], runs [first,count,value], machine differences
// [device,field,fileValue,coreValue]. The page reads Float64Array. Binary non-finite values keep
// IEEE-754 bits; JSON uses "-Infinity", "Infinity", "NaN". Byte
// counters are exact doubles < 2^53; uint64 identities are decimal strings. Events are a union
// discriminated by kind in the generated .d.ts. Size queries and copies allocate nothing.

// Measurement queries are named JSON MeasurementQuery records from the one codec generator.
// Source frame ranges are [fromFrame,toFrame); channel/axis order and row layouts are documented
// in docs/SESSION.md. QueryStatus is carried by the answer. Entry/JSON errors write nothing.
// query_size returns capacity bounds BEFORE work; query_copy returns actual sizes in written.
// Between them no command/step/query may intervene. Request IDs are echoed exactly; source,
// revision and measurement key identify the retained evidence. Arrays belong to the caller.
// query_bytes prices the transient result and bounded cache, using fc_session_storage semantics.
fc_session_status fc_session_query_bytes (fc_session session, const char* request, uint32_t request_bytes,
                                          fc_session_storage* out);
fc_session_status fc_session_query_size (fc_session session, const char* request, uint32_t request_bytes,
                                         fc_session_sizes* out);
fc_session_status fc_session_query_copy (fc_session session, const char* request, uint32_t request_bytes,
                                         char* json, uint32_t json_capacity, double* rows, uint32_t row_capacity,
                                         fc_session_sizes* written);
// Frequent UI snapshots: same fields and scalar measurements, measurementRowsIncluded=false.
// Large rows are read through queries; the existing full snapshot entry points are unchanged.
fc_session_status fc_session_summary_size (fc_session session, fc_session_sizes* out);
fc_session_status fc_session_summary_copy (fc_session session, char* json, uint32_t json_capacity,
                                           double* rows, uint32_t row_capacity);

// Additive v1 sidecar path. The typed MeasuredSource JSON is generated by the
// same codec schema as snapshots. A missing PCM is explicit in the snapshot;
// attachment checks the ordinary source hash, retains the facts, and indexes
// only the waveform. Each bytes call precedes any allocation or PCM scan.
fc_session_status fc_session_load_measured_bytes (fc_session session, const char* facts, uint32_t facts_bytes,
                                                  fc_session_storage* out);
fc_session_status fc_session_load_measured (fc_session session, uint32_t command_low, uint32_t command_high,
                                            const char* facts, uint32_t facts_bytes,
                                            char* answer, uint32_t capacity, uint32_t* written);
fc_session_status fc_session_attach_audio_bytes (fc_session session, uint32_t channels, uint32_t frames,
                                                 uint32_t rate, fc_session_storage* out);
fc_session_status fc_session_attach_audio (fc_session session, uint32_t command_low, uint32_t command_high,
                                           const float* const* pcm, uint32_t channels, uint32_t frames, uint32_t rate,
                                           char* answer, uint32_t capacity, uint32_t* written);

// A ready v14 fc_master topology/parameter pair, supplied by Decide. Its sampleRate/channels
// must name the retained source. The identity arguments fence a stale request before any
// allocation. The delivery format is the target's: its rate (the source's when the target
// keeps it) and its bit depth, PCM16 or PCM24. topology->deliveryRate and
// params->dither.bits of 0 take them, the same value restates them, and any other
// finite value is the open rejection DeliveryFormat, before any allocation, with a fact
// naming the target's depth and rate. A NaN or infinite deliveryRate is a malformed
// record, as every other non-finite field is: FC_SESSION_ERR_CONTRACT, no answer. This
// holds even when topology->dither is off. This is one session job, not another Project.
fc_session_status fc_session_master_bytes (fc_session session, uint32_t source_low, uint32_t source_high,
                                           uint32_t revision_low, uint32_t revision_high,
                                           const fc_master_config* topology, const fc_master_params* params,
                                           fc_session_storage* out);
fc_session_status fc_session_master (fc_session session, uint32_t command_low, uint32_t command_high,
                                     uint32_t source_low, uint32_t source_high,
                                     uint32_t revision_low, uint32_t revision_high,
                                     const fc_master_config* topology, const fc_master_params* params,
                                     char* answer, uint32_t capacity, uint32_t* written);

// One pending PCM transfer per session. The four token fields are read from the snapshot's
// pendingMaster. A source replacement invalidates them. Copy writes caller storage; release
// frees session storage. Native C++ callers can move it with Session::takeMaster.
typedef struct fc_session_master_token
{
    uint32_t size;
    uint32_t source_low, source_high;
    uint32_t revision_low, revision_high;
    uint32_t job, master;
} fc_session_master_token;
fc_session_status fc_session_master_audio_size (fc_session session, const fc_session_master_token* token,
                                                double* bytes, uint32_t* frames, uint32_t* channels, uint32_t* rate);
fc_session_status fc_session_master_audio_copy (fc_session session, const fc_session_master_token* token,
                                                float* output, uint32_t sample_capacity);
fc_session_status fc_session_master_audio_release (fc_session session, const fc_session_master_token* token);
// Scoped wasm view for one copy into an independent ArrayBuffer. The returned address is valid only
// until the next module call or memory.grow. Construct the heap view and call slice() synchronously,
// then release; never retain the heap view or transfer the WebAssembly memory buffer.
fc_session_status fc_session_master_audio_view (fc_session session, const fc_session_master_token* token,
                                                const float** output, uint32_t* samples);

// Explicit master waveform deep zoom. The caller supplies planar delivered PCM for exactly the
// requested [fromFrame,toFrame), at most 65536 frames. Size calls inspect only its shape;
// copy reads the supplied chunk and never re-renders or stores it in the session.
fc_session_status fc_session_master_waveform_chunk_bytes (fc_session session, const char* request,
                                                          uint32_t request_bytes, uint32_t channels,
                                                          uint32_t frames, uint32_t rate, fc_session_storage* out);
fc_session_status fc_session_master_waveform_chunk_size (fc_session session, const char* request,
                                                         uint32_t request_bytes, uint32_t channels,
                                                         uint32_t frames, uint32_t rate, fc_session_sizes* out);
fc_session_status fc_session_master_waveform_chunk_copy (fc_session session, const char* request,
                                                         uint32_t request_bytes, const float* const* pcm,
                                                         uint32_t channels, uint32_t frames, uint32_t rate,
                                                         char* json, uint32_t json_capacity,
                                                         double* rows, uint32_t row_capacity,
                                                         fc_session_sizes* written);

// A completed safe master can be copied as a canonical WAV before its PCM is
// released. The format is the completed job's target bit depth (PCM16 or PCM24).
// Size is allocation-free. Copy writes at most 65536 bytes at a caller-chosen
// offset; repeat any slice to get identical bytes. The shell owns the assembled
// image, indexed by master id, before calling master_audio_release. A later
// format requires an explicit external-PCM contract and cannot re-render here.
fc_session_status fc_session_master_wav_size (fc_session session, const fc_session_master_token* token,
                                             double* bytes, uint32_t* bits);
fc_session_status fc_session_master_wav_copy (fc_session session, const fc_session_master_token* token,
                                             uint32_t offset_low, uint32_t offset_high,
                                             uint8_t* output, uint32_t capacity, uint32_t* written);

// ====================================================================================
// THE PURE KIT — stateless answers for a shell's UI thread (felitronics/session/Kit.h)
// ====================================================================================
// The same module instantiated a second time on the page's main thread answers these within one frame, without the
// worker: a fact as text, what a person typed into a field, a knob's travel and heat, mono bass's zones, an EQ curve
// preview and a low-end curve. No handle — no session is needed or touched — and no state: each call is a pure function
// of its arguments and the compiled-in config and catalogue, and forwards to felitronics::session::Kit, whose header
// carries the rules. The conventions are the session's: the poison, the argument order above, statuses for refusals
// (an unknown field or language, a non-finite or out-of-domain argument, a fact that is not one: CONTRACT; the calling
// thread's floating-point environment: FP_ENVIRONMENT), caller buffers, nothing allocated, nothing kept.
// A FIELD is its text::Term id — the id a refusal already names it by. A LANGUAGE is its code ("ru"), as Text::langOf
// reads it. Doubles cross as doubles (alignment 8); a count of points is per point, two doubles each (hz, then the dB).
#define FC_SESSION_KIT_FIELD_TARGET_LUFS 3u
#define FC_SESSION_KIT_FIELD_TARGET_TP 4u
#define FC_SESSION_KIT_FIELD_HPF_FQ 5u
#define FC_SESSION_KIT_FIELD_HPF_SLOPE 6u
#define FC_SESSION_KIT_FIELD_MONO_BASS_FQ 7u
#define FC_SESSION_KIT_FIELD_MONO_BASS_WIDTH 8u
#define FC_SESSION_KIT_FIELD_GLUE_UP_TO_DB 9u
#define FC_SESSION_KIT_FIELD_SATURATION_DRIVE 10u
#define FC_SESSION_KIT_FIELD_SATURATION_MIX 11u
#define FC_SESSION_KIT_FIELD_TILT_DB 13u
#define FC_SESSION_KIT_FIELD_LIMITER_NEEDLES_DB 15u
#define FC_SESSION_KIT_FIELD_LOW_DB 16u
#define FC_SESSION_KIT_PARSE_ACCEPTED 0u
#define FC_SESSION_KIT_PARSE_NOT_A_NUMBER 1u
#define FC_SESSION_KIT_PARSE_OUT_OF_DOMAIN 2u
#define FC_SESSION_KIT_TRAVEL_VALUES 3u
#define FC_SESSION_KIT_HEAT_VALUES 3u
#define FC_SESSION_KIT_ZONES 2u
#define FC_SESSION_KIT_EQ_PARAMS 7u
#define FC_SESSION_KIT_EQ_POINTS 128u
#define FC_SESSION_KIT_EQ_PEAK_VALUES 4u
// Appended with the EQ bands: their five gains as kit fields, and the curve with them (fc_kit_eq_curve_bands).
#define FC_SESSION_KIT_FIELD_BANDS_BODY 134u
#define FC_SESSION_KIT_FIELD_BANDS_MUD 135u
#define FC_SESSION_KIT_FIELD_BANDS_FORWARD 136u
#define FC_SESSION_KIT_FIELD_BANDS_BRIGHTNESS 137u
#define FC_SESSION_KIT_FIELD_BANDS_AIR 138u
#define FC_SESSION_KIT_EQ_BANDS_PARAMS 13u
// Appended with slice 5: the saturation's transfer curve (fc_kit_saturation_curve), its points.
#define FC_SESSION_KIT_SATURATION_POINTS 129u

// A published fact ({"FactId":…,"args":[…]}, as a snapshot, an event or a plan carries it) in a language, as UTF-8
// without a terminator. A null output is allowed with capacity 0; TOO_SMALL writes the bytes needed to *written. A fact
// that does not carry exactly the arguments its message needs (one missing, one too many, one of another kind) is
// FC_SESSION_ERR_CONTRACT, as a malformed one is: nothing written, *written untouched — never the raw template.
fc_session_status fc_kit_text (const char* fact, uint32_t fact_bytes, const char* lang, uint32_t lang_bytes,
                              char* output, uint32_t capacity, uint32_t* written);
// What a person typed into a field: OK with *refusal FC_SESSION_KIT_PARSE_* and, when accepted, *value the number to send.
// source_rate is the source's, Hz (the high-pass's cutoff stays below its Nyquist); 0 when there is no source.
fc_session_status fc_kit_parse (const char* typed, uint32_t typed_bytes, const char* lang, uint32_t lang_bytes,
                               uint32_t field, uint32_t source_rate, double* value, uint32_t* refusal);
// A knob's travel: out[FC_SESSION_KIT_TRAVEL_VALUES] = from, to, step.
fc_session_status fc_kit_travel (uint32_t field, double* out);
// Where a value stands along the travel, 0 … 1; and the value at a position, on the knob's grid.
fc_session_status fc_kit_position (uint32_t field, double value, double* out);
fc_session_status fc_kit_value_at (uint32_t field, double position, double* out);
// out[FC_SESSION_KIT_HEAT_VALUES] = heat 0 … 1, side −1 / 0 / +1, and 1 when the knob has a window (0: heat and side are 0).
fc_session_status fc_kit_heat (uint32_t field, double value, double* out);
// Mono bass's zones, club then vinyl: out[2 * FC_SESSION_KIT_ZONES] = fromHz, toHz each. And the zones holding hz, as bits.
fc_session_status fc_kit_mono_zones (double* out);
fc_session_status fc_kit_mono_zones_at (double hz, uint32_t* out);
// The EQ curve three EQ knobs would draw. params[FC_SESSION_KIT_EQ_PARAMS] = hpf on (0/1), cutoff Hz, slope dB/oct (whole),
// tilt on, tilt dB, low on, low dB; rate a whole number of Hz. curve[2 * FC_SESSION_KIT_EQ_POINTS] = hz, dB per point;
// peak[FC_SESSION_KIT_EQ_PEAK_VALUES] = the peak of the curve without the high-pass (tilt, low, the EQ bands): hz, its dB, 1 when
// beyond warnDb, the device bit that gives most of it (FC_SESSION_DEVICE_TILT, FC_SESSION_DEVICE_LOW_SHELF or
// FC_SESSION_DEVICE_EQ_BANDS).
fc_session_status fc_kit_eq_curve (const double* params, double rate, double* curve, double* peak);
// ...with the EQ bands: params[FC_SESSION_KIT_EQ_BANDS_PARAMS] = the seven above, then the bands' gains in dB — body, mud,
// forward, brightness, air (0: no band) — then the bands' tick, on (0/1): off draws no band whatever the gains, which
// must still be in their domain. curve and peak as above, the bands in the peak.
fc_session_status fc_kit_eq_curve_bands (const double* params, double rate, double* curve, double* peak);
// A low-end curve from `bands` band centres and energies: the bands in from_hz … to_hz as (hz, dB) points into output,
// capacity in points. *written is the points written — 0 for fewer than two — or, with TOO_SMALL, the points needed.
fc_session_status fc_kit_low_end_curve (const double* centre_hz, const double* energy, uint32_t bands,
                                       double from_hz, double to_hz, double* output, uint32_t capacity,
                                       uint32_t* written);
// The saturation's transfer curve: type a saturation type a person may pick (its number in a project: 0 tanh, 4 tube,
// 5 transistor, 6 transformer, 7 tape), drive_db the shaper's own drive (the plan's saturation.driveDb; the knob's value
// for an input peaking at 0 dBTP), mix 0 … 1. curve[2 * FC_SESSION_KIT_SATURATION_POINTS] = input, output per point, the
// inputs from −1 to +1 of full scale a 64th apart — the chain's saturator settled on a held level.
fc_session_status fc_kit_saturation_curve (uint32_t type, double drive_db, double mix, double* curve);

#ifdef __cplusplus
}   // extern "C"
#endif

#endif   // FC_SESSION_ABI_H
