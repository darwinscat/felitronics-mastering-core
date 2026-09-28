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
//   fc_session   the surface
//   ----------   ------------------------------------------------------------------------------------------------
//   1            capabilities/config creation and demand, commands/load/project, step, events and snapshot copies.
#define FC_SESSION_ABI_VERSION 1u
#define FC_SESSION_CAPABILITIES_V1_BYTES 32u
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
    FC_SESSION_ERR_OVERLAP = 13,        // output overlaps another output or an input
    FC_SESSION_ERR_TRAP = 14,           // shell maps a thrown wasm trap/abort to this status; see POISON
    FC_SESSION_ERR_STRUCT_TOO_SMALL = 15, // size is below the v1 record size
    FC_SESSION_ERR_STRUCT_TOO_LARGE = 16, // size exceeds the record this build understands
    FC_SESSION_ERR_NO_SOURCE = 17,        // export: the session has no source
    FC_SESSION_ERR_NOT_PLACED = 18        // export: the first measurement has not placed devices
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
typedef struct fc_session_capabilities
{
    uint32_t size;
    double heapCeilingBytes;
    uint32_t maxRateHz;
    uint32_t offeredDevices;
    double largestFreeBlockBytes;
} fc_session_capabilities;

typedef struct fc_session_sizes
{
    uint32_t size;
    uint32_t jsonBytes;
    uint32_t rowBytes;
} fc_session_sizes;

// All boundary structs are size-prefixed: capabilities (32), sizes (12), capacity (24), storage (32).
// Set size to the caller's sizeof before EVERY call, including output queries. It is preserved.
// Fields are append-only. Future builds accept older supported sizes, write only that prefix,
// and document the v1 default of each absent later field. A new field never changes the
// meaning of a v1 prefix; appended reserved fields default to zero and are ignored. V1 has no optional suffix fields:
// size < the v1 size is STRUCT_TOO_SMALL; size > this build's size is STRUCT_TOO_LARGE.
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
    double liveBytes;               // current declared bytes; caller can assess live + bytes
} fc_session_storage;

// Detailed source measurement demand. Counts include one retained result copy and its codec buffers.
// Reserved slots preserve the earlier record layout; always zero. Needles have a separate job demand.
typedef struct fc_session_measurement_storage
{
    uint32_t size;
    uint32_t rejection;
    double sourceBytes;
    double resultBytes;
    double reservedBytes;
    double workspaceBytes;
    double copyBytes;
    double codecBytes;
    double allocatorBytes;
    double loadPeakBytes;
    double workPeakBytes;
    double peakBytes;
    double largestBlockBytes;
    uint32_t reserved;
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
// while no command or step intervenes. No pointer into session memory survives a call.
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

// Work units, never milliseconds. Zero polls; one call takes at most the session's kStepUnits.
fc_session_status fc_session_step (fc_session session, uint32_t budget, uint32_t* out);
fc_session_status fc_session_events_size (fc_session session, fc_session_sizes* out);
fc_session_status fc_session_events_copy (fc_session session, char* json, uint32_t json_capacity,
                                          double* rows, uint32_t row_capacity);
fc_session_status fc_session_snapshot_size (fc_session session, fc_session_sizes* out);
fc_session_status fc_session_snapshot_copy (fc_session session, char* json, uint32_t json_capacity,
                                            double* rows, uint32_t row_capacity);
// row_capacity is BYTES, divisible by 8. JSON descriptors {byteOffset,length,stride} address the
// separate LITTLE-ENDIAN IEEE-754 f64 buffer (big-endian builds are refused at compile time): points [index,value], runs [first,count,value], machine differences
// [device,field,fileValue,coreValue]. The page reads Float64Array. Binary non-finite values keep
// IEEE-754 bits; JSON uses "-Infinity", "Infinity", "NaN". Byte
// counters are exact doubles < 2^53; uint64 identities are decimal strings. Events are a union
// discriminated by kind in the generated .d.ts. Size queries and copies allocate nothing.

#ifdef __cplusplus
}   // extern "C"
#endif

#endif   // FC_SESSION_ABI_H
