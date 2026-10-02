// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// fc_session — the implementation of the C ABI declared in tools/fc_session_abi.h, and the translation unit of the
// fcsession wasm module (tools/wasm/build.sh). Read the header first: it carries the contract, this file only the
// unpacking of it.
//
// NOTHING HERE DECIDES ANYTHING. Four jobs and no fifth: check what a page hands over, own the handles, refuse every
// call once one has not returned (the poison), and forward to `felitronics::session` — including its refusals, mapped
// to status codes. It compiles natively too (FC_EXPORT degrades to extern "C"), which is how
// felitronics_session_abi_tests runs it under ctest, ASan and UBSan, and how fcore_session reports the ABI it speaks —
// and everywhere it is compiled with the session library's own options (tools/CMakeLists.txt, tools/wasm/build.sh): no
// exceptions, no RTTI, no contraction, no fast-math.
//
// TWO GLOBALS, AND THOSE TWO ARE THE ONLY ONES SESSION HAS ANYWHERE. `modules/session` holds no mutable state outside
// its objects (docs/SESSION.md); a C boundary cannot work without some, because a handle must name something between
// calls and the poison must outlive the call that never returned. So this file keeps the handle table and the poison
// flag, and nothing else — the object-file gate reads this translation unit's object and refuses any writable symbol
// but those two, named in tools/lint/session-objects.txt. Both are trivially destructible, so the module registers no
// destructor to run at exit either.
//
// AND THE SESSION'S SOURCE LAWS HOLD HERE TOO (tools/lint/check-session-laws.mjs scans this file and the ABI header): the
// build guards first, no macro but FC_EXPORT (which tools/wasm/build.sh's export scanner reads), no platform branch but
// the one below, the include allowlist plus the two emscripten headers. That list is the file's stated allowance.

#include "BuildGuards.h"                       // first: refuses a unit compiled with exceptions, RTTI or fast-math
#include "fc_session_abi.h"

#include <felitronics/session/Config.h>
#include <felitronics/session/Kit.h>
#include <felitronics/session/Session.h>
#include <felitronics/session/Wire.h>

#include <cstdint>
#include <algorithm>
#include <memory>
#include <cmath>

#if defined(__EMSCRIPTEN__)
  #include <emscripten/emscripten.h>
  #include <emscripten/heap.h>                 // emscripten_get_heap_size — NOT declared by emscripten.h
  #define FC_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
  #define FC_EXPORT extern "C"
#endif

using felitronics::session::Session;
using felitronics::session::Status;
using felitronics::session::Wire;
using felitronics::session::CodecStatus;
using felitronics::session::Rejection;
using felitronics::session::command::MasterReady;

static_assert (FC_SESSION_COMMAND_JSON_BYTES == felitronics::session::kCommandJsonBytes);
static_assert (FC_SESSION_ANSWER_BYTES == felitronics::session::kAnswerBytes);
static_assert (FC_SESSION_STEP_UNITS == felitronics::session::kStepUnits);
static_assert (FC_SESSION_MIN_RATE_HZ == felitronics::session::kMinSampleRate);
static_assert (FC_SESSION_MORE == unsigned (felitronics::session::StepState::More));
static_assert (FC_SESSION_DONE == unsigned (felitronics::session::StepState::Done));
static_assert (sizeof (fc_session) == 4, "a handle is 32 bits on every tier");
static_assert (FC_SESSION_MAX_HANDLES >= 1u && FC_SESSION_MAX_HANDLES <= 255u,
               "the slot index is packed into the handle's low 8 bits, with 0 kept for 'no handle'");

namespace
{

//==============================================================================
// THE HANDLE TABLE — fc_master's scheme (tools/wasm/fc_master.cpp, HANDLES), for fc_master's measured reason: under
// emscripten a destroyed object's address came back for the next one 19 times out of 19, so a raw pointer handle is a
// use-after-free that the shipping tier reproduces every time and a desktop machine never does. A handle is a slot and
// that slot's generation: `(generation << 8) | (slot + 1)`, 24 bits of generation.
//
// AND A SLOT RETIRES BEFORE ITS GENERATION WRAPS. A generation that wrapped would give the slot's first handle back to a
// later session, and the stale handle would then destroy it: reproduced on the wasm module — handle 257 created and
// destroyed, 16 777 214 more create/destroy cycles (0.21 s), the next create answered 257 again, and destroy(257)
// through the stale copy destroyed the new session. So a slot whose generation reaches kMaxGeneration is retired when
// that session is destroyed: it issues no handle again, and every handle it ever issued stays refused. The price is a
// lifetime budget — FC_SESSION_MAX_HANDLES x kMaxGeneration creates per module instance — after which a create
// answers FC_SESSION_ERR_EXHAUSTED for good (fc_session_abi.h says so).
constexpr std::uint32_t kMaxGeneration = FC_SESSION_SLOT_GENERATIONS;
static_assert (kMaxGeneration == 0x00FFFFFFu, "a generation is the 24 bits of the handle above the slot byte");

struct Slot
{
    std::uint32_t gen = 1;           // from 1, so a zeroed handle is never valid
    bool retired = false;            // its generations are spent: it issues no handle again
    Session* session = nullptr;      // null: the slot is free. Owned — deleted by destroy — and a raw pointer so the
                                     // table is trivially destructible (no exit-time destructor for a module to run)
};

Slot g_slots[FC_SESSION_MAX_HANDLES];

// THE POISON FLAG — one variable, three states, because "a call is in progress" and "a call never returned" are the
// same mark read at two moments: an entry point that finds it anything but Idle knows an earlier call did not come
// back (or that it was re-entered, which it cannot tell apart), and latches Poisoned. `volatile` for fc_master's
// reason: the mark's only reader follows an abort, and nothing obliges an optimiser to keep a store whose reader it
// cannot see.
enum : std::uint8_t { kIdle = 0, kInCall = 1, kPoisoned = 2 };
volatile std::uint8_t g_callState = kIdle;

// THE CALL, as an object — the first statement of every status-returning entry point, ahead of every other check,
// because an abandoned module has no handle and no argument this file can vouch for:
//     const CallGuard call;
//     if (call.refused()) return FC_SESSION_ERR_POISONED;
// Entering finds the mark Idle and sets InCall, or finds anything else — a call that never returned, or a re-entry,
// which it cannot tell apart — and latches Poisoned. Leaving clears InCall on a normal return. Nothing else can end a
// call: this translation unit and the library are compiled without exceptions, so there is no unwinding to tell apart
// from a return — an allocation that cannot be served ends the process natively and aborts the module in wasm, where
// the mark stays set and poisons what follows. A mark that became Poisoned during the call is not cleared — the latch
// is the point. (A class, not a macro: the session's sources define no macros, and this file keeps only FC_EXPORT.)
class CallGuard
{
public:
    CallGuard() noexcept : refused_ (g_callState != kIdle)
    {
        g_callState = refused_ ? kPoisoned : kInCall;
    }
    ~CallGuard()
    {
        if (g_callState == kInCall) g_callState = kIdle;
    }
    CallGuard (const CallGuard&) = delete;
    CallGuard& operator= (const CallGuard&) = delete;

    [[nodiscard]] bool refused() const noexcept { return refused_; }

private:
    const bool refused_;
};

fc_session packHandle (std::uint32_t slot, std::uint32_t gen) noexcept
{
    return (gen << 8) | (slot + 1u);
}

Slot* lookup (fc_session h) noexcept
{
    const std::uint32_t slot = (h & 0xFFu) - 1u;          // 0 wraps to a huge index, refused below
    if (slot >= FC_SESSION_MAX_HANDLES) return nullptr;
    Slot& s = g_slots[slot];
    if (s.session == nullptr) return nullptr;
    if (s.gen != (h >> 8)) return nullptr;
    return &s;
}

// Is [p, p + bytes) inside the wasm linear memory? An out-pointer from JavaScript is an address a page computed, and
// an aligned one four bytes below the top of the heap passes every other check and then traps the module on the write
// — the barrier this facade exists to be, with a hole exactly where the value looks harmless. Natively there is no
// linear memory to bound it by, and this answers yes.
bool inHeap (const void* p, std::uint64_t bytes) noexcept
{
#if defined(__EMSCRIPTEN__)
    const std::uint64_t base = (std::uint64_t) reinterpret_cast<std::uintptr_t> (p);
    // Every span is a uint32 count times at most sizeof (pointer); this sum fits uint64.
    const std::uint64_t end = base + bytes;
    return end <= (std::uint64_t) emscripten_get_heap_size();
#else
    const auto base = reinterpret_cast<std::uintptr_t> (p);
    return bytes <= UINTPTR_MAX - base;
#endif
}

fc_session_status pointer (const void* p, std::uint64_t bytes, std::size_t alignment, bool optional = false) noexcept
{
    if (p == nullptr && optional && bytes == 0) return FC_SESSION_OK;
    if (p == nullptr) return FC_SESSION_ERR_NULL;
    if ((reinterpret_cast<std::uintptr_t> (p) & (alignment - 1u)) != 0u) return FC_SESSION_ERR_ALIGNMENT;
    if (! inHeap (p, bytes)) return FC_SESSION_ERR_SPAN;
    return FC_SESSION_OK;
}
// The capabilities' base is the whole record at v0.6.0, leanSummary included (owner, 2026-10-01). A field appended later
// keeps the base at 40: this assertion makes that a decision, not an accident of sizeof.
static_assert (sizeof (fc_session_capabilities) == 40u, "fc_session_capabilities grew: state its base size of 40 here");
constexpr std::uint32_t minimumSize (const fc_session_capabilities*) noexcept { return sizeof (fc_session_capabilities); }
constexpr std::uint32_t minimumSize (const fc_session_sizes*) noexcept { return FC_SESSION_SIZES_V1_BYTES; }
constexpr std::uint32_t minimumSize (const fc_session_capacity*) noexcept { return FC_SESSION_CAPACITY_V1_BYTES; }
constexpr std::uint32_t minimumSize (const fc_session_storage*) noexcept { return FC_SESSION_STORAGE_V1_BYTES; }
constexpr std::uint32_t minimumSize (const fc_session_measurement_storage*) noexcept { return sizeof (fc_session_measurement_storage); }
template <class T> fc_session_status record (const T* p) noexcept
{
    if (const auto st = pointer (p, minimumSize (p), alignof (T)); st != FC_SESSION_OK) return st;
    if (p->size < minimumSize (p)) return FC_SESSION_ERR_STRUCT_TOO_SMALL;
    if (p->size > sizeof (T)) return FC_SESSION_ERR_STRUCT_TOO_LARGE;
    return inHeap (p, p->size) ? FC_SESSION_OK : FC_SESSION_ERR_SPAN;
}
fc_session_status rejection (Rejection r) noexcept
{
    if (r == Rejection::FloatingPointEnvironment) return FC_SESSION_ERR_FP_ENVIRONMENT;
    if (r == Rejection::NoSource) return FC_SESSION_ERR_NO_SOURCE;
    if (r == Rejection::NotPlaced) return FC_SESSION_ERR_NOT_PLACED;
    return r == Rejection::None ? FC_SESSION_OK : FC_SESSION_ERR_CONTRACT;
}
bool overlap (const void* a, std::uint64_t an, const void* b, std::uint64_t bn) noexcept
{
    if (an == 0 || bn == 0) return false;
    const auto av = std::uint64_t (reinterpret_cast<std::uintptr_t> (a));
    const auto bv = std::uint64_t (reinterpret_cast<std::uintptr_t> (b));
    return av <= bv ? bv - av < an : av - bv < bn;
}
fc_session_status status (Status s) noexcept
{
    switch (s)
    {
        case Status::Ok: return FC_SESSION_OK;
        case Status::FloatingPointEnvironment: return FC_SESSION_ERR_FP_ENVIRONMENT;
        case Status::ConfigVersion: return FC_SESSION_ERR_CONFIG_VERSION;
        case Status::Capabilities: return FC_SESSION_ERR_CAPABILITIES;
        case Status::Memory: return FC_SESSION_ERR_MEMORY;
    }
    return FC_SESSION_ERR_CONTRACT;
}
fc_session_status status (CodecStatus s) noexcept
{
    switch (s)
    {
        case CodecStatus::Ok: return FC_SESSION_OK;
        case CodecStatus::Invalid: return FC_SESSION_ERR_CONTRACT;
        case CodecStatus::TooSmall: return FC_SESSION_ERR_TOO_SMALL;
        case CodecStatus::FloatingPointEnvironment: return FC_SESSION_ERR_FP_ENVIRONMENT;
    }
    return FC_SESSION_ERR_CONTRACT;
}
// A field appended after version 1 is read only from a record long enough to hold it.
felitronics::session::Capabilities unpack (const fc_session_capabilities& caps) noexcept
{
    felitronics::session::Capabilities out { caps.heapCeilingBytes, caps.maxRateHz, caps.offeredDevices, caps.largestFreeBlockBytes };
    if (caps.size >= offsetof (fc_session_capabilities, leanSummary) + sizeof (caps.leanSummary)) out.leanSummary = caps.leanSummary != 0;
    return out;
}
bool knownCapabilities (const fc_session_capabilities& caps) noexcept
{
    return caps.size < offsetof (fc_session_capabilities, leanSummary) + sizeof (caps.leanSummary) || caps.leanSummary <= 1u;
}

// The existing fc_master records are the complete, versioned ready DSP input.
// Keep this mapping at the C boundary; the session owns only C++ job values.
bool masterReady (const fc_master_config& c, const fc_master_params& p,
                  const felitronics::session::Source& source, MasterReady& ready) noexcept
{
    namespace eq = felitronics::eq;
    namespace dynamics = felitronics::dynamics;
    namespace saturation = felitronics::saturation;
    namespace dither = felitronics::dither;
    static_assert (FC_MAX_EQ_BANDS == eq::EqEngine::kMaxBands);
    static_assert (FC_MAX_EQ_LANES == eq::kNumLanes);
    if (c.header.abiVersion != FC_MASTER_ABI_VERSION || c.header.structSize != sizeof (c)
        || p.header.abiVersion != FC_MASTER_ABI_VERSION || p.header.structSize != sizeof (p)
        || ! std::isfinite (c.sampleRate) || ! std::isfinite (c.deliveryRate)
        || ! std::isfinite (c.compressorLookaheadMs) || ! std::isfinite (c.limiterLookaheadMs)
        || ! std::isfinite (c.sidechainHpfHz) || c.channels != std::int32_t (source.channels)
        || std::fabs (c.sampleRate - double (source.sampleRate)) > 0.0) return false;
    // The session refuses any rate but 0 and the target's, openly. A finite rate that is not a whole number of hertz
    // below 2^32 is no target's rate either: it arrives as 2^32 - 1, which no target delivers.
    const bool whole = c.deliveryRate >= 0.0 && c.deliveryRate < 4294967295.0
        && ! (std::fabs (c.deliveryRate - std::floor (c.deliveryRate)) > 0.0);
    ready.version = 1;
    ready.deliveryRateHz = whole ? std::uint32_t (c.deliveryRate) : 4294967295u;
    auto& t = ready.topology;
    t.internalBlock = c.internalBlock; t.eq = c.eq != 0; t.monoBass = c.monoBass != 0;
    t.stereoAir = c.stereoAir != 0; t.compressor = c.compressor != 0; t.clipper = c.clipper != 0;
    t.limiter = c.limiter != 0; t.dither = c.dither != 0;
    t.compressorLookaheadMs = c.compressorLookaheadMs; t.limiterLookaheadMs = c.limiterLookaheadMs;
    t.oversampleFactor = c.oversampleFactor; t.tapsPerPhase = c.tapsPerPhase;
    t.sidechainHpfHz = c.sidechainHpfHz;
    auto& q = ready.params;
    if (! std::isfinite (p.inputGainDb) || ! std::isfinite (p.preLimiterGainDb)) return false;
    q.inputGainDb = p.inputGainDb; q.preLimiterGainDb = p.preLimiterGainDb;
    for (int b = 0; b < FC_MAX_EQ_BANDS; ++b)
    {
        const auto& src = p.eqBands[b]; auto& dst = q.eqBands[b];
        if (src.type < FC_FILTER_BELL || src.type > FC_FILTER_TILT
            || ! std::isfinite (src.dyn.rangeDb) || ! std::isfinite (src.dyn.thrDb)
            || ! std::isfinite (src.dyn.atk) || ! std::isfinite (src.dyn.rel)) return false;
        dst.on = src.on != 0; dst.type = static_cast<eq::FilterType> (src.type);
        dst.swept = src.swept != 0; dst.bypass = src.bypass != 0;
        dst.dyn.on = src.dyn.on != 0; dst.dyn.rangeDb = src.dyn.rangeDb;
        dst.dyn.thrDb = src.dyn.thrDb; dst.dyn.thrAuto = src.dyn.thrAuto != 0;
        dst.dyn.atk = src.dyn.atk; dst.dyn.rel = src.dyn.rel;
        for (int l = 0; l < FC_MAX_EQ_LANES; ++l)
        {
            const auto& sl = src.lanes[l]; auto& dl = dst.lanes[l];
            if (! std::isfinite (sl.freq) || ! std::isfinite (sl.q) || ! std::isfinite (sl.gainDb)) return false;
            dl.on = sl.on != 0; dl.freq = sl.freq; dl.Q = sl.q; dl.gainDb = sl.gainDb;
            dl.slope = sl.slope; dl.bypass = sl.bypass != 0;
        }
    }
    if (! std::isfinite (p.monoBass.frequencyHz) || ! std::isfinite (p.monoBass.lowWidth)) return false;
    q.monoBass.enabled = p.monoBass.enabled != 0;
    q.monoBass.frequencyHz = p.monoBass.frequencyHz; q.monoBass.lowWidth = p.monoBass.lowWidth;
    if (p.compressor.detector < FC_DETECTOR_PEAK || p.compressor.detector > FC_DETECTOR_RMS
        || p.compressor.link < FC_LINK_MAX || p.compressor.link > FC_LINK_MEAN_POWER
        || p.compressor.mode < FC_COMP_DOWN_COMPRESS || p.compressor.mode > FC_COMP_DOWN_EXPAND
        || ! std::isfinite (p.compressor.rmsWindowMs) || ! std::isfinite (p.compressor.thresholdDb)
        || ! std::isfinite (p.compressor.ratio) || ! std::isfinite (p.compressor.kneeDb)
        || ! std::isfinite (p.compressor.rangeDb) || ! std::isfinite (p.compressor.attackMs)
        || ! std::isfinite (p.compressor.releaseMs) || ! std::isfinite (p.compressor.makeupDb)) return false;
    q.compressor.detector = static_cast<dynamics::Detector> (p.compressor.detector);
    q.compressor.link = static_cast<dynamics::LinkMode> (p.compressor.link);
    q.compressor.rmsWindowMs = p.compressor.rmsWindowMs;
    q.compressor.mode = static_cast<dynamics::Mode> (p.compressor.mode);
    q.compressor.thresholdDb = p.compressor.thresholdDb; q.compressor.ratio = p.compressor.ratio;
    q.compressor.kneeDb = p.compressor.kneeDb; q.compressor.rangeDb = p.compressor.rangeDb;
    q.compressor.attackMs = p.compressor.attackMs; q.compressor.releaseMs = p.compressor.releaseMs;
    q.compressor.makeupDb = p.compressor.makeupDb; q.compressor.autoMakeup = p.compressor.autoMakeup != 0;
    if (p.clipper.shape < FC_SHAPE_TANH || p.clipper.shape > FC_SHAPE_TAPE
        || ! std::isfinite (p.clipper.driveDb) || ! std::isfinite (p.clipper.bias)
        || ! std::isfinite (p.clipper.mix) || ! std::isfinite (p.clipper.outputDb)
        || ! std::isfinite (p.clipper.autoComp) || ! std::isfinite (p.clipper.dcBlockHz)) return false;
    q.clipper.shape = static_cast<saturation::WaveShaper::Shape> (p.clipper.shape);
    q.clipper.driveDb = p.clipper.driveDb; q.clipper.bias = p.clipper.bias;
    q.clipper.mix = p.clipper.mix; q.clipper.outputDb = p.clipper.outputDb;
    q.clipper.autoComp = p.clipper.autoComp; q.clipper.dcBlockHz = p.clipper.dcBlockHz;
    if (! std::isfinite (p.limiter.ceilingDbTp) || ! std::isfinite (p.limiter.releaseMs)
        || ! std::isfinite (p.limiterSlowReleaseMs) || ! std::isfinite (p.peakClipperOverCeilingDb)
        || ! std::isfinite (p.peakClipperKneeDb) || ! std::isfinite (p.stereoAirHz)
        || ! std::isfinite (p.stereoAirDb) || ! std::isfinite (p.compressorMix)
        || p.dither.shaping < FC_SHAPING_NONE || p.dither.shaping > FC_SHAPING_PSYCHO) return false;
    q.limiter.ceilingDbTp = p.limiter.ceilingDbTp; q.limiter.releaseMs = p.limiter.releaseMs;
    q.limiter.dualRelease = p.limiterDualRelease != 0; q.limiter.slowReleaseMs = p.limiterSlowReleaseMs;
    q.limiter.peakClip = p.peakClipper != 0; q.limiter.overCeilingDb = p.peakClipperOverCeilingDb;
    q.limiter.kneeDb = p.peakClipperKneeDb;
    q.stereoAir.enabled = p.stereoAir != 0;
    q.stereoAir.frequencyHz = float (p.stereoAirHz); q.stereoAir.gainDb = float (p.stereoAirDb);
    q.dither.bits = p.dither.bits; q.dither.shaping = static_cast<dither::NoiseShaping> (p.dither.shaping);
    // The session refuses any depth but 0 and the target's, openly. A value one byte cannot hold is no target's depth
    // either: it arrives as 255, never as its low byte (272 is not 16).
    ready.deliveryBits = p.dither.bits >= 0 && p.dither.bits <= 255 ? std::uint8_t (p.dither.bits) : std::uint8_t (255);
    q.dither.seed = (std::uint64_t (p.dither.seedHi) << 32) | p.dither.seedLo;
    q.dither.autoBlank = p.dither.autoBlank != 0; q.dither.autoBlankSamples = p.dither.autoBlankSamples;
    q.compressorMix = p.compressorMix;
    q.bypassEq = p.bypassEq != 0; q.bypassMonoBass = p.bypassMonoBass != 0;
    q.bypassCompressor = p.bypassCompressor != 0; q.bypassClipper = p.bypassClipper != 0;
    q.bypassLimiter = p.bypassLimiter != 0; q.bypassDither = p.bypassDither != 0;
    return true;
}
std::uint64_t joined (std::uint32_t low, std::uint32_t high) noexcept { return (std::uint64_t (high) << 32) | low; }
fc_session_status answerOut (char* out, std::uint32_t capacity, std::uint32_t* written) noexcept
{
    if (const auto st = pointer (out, capacity, 1); st != FC_SESSION_OK) return st;
    return pointer (written, sizeof (*written), alignof (std::uint32_t));
}
fc_session_status answerInputs (const char* input, std::uint32_t size, char* out, std::uint32_t capacity,
                               std::uint32_t* written) noexcept
{
    if (const auto st = pointer (input, size, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (out, capacity, written, sizeof (*written)) || overlap (input, size, out, capacity)
        || overlap (input, size, written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    return FC_SESSION_OK;
}
fc_session_status transferSize (fc_session session, fc_session_sizes* out, bool snapshot) noexcept
{
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    const auto n = snapshot ? Wire::snapshotBytes (*slot->session) : Wire::eventsBytes (slot->session->events());
    if (n.status != CodecStatus::Ok) return status (n.status);
    out->jsonBytes = n.jsonBytes; out->rowBytes = n.rowBytes; return FC_SESSION_OK;
}
fc_session_status transferCopy (fc_session session, char* json, std::uint32_t capacity,
                                double* rows, std::uint32_t rowCapacity, bool snapshot) noexcept
{
    if (const auto st = pointer (json, capacity, 1); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (rows, rowCapacity, 8, true); st != FC_SESSION_OK) return st;
    if (rowCapacity % sizeof (double) != 0) return FC_SESSION_ERR_ALIGNMENT;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (overlap (json, capacity, rows, rowCapacity)) return FC_SESSION_ERR_OVERLAP;
    return status (snapshot ? Wire::snapshot (*slot->session, { json, capacity }, { rows, rowCapacity / sizeof (double) })
                           : Wire::events (slot->session->events(), { json, capacity }, { rows, rowCapacity / sizeof (double) }));
}

fc_session_status storageOut (const Session& session, const felitronics::session::Checked& priced, fc_session_storage* out) noexcept
{
    if (priced.rejection == Rejection::FloatingPointEnvironment) return FC_SESSION_ERR_FP_ENVIRONMENT;
    out->rejection = std::uint32_t (priced.rejection);
    out->bytes = double (priced.bytes); out->largestBlockBytes = double (priced.largestBlockBytes);
    out->liveBytes = session.liveBytes(); return FC_SESSION_OK;
}

fc_session_status masterInputs (const fc_master_config* config, const fc_master_params* params) noexcept
{
    if (const auto st = pointer (config, sizeof (*config), alignof (fc_master_config)); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (params, sizeof (*params), alignof (fc_master_params)); st != FC_SESSION_OK) return st;
    return FC_SESSION_OK;
}
fc_session_status tokenInput (const fc_session_master_token* token) noexcept
{
    if (const auto st = pointer (token, sizeof (*token), alignof (fc_session_master_token)); st != FC_SESSION_OK) return st;
    return token->size == sizeof (*token) ? FC_SESSION_OK : FC_SESSION_ERR_CONTRACT;
}
felitronics::session::MasterToken unpack (const fc_session_master_token& token) noexcept
{
    return { joined (token.source_low, token.source_high), joined (token.revision_low, token.revision_high),
        token.job, token.master };
}

} // namespace

// Linked only by the contract fixture's extra translation unit. Production export
// lists omit the caller, so the optimizer discards this access function.
Session* contractSession (fc_session h) noexcept
{
    auto* slot = lookup (h);
    return slot ? slot->session : nullptr;
}

//==============================================================================
// ENTRY POINTS

FC_EXPORT std::uint32_t fc_session_abi_version (void) { return FC_SESSION_ABI_VERSION; }

FC_EXPORT fc_session_status fc_session_create_bytes (const fc_session_capabilities* capabilities, double* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (out, sizeof (*out), 8); st != FC_SESSION_OK) return st;
    if (const auto st = record (capabilities); st != FC_SESSION_OK) return st;
    // The record is as long as it says: a version-1 shell's ends before the appended fields.
    if (overlap (out, sizeof (*out), capabilities, capabilities->size)) return FC_SESSION_ERR_OVERLAP;
    if (! knownCapabilities (*capabilities)) return FC_SESSION_ERR_CAPABILITIES;
    const auto caps = unpack (*capabilities);
    const auto st = Session::checkCreate (caps, felitronics::session::config::Config::versions().all);
    if (st != Status::Ok && st != Status::Memory) return status (st);
    *out = double (Session::createBytes (caps)); return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_create (const fc_session_capabilities* capabilities, std::uint32_t config_low,
                                              std::uint32_t config_high, fc_session* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (out, sizeof (*out), alignof (fc_session)); st != FC_SESSION_OK) return st;
    if (const auto st = record (capabilities); st != FC_SESSION_OK) return st;
    if (overlap (out, sizeof (*out), capabilities, capabilities->size)) return FC_SESSION_ERR_OVERLAP;
    if (! knownCapabilities (*capabilities)) return FC_SESSION_ERR_CAPABILITIES;
    std::uint32_t slot = 0;
    while (slot < FC_SESSION_MAX_HANDLES && (g_slots[slot].session != nullptr || g_slots[slot].retired)) ++slot;
    if (slot == FC_SESSION_MAX_HANDLES) return FC_SESSION_ERR_EXHAUSTED;
    auto created = Session::create (unpack (*capabilities), joined (config_low, config_high));
    if (created.status != Status::Ok) return status (created.status);
    if (g_callState == kPoisoned) return FC_SESSION_ERR_POISONED;
    g_slots[slot].session = created.session.release();
    *out = packHandle (slot, g_slots[slot].gen);
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_destroy (fc_session session)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    Slot* s = lookup (session);
    if (s == nullptr) return FC_SESSION_ERR_HANDLE;
    delete s->session;
    if (g_callState == kPoisoned) return FC_SESSION_ERR_POISONED;
    s->session = nullptr;
    // The generation moves on, so the handle just destroyed can never address the next session in this slot — and at
    // the last generation the slot retires instead of wrapping (see HANDLES above). Generation 0 is never issued, so no
    // handle is ever a small integer.
    if (s->gen == kMaxGeneration)
        s->retired = true;
    else
        ++s->gen;
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_config_version (std::uint32_t* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const fc_session_status st = pointer (out, 2 * sizeof (*out), alignof (std::uint32_t)); st != FC_SESSION_OK) return st;
    // Reads the embedded data; allocates nothing.
    const std::uint64_t v = felitronics::session::config::Config::versions().all;
    out[0] = static_cast<std::uint32_t> (v);
    out[1] = static_cast<std::uint32_t> (v >> 32);
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_command (fc_session session, const char* json, std::uint32_t json_bytes,
                                               char* answer, std::uint32_t capacity, std::uint32_t* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = answerOut (answer, capacity, written); st != FC_SESSION_OK) return st;
    auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = answerInputs (json, json_bytes, answer, capacity, written); st != FC_SESSION_OK) return st;
    if (capacity < FC_SESSION_ANSWER_BYTES) return FC_SESSION_ERR_TOO_SMALL;
    char reply[FC_SESSION_ANSWER_BYTES]; std::uint32_t size = 0;
    const auto st = Wire::command (*slot->session, { json, json_bytes }, reply, size);
    if (g_callState == kPoisoned) return FC_SESSION_ERR_POISONED;
    if (st != CodecStatus::Ok) return status (st);
    std::copy_n (reply, size, answer); *written = size; return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_load (fc_session session, std::uint32_t command_low, std::uint32_t command_high,
                                            const float* const* pcm, std::uint32_t channels, std::uint32_t frames, std::uint32_t rate,
                                            const char* meta, std::uint32_t meta_bytes, char* answer, std::uint32_t capacity,
                                            std::uint32_t* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = answerOut (answer, capacity, written); st != FC_SESSION_OK) return st;
    auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = pointer (pcm, std::uint64_t (channels) * sizeof (*pcm), alignof (const float*), channels == 0);
        st != FC_SESSION_OK) return st;
    // Only the session decides whether the channel count/rate/audio is meaningful. Bound the
    // pointers it may read here; an invalid channel count is never used to walk a pointer table.
    if (channels <= 2)
        for (std::uint32_t c = 0; c < channels; ++c)
            if (const auto st = pointer (pcm[c], std::uint64_t (frames) * sizeof (float), alignof (float), frames == 0);
                st != FC_SESSION_OK) return st;
    if (const auto st = answerInputs (meta, meta_bytes, answer, capacity, written); st != FC_SESSION_OK) return st;
    if (overlap (pcm, std::uint64_t (channels) * sizeof (*pcm), answer, capacity)
        || overlap (pcm, std::uint64_t (channels) * sizeof (*pcm), written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    if (channels <= 2)
        for (std::uint32_t c = 0; c < channels; ++c)
            if (overlap (pcm[c], std::uint64_t (frames) * sizeof (float), answer, capacity)
                || overlap (pcm[c], std::uint64_t (frames) * sizeof (float), written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    if (capacity < FC_SESSION_ANSWER_BYTES) return FC_SESSION_ERR_TOO_SMALL;
    char reply[FC_SESSION_ANSWER_BYTES]; std::uint32_t size = 0;
    const auto st = Wire::load (*slot->session, joined (command_low, command_high), { pcm, channels, frames, rate },
                                { meta, meta_bytes }, reply, size);
    if (g_callState == kPoisoned) return FC_SESSION_ERR_POISONED;
    if (st != CodecStatus::Ok) return status (st);
    std::copy_n (reply, size, answer); *written = size; return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_import_project (fc_session session, std::uint32_t command_low, std::uint32_t command_high,
                                                      const char* project, std::uint32_t project_bytes,
                                                      char* answer, std::uint32_t capacity, std::uint32_t* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = answerOut (answer, capacity, written); st != FC_SESSION_OK) return st;
    auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = answerInputs (project, project_bytes, answer, capacity, written); st != FC_SESSION_OK) return st;
    if (capacity < FC_SESSION_ANSWER_BYTES) return FC_SESSION_ERR_TOO_SMALL;
    char reply[FC_SESSION_ANSWER_BYTES]; std::uint32_t size = 0;
    const auto st = Wire::importProject (*slot->session, joined (command_low, command_high),
                                         { project, project_bytes }, reply, size);
    if (g_callState == kPoisoned) return FC_SESSION_ERR_POISONED;
    if (st != CodecStatus::Ok) return status (st);
    std::copy_n (reply, size, answer); *written = size; return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_export_project_size (fc_session session, std::uint32_t* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (out, sizeof (*out), alignof (std::uint32_t)); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    const auto need = slot->session->exportProjectBytes();
    if (const auto st = rejection (need.rejection); st != FC_SESSION_OK) return st;
    *out = std::uint32_t (need.bytes); return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_export_project_copy (fc_session session, char* output, std::uint32_t capacity,
                                                           std::uint32_t* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = answerOut (output, capacity, written); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (overlap (output, capacity, written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    const auto need = slot->session->exportProjectBytes();
    if (const auto st = rejection (need.rejection); st != FC_SESSION_OK) return st;
    if (capacity < need.bytes) return FC_SESSION_ERR_TOO_SMALL;
    (void) slot->session->exportProject ({ output, capacity }); *written = std::uint32_t (need.bytes); return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_step (fc_session session, std::uint32_t budget, std::uint32_t* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (out, sizeof (*out), alignof (std::uint32_t)); st != FC_SESSION_OK) return st;
    auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    const auto stepped = slot->session->step (budget);
    if (stepped.refused) return FC_SESSION_ERR_FP_ENVIRONMENT;
    *out = std::uint32_t (stepped.state); return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_events_size (fc_session session, fc_session_sizes* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    return transferSize (session, out, false);
}
FC_EXPORT fc_session_status fc_session_snapshot_size (fc_session session, fc_session_sizes* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    return transferSize (session, out, true);
}
FC_EXPORT fc_session_status fc_session_events_copy (fc_session session, char* json, std::uint32_t json_capacity,
                                                   double* rows, std::uint32_t row_capacity)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    return transferCopy (session, json, json_capacity, rows, row_capacity, false);
}
FC_EXPORT fc_session_status fc_session_snapshot_copy (fc_session session, char* json, std::uint32_t json_capacity,
                                                     double* rows, std::uint32_t row_capacity)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    return transferCopy (session, json, json_capacity, rows, row_capacity, true);
}

FC_EXPORT fc_session_status fc_session_summary_size (fc_session session, fc_session_sizes* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    const auto n = Wire::summaryBytes (*slot->session);
    if (n.status != CodecStatus::Ok) return status (n.status);
    out->jsonBytes = n.jsonBytes; out->rowBytes = n.rowBytes; return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_summary_copy (fc_session session, char* json, std::uint32_t json_capacity,
                                                    double* rows, std::uint32_t row_capacity)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (json, json_capacity, 1); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (rows, row_capacity, 8, true); st != FC_SESSION_OK) return st;
    if (row_capacity % sizeof (double) != 0) return FC_SESSION_ERR_ALIGNMENT;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (overlap (json, json_capacity, rows, row_capacity)) return FC_SESSION_ERR_OVERLAP;
    return status (Wire::summary (*slot->session, { json, json_capacity }, { rows, row_capacity / sizeof (double) }));
}
FC_EXPORT fc_session_status fc_session_query_bytes (fc_session session, const char* request, std::uint32_t request_bytes,
                                                   fc_session_storage* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = pointer (request, request_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (out, sizeof (*out), request, request_bytes)) return FC_SESSION_ERR_OVERLAP;
    return storageOut (*slot->session, Wire::queryStorage (*slot->session, { request, request_bytes }), out);
}
FC_EXPORT fc_session_status fc_session_query_size (fc_session session, const char* request, std::uint32_t request_bytes,
                                                  fc_session_sizes* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = pointer (request, request_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (out, sizeof (*out), request, request_bytes)) return FC_SESSION_ERR_OVERLAP;
    const auto n = Wire::queryBuffers (*slot->session, { request, request_bytes });
    if (n.status != CodecStatus::Ok) return status (n.status);
    out->jsonBytes = n.jsonBytes; out->rowBytes = n.rowBytes; return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_query_copy (fc_session session, const char* request, std::uint32_t request_bytes,
                                                  char* json, std::uint32_t json_capacity, double* rows, std::uint32_t row_capacity,
                                                  fc_session_sizes* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (json, json_capacity, 1); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (rows, row_capacity, 8, true); st != FC_SESSION_OK) return st;
    if (row_capacity % sizeof (double) != 0) return FC_SESSION_ERR_ALIGNMENT;
    if (const auto st = record (written); st != FC_SESSION_OK) return st;
    auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = pointer (request, request_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (json, json_capacity, rows, row_capacity) || overlap (json, json_capacity, written, sizeof (*written))
        || overlap (rows, row_capacity, written, sizeof (*written)) || overlap (request, request_bytes, json, json_capacity)
        || overlap (request, request_bytes, rows, row_capacity) || overlap (request, request_bytes, written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    const auto n = Wire::query (*slot->session, { request, request_bytes }, { json, json_capacity }, { rows, row_capacity / sizeof (double) });
    if (n.status != CodecStatus::Ok) return status (n.status);
    written->jsonBytes = n.jsonBytes; written->rowBytes = n.rowBytes; return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_master_waveform_chunk_bytes (fc_session session, const char* request,
    std::uint32_t request_bytes, std::uint32_t channels, std::uint32_t frames, std::uint32_t rate,
    fc_session_storage* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = pointer (request, request_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (out, sizeof (*out), request, request_bytes)) return FC_SESSION_ERR_OVERLAP;
    return storageOut (*slot->session, Wire::masterWaveformChunkStorage (*slot->session,
        { request, request_bytes }, { nullptr, channels, frames, rate }), out);
}
FC_EXPORT fc_session_status fc_session_master_waveform_chunk_size (fc_session session, const char* request,
    std::uint32_t request_bytes, std::uint32_t channels, std::uint32_t frames, std::uint32_t rate,
    fc_session_sizes* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = pointer (request, request_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (out, sizeof (*out), request, request_bytes)) return FC_SESSION_ERR_OVERLAP;
    const auto n = Wire::masterWaveformChunkBuffers (*slot->session, { request, request_bytes },
        { nullptr, channels, frames, rate });
    if (n.status != CodecStatus::Ok) return status (n.status);
    out->jsonBytes = n.jsonBytes; out->rowBytes = n.rowBytes; return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_master_waveform_chunk_copy (fc_session session, const char* request,
    std::uint32_t request_bytes, const float* const* pcm, std::uint32_t channels, std::uint32_t frames,
    std::uint32_t rate, char* json, std::uint32_t json_capacity, double* rows,
    std::uint32_t row_capacity, fc_session_sizes* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (json, json_capacity, 1); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (rows, row_capacity, 8, true); st != FC_SESSION_OK) return st;
    if (row_capacity % sizeof (double) != 0) return FC_SESSION_ERR_ALIGNMENT;
    if (const auto st = record (written); st != FC_SESSION_OK) return st;
    auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = pointer (request, request_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (pcm, std::uint64_t (channels) * sizeof (*pcm), alignof (const float*), channels == 0);
        st != FC_SESSION_OK) return st;
    if (channels <= 2)
        for (std::uint32_t c = 0; c < channels; ++c)
            if (const auto st = pointer (pcm[c], std::uint64_t (frames) * sizeof (float), alignof (float), frames == 0);
                st != FC_SESSION_OK) return st;
    if (overlap (json, json_capacity, rows, row_capacity) || overlap (json, json_capacity, written, sizeof (*written))
        || overlap (rows, row_capacity, written, sizeof (*written)) || overlap (request, request_bytes, json, json_capacity)
        || overlap (request, request_bytes, rows, row_capacity) || overlap (request, request_bytes, written, sizeof (*written))
        || overlap (pcm, std::uint64_t (channels) * sizeof (*pcm), json, json_capacity)
        || overlap (pcm, std::uint64_t (channels) * sizeof (*pcm), rows, row_capacity)
        || overlap (pcm, std::uint64_t (channels) * sizeof (*pcm), written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    if (channels <= 2)
        for (std::uint32_t c = 0; c < channels; ++c)
            if (overlap (pcm[c], std::uint64_t (frames) * sizeof (float), json, json_capacity)
                || overlap (pcm[c], std::uint64_t (frames) * sizeof (float), rows, row_capacity)
                || overlap (pcm[c], std::uint64_t (frames) * sizeof (float), written, sizeof (*written)))
                return FC_SESSION_ERR_OVERLAP;
    // The retained master PCM may be this chunk's input, but never an output.
    const auto retained = slot->session->viewMaster (slot->session->pendingMaster());
    if (overlap (json, json_capacity, retained.data(), retained.size_bytes())
        || overlap (rows, row_capacity, retained.data(), retained.size_bytes())
        || overlap (written, sizeof (*written), retained.data(), retained.size_bytes())) return FC_SESSION_ERR_OVERLAP;
    const auto n = Wire::masterWaveformChunk (*slot->session, { request, request_bytes },
        { pcm, channels, frames, rate }, { json, json_capacity }, { rows, row_capacity / sizeof (double) });
    if (n.status != CodecStatus::Ok) return status (n.status);
    written->jsonBytes = n.jsonBytes; written->rowBytes = n.rowBytes; return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_set_capacity (fc_session session, const fc_session_capacity* capacity)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = record (capacity); st != FC_SESSION_OK) return st;
    return status (slot->session->setCapacity ({ capacity->heapCeilingBytes, capacity->largestFreeBlockBytes }));
}
FC_EXPORT fc_session_status fc_session_command_bytes (fc_session session, const char* json, std::uint32_t json_bytes,
                                                     fc_session_storage* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = pointer (json, json_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (out, sizeof (*out), json, json_bytes)) return FC_SESSION_ERR_OVERLAP;
    return storageOut (*slot->session, Wire::commandStorage (*slot->session, { json, json_bytes }), out);
}
FC_EXPORT fc_session_status fc_session_load_bytes (fc_session session, std::uint32_t channels, std::uint32_t frames,
                                                  std::uint32_t rate, const char* meta, std::uint32_t meta_bytes,
                                                  fc_session_storage* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = pointer (meta, meta_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (out, sizeof (*out), meta, meta_bytes)) return FC_SESSION_ERR_OVERLAP;
    return storageOut (*slot->session, Wire::loadStorage (*slot->session, channels, frames, rate, { meta, meta_bytes }), out);
}
FC_EXPORT fc_session_status fc_session_import_project_bytes (fc_session session, const char* project,
                                                            std::uint32_t project_bytes, fc_session_storage* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = pointer (project, project_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (out, sizeof (*out), project, project_bytes)) return FC_SESSION_ERR_OVERLAP;
    return storageOut (*slot->session, Wire::importStorage (*slot->session, { project, project_bytes }), out);
}

FC_EXPORT fc_session_status fc_session_needles_bytes (fc_session session, double ceiling_db, fc_session_storage* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    return storageOut (*slot->session, slot->session->needlesStorage (ceiling_db), out);
}

FC_EXPORT fc_session_status fc_session_measurement_bytes (fc_session session, std::uint32_t channels,
                                                         std::uint32_t frames, std::uint32_t rate,
                                                         fc_session_measurement_storage* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    const felitronics::session::Pcm pcm { nullptr, channels, frames, rate };
    const auto priced = slot->session->storageFor (felitronics::session::command::Load { 0, pcm, {} });
    if (priced.rejection == Rejection::FloatingPointEnvironment) return FC_SESSION_ERR_FP_ENVIRONMENT;
    const auto demand = priced.rejection == Rejection::None ? slot->session->measurementStorage (pcm)
                                                          : felitronics::session::MeasurementStorage {};
    out->rejection = std::uint32_t (priced.rejection);
    out->sourceBytes = demand.sourceBytes;
    out->resultBytes = demand.resultBytes;
    out->workspaceBytes = demand.workspaceBytes;
    out->copyBytes = demand.copyBytes;
    out->codecBytes = demand.codecBytes;
    out->allocatorBytes = demand.allocatorBytes;
    out->loadPeakBytes = demand.loadPeakBytes;
    out->workPeakBytes = demand.workPeakBytes;
    out->peakBytes = demand.peakBytes;
    out->largestBlockBytes = demand.largestBlockBytes;
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_load_measured_bytes (fc_session session, const char* facts,
                                                             std::uint32_t facts_bytes, fc_session_storage* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = pointer (facts, facts_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (facts, facts_bytes, out, sizeof (*out))) return FC_SESSION_ERR_OVERLAP;
    return storageOut (*slot->session, Wire::loadMeasuredStorage (*slot->session, { facts, facts_bytes }), out);
}
FC_EXPORT fc_session_status fc_session_load_measured (fc_session session, std::uint32_t command_low,
                                                       std::uint32_t command_high, const char* facts,
                                                       std::uint32_t facts_bytes, char* answer,
                                                       std::uint32_t capacity, std::uint32_t* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = answerOut (answer, capacity, written); st != FC_SESSION_OK) return st;
    auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = answerInputs (facts, facts_bytes, answer, capacity, written); st != FC_SESSION_OK) return st;
    if (capacity < FC_SESSION_ANSWER_BYTES) return FC_SESSION_ERR_TOO_SMALL;
    char reply[FC_SESSION_ANSWER_BYTES]; std::uint32_t size = 0;
    const auto st = Wire::loadMeasured (*slot->session, joined (command_low, command_high), { facts, facts_bytes }, reply, size);
    if (g_callState == kPoisoned) return FC_SESSION_ERR_POISONED;
    if (st != CodecStatus::Ok) return status (st);
    std::copy_n (reply, size, answer); *written = size; return FC_SESSION_OK;
}
FC_EXPORT fc_session_status fc_session_attach_audio_bytes (fc_session session, std::uint32_t channels,
                                                            std::uint32_t frames, std::uint32_t rate,
                                                            fc_session_storage* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    return storageOut (*slot->session, Wire::attachAudioStorage (*slot->session, { nullptr, channels, frames, rate }), out);
}
FC_EXPORT fc_session_status fc_session_attach_audio (fc_session session, std::uint32_t command_low,
                                                      std::uint32_t command_high, const float* const* pcm,
                                                      std::uint32_t channels, std::uint32_t frames,
                                                      std::uint32_t rate, char* answer, std::uint32_t capacity,
                                                      std::uint32_t* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = answerOut (answer, capacity, written); st != FC_SESSION_OK) return st;
    auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (overlap (answer, capacity, written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    if (const auto st = pointer (pcm, std::uint64_t (channels) * sizeof (*pcm), alignof (const float*), channels == 0);
        st != FC_SESSION_OK) return st;
    if (channels <= 2)
        for (std::uint32_t c = 0; c < channels; ++c)
            if (const auto st = pointer (pcm[c], std::uint64_t (frames) * sizeof (float), alignof (float), frames == 0);
                st != FC_SESSION_OK) return st;
    if (overlap (pcm, std::uint64_t (channels) * sizeof (*pcm), answer, capacity)
        || overlap (pcm, std::uint64_t (channels) * sizeof (*pcm), written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    if (channels <= 2)
        for (std::uint32_t c = 0; c < channels; ++c)
            if (overlap (pcm[c], std::uint64_t (frames) * sizeof (float), answer, capacity)
                || overlap (pcm[c], std::uint64_t (frames) * sizeof (float), written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    if (capacity < FC_SESSION_ANSWER_BYTES) return FC_SESSION_ERR_TOO_SMALL;
    char reply[FC_SESSION_ANSWER_BYTES]; std::uint32_t size = 0;
    const auto st = Wire::attachAudio (*slot->session, joined (command_low, command_high),
                                       { pcm, channels, frames, rate }, reply, size);
    if (g_callState == kPoisoned) return FC_SESSION_ERR_POISONED;
    if (st != CodecStatus::Ok) return status (st);
    std::copy_n (reply, size, answer); *written = size; return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_master_bytes (fc_session session, std::uint32_t source_low,
    std::uint32_t source_high, std::uint32_t revision_low, std::uint32_t revision_high,
    const fc_master_config* topology, const fc_master_params* params, fc_session_storage* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = record (out); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = masterInputs (topology, params); st != FC_SESSION_OK) return st;
    if (joined (source_low, source_high) != slot->session->source().hash
        || joined (revision_low, revision_high) != slot->session->revision()) return FC_SESSION_ERR_STALE;
    if (overlap (out, sizeof (*out), topology, sizeof (*topology))
        || overlap (out, sizeof (*out), params, sizeof (*params))) return FC_SESSION_ERR_OVERLAP;
    felitronics::session::command::Master request;
    request.source = joined (source_low, source_high); request.revision = joined (revision_low, revision_high);
    if (! masterReady (*topology, *params, slot->session->source(), request.ready)) return FC_SESSION_ERR_CONTRACT;
    return storageOut (*slot->session, slot->session->storageFor (request), out);
}

FC_EXPORT fc_session_status fc_session_master (fc_session session, std::uint32_t command_low,
    std::uint32_t command_high, std::uint32_t source_low, std::uint32_t source_high,
    std::uint32_t revision_low, std::uint32_t revision_high,
    const fc_master_config* topology, const fc_master_params* params,
    char* answer, std::uint32_t capacity, std::uint32_t* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = answerOut (answer, capacity, written); st != FC_SESSION_OK) return st;
    auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = masterInputs (topology, params); st != FC_SESSION_OK) return st;
    if (joined (source_low, source_high) != slot->session->source().hash
        || joined (revision_low, revision_high) != slot->session->revision()) return FC_SESSION_ERR_STALE;
    if (overlap (answer, capacity, written, sizeof (*written))
        || overlap (topology, sizeof (*topology), answer, capacity)
        || overlap (params, sizeof (*params), answer, capacity)
        || overlap (topology, sizeof (*topology), written, sizeof (*written))
        || overlap (params, sizeof (*params), written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    if (capacity < FC_SESSION_ANSWER_BYTES) return FC_SESSION_ERR_TOO_SMALL;
    felitronics::session::command::Master request;
    request.id = joined (command_low, command_high);
    request.source = joined (source_low, source_high); request.revision = joined (revision_low, revision_high);
    if (! masterReady (*topology, *params, slot->session->source(), request.ready)) return FC_SESSION_ERR_CONTRACT;
    char reply[FC_SESSION_ANSWER_BYTES]; std::uint32_t size = 0;
    const auto st = Wire::master (*slot->session, request, reply, size);
    if (g_callState == kPoisoned) return FC_SESSION_ERR_POISONED;
    if (st != CodecStatus::Ok) return status (st);
    std::copy_n (reply, size, answer); *written = size; return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_master_audio_size (fc_session session,
    const fc_session_master_token* token, double* bytes, std::uint32_t* frames,
    std::uint32_t* channels, std::uint32_t* rate)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (bytes, sizeof (*bytes), alignof (double)); st != FC_SESSION_OK) return st;
    for (auto* output : { frames, channels, rate })
        if (const auto st = pointer (output, sizeof (*output), alignof (std::uint32_t)); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = tokenInput (token); st != FC_SESSION_OK) return st;
    if (overlap (bytes, sizeof (*bytes), token, sizeof (*token))
        || overlap (frames, sizeof (*frames), token, sizeof (*token))
        || overlap (channels, sizeof (*channels), token, sizeof (*token))
        || overlap (rate, sizeof (*rate), token, sizeof (*token))
        || overlap (bytes, sizeof (*bytes), frames, sizeof (*frames))
        || overlap (bytes, sizeof (*bytes), channels, sizeof (*channels))
        || overlap (bytes, sizeof (*bytes), rate, sizeof (*rate))
        || overlap (frames, sizeof (*frames), channels, sizeof (*channels))
        || overlap (frames, sizeof (*frames), rate, sizeof (*rate))
        || overlap (channels, sizeof (*channels), rate, sizeof (*rate))) return FC_SESSION_ERR_OVERLAP;
    const auto owned = slot->session->viewMaster (unpack (*token));
    if (owned.empty()) return FC_SESSION_ERR_STALE;
    if (overlap (bytes, sizeof (*bytes), owned.data(), owned.size_bytes())
        || overlap (frames, sizeof (*frames), owned.data(), owned.size_bytes())
        || overlap (channels, sizeof (*channels), owned.data(), owned.size_bytes())
        || overlap (rate, sizeof (*rate), owned.data(), owned.size_bytes())) return FC_SESSION_ERR_OVERLAP;
    const auto shape = slot->session->masterAudioShape (unpack (*token));
    *bytes = double (owned.size_bytes());
    *frames = std::uint32_t (shape.frames);
    *channels = shape.channels;
    *rate = shape.sampleRate;
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_master_audio_copy (fc_session session,
    const fc_session_master_token* token, float* output, std::uint32_t sample_capacity)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (output, std::uint64_t (sample_capacity) * sizeof (float), alignof (float)); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = tokenInput (token); st != FC_SESSION_OK) return st;
    if (overlap (output, std::uint64_t (sample_capacity) * sizeof (float), token, sizeof (*token))) return FC_SESSION_ERR_OVERLAP;
    const auto retained = slot->session->viewMaster (unpack (*token));
    if (overlap (output, std::uint64_t (sample_capacity) * sizeof (float), retained.data(), retained.size_bytes()))
        return FC_SESSION_ERR_OVERLAP;
    const auto state = slot->session->copyMaster (unpack (*token), { output, sample_capacity });
    if (state == felitronics::session::MasterTransferStatus::TooSmall) return FC_SESSION_ERR_TOO_SMALL;
    return state == felitronics::session::MasterTransferStatus::Ok ? FC_SESSION_OK : FC_SESSION_ERR_STALE;
}

FC_EXPORT fc_session_status fc_session_master_audio_release (fc_session session, const fc_session_master_token* token)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = tokenInput (token); st != FC_SESSION_OK) return st;
    return slot->session->releaseMaster (unpack (*token)) == felitronics::session::MasterTransferStatus::Ok
        ? FC_SESSION_OK : FC_SESSION_ERR_STALE;
}

FC_EXPORT fc_session_status fc_session_master_audio_view (fc_session session,
    const fc_session_master_token* token, const float** output, std::uint32_t* samples)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (output, sizeof (*output), alignof (const float*)); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (samples, sizeof (*samples), alignof (std::uint32_t)); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = tokenInput (token); st != FC_SESSION_OK) return st;
    if (overlap (output, sizeof (*output), samples, sizeof (*samples))
        || overlap (output, sizeof (*output), token, sizeof (*token))
        || overlap (samples, sizeof (*samples), token, sizeof (*token))) return FC_SESSION_ERR_OVERLAP;
    const auto view = slot->session->viewMaster (unpack (*token));
    if (view.empty()) return FC_SESSION_ERR_STALE;
    if (overlap (output, sizeof (*output), view.data(), view.size_bytes())
        || overlap (samples, sizeof (*samples), view.data(), view.size_bytes())) return FC_SESSION_ERR_OVERLAP;
    if (view.size() > UINT32_MAX) return FC_SESSION_ERR_CONTRACT;
    *output = view.data(); *samples = std::uint32_t (view.size()); return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_master_wav_size (fc_session session,
    const fc_session_master_token* token, double* bytes, std::uint32_t* bits)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (bytes, sizeof (*bytes), alignof (double)); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (bits, sizeof (*bits), alignof (std::uint32_t)); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = tokenInput (token); st != FC_SESSION_OK) return st;
    if (overlap (bytes, sizeof (*bytes), bits, sizeof (*bits))
        || overlap (bytes, sizeof (*bytes), token, sizeof (*token))
        || overlap (bits, sizeof (*bits), token, sizeof (*token))) return FC_SESSION_ERR_OVERLAP;
    const auto identity = unpack (*token);
    if (slot->session->masterAudioBytes (identity) == 0) return FC_SESSION_ERR_STALE;
    const auto pcm = slot->session->viewMaster (identity);
    if (overlap (bytes, sizeof (*bytes), pcm.data(), pcm.size_bytes())
        || overlap (bits, sizeof (*bits), pcm.data(), pcm.size_bytes())) return FC_SESSION_ERR_OVERLAP;
    const auto plan = slot->session->masterWavPlan (identity);
    if (! plan) return FC_SESSION_ERR_CONTRACT;
    *bytes = double (plan.bytes); *bits = plan.bits;
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_session_master_wav_copy (fc_session session,
    const fc_session_master_token* token, std::uint32_t offset_low, std::uint32_t offset_high,
    std::uint8_t* output, std::uint32_t capacity, std::uint32_t* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (output, capacity, alignof (std::uint8_t)); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (written, sizeof (*written), alignof (std::uint32_t)); st != FC_SESSION_OK) return st;
    const auto* slot = lookup (session); if (! slot) return FC_SESSION_ERR_HANDLE;
    if (const auto st = tokenInput (token); st != FC_SESSION_OK) return st;
    if (overlap (output, capacity, written, sizeof (*written))
        || overlap (output, capacity, token, sizeof (*token))
        || overlap (written, sizeof (*written), token, sizeof (*token))) return FC_SESSION_ERR_OVERLAP;
    if (capacity == 0 || capacity > 65536u) return FC_SESSION_ERR_CONTRACT;
    const auto identity = unpack (*token);
    if (slot->session->masterAudioBytes (identity) == 0) return FC_SESSION_ERR_STALE;
    const auto plan = slot->session->masterWavPlan (identity);
    if (! plan) return FC_SESSION_ERR_CONTRACT;
    const auto offset = joined (offset_low, offset_high);
    if (offset >= plan.bytes) return FC_SESSION_ERR_CONTRACT;
    const auto count = std::uint32_t (std::min<std::uint64_t> (capacity, plan.bytes - offset));
    const auto pcm = slot->session->viewMaster (identity);
    if (overlap (output, count, pcm.data(), pcm.size_bytes())
        || overlap (written, sizeof (*written), pcm.data(), pcm.size_bytes())) return FC_SESSION_ERR_OVERLAP;
    const auto state = slot->session->copyMasterWav (identity, offset, { output, count });
    if (state != felitronics::session::MasterTransferStatus::Ok) return FC_SESSION_ERR_CONTRACT;
    *written = count;
    return FC_SESSION_OK;
}

//==============================================================================
// THE PURE KIT (fc_session_abi.h, "THE PURE KIT"): no handle, no state — the poison, the checks of what a page hands
// over in the header's order, and felitronics::session::Kit's answer, mapped. Nothing here computes.
namespace
{
using felitronics::session::Kit;
namespace text = felitronics::session::text;

static_assert (FC_SESSION_KIT_FIELD_TARGET_LUFS == unsigned (text::Term::FieldTargetLufs));
static_assert (FC_SESSION_KIT_FIELD_TARGET_TP == unsigned (text::Term::FieldTargetTp));
static_assert (FC_SESSION_KIT_FIELD_HPF_FQ == unsigned (text::Term::FieldHpfFq));
static_assert (FC_SESSION_KIT_FIELD_HPF_SLOPE == unsigned (text::Term::FieldHpfSlope));
static_assert (FC_SESSION_KIT_FIELD_MONO_BASS_FQ == unsigned (text::Term::FieldMonoBassFq));
static_assert (FC_SESSION_KIT_FIELD_MONO_BASS_WIDTH == unsigned (text::Term::FieldMonoBassWidth));
static_assert (FC_SESSION_KIT_FIELD_GLUE_UP_TO_DB == unsigned (text::Term::FieldGlueUpToDb));
static_assert (FC_SESSION_KIT_FIELD_SATURATION_DRIVE == unsigned (text::Term::FieldSaturationDrive));
static_assert (FC_SESSION_KIT_FIELD_SATURATION_MIX == unsigned (text::Term::FieldSaturationMix));
static_assert (FC_SESSION_KIT_FIELD_TILT_DB == unsigned (text::Term::FieldTiltDb));
static_assert (FC_SESSION_KIT_FIELD_LIMITER_NEEDLES_DB == unsigned (text::Term::FieldLimiterNeedlesDb));
static_assert (FC_SESSION_KIT_FIELD_LOW_DB == unsigned (text::Term::FieldLowDb));
static_assert (FC_SESSION_KIT_FIELD_BANDS_BODY == unsigned (text::Term::FieldBandsBody));
static_assert (FC_SESSION_KIT_FIELD_BANDS_MUD == unsigned (text::Term::FieldBandsMud));
static_assert (FC_SESSION_KIT_FIELD_BANDS_FORWARD == unsigned (text::Term::FieldBandsForward));
static_assert (FC_SESSION_KIT_FIELD_BANDS_BRIGHTNESS == unsigned (text::Term::FieldBandsBrightness));
static_assert (FC_SESSION_KIT_FIELD_BANDS_AIR == unsigned (text::Term::FieldBandsAir));
static_assert (FC_SESSION_DEVICE_EQ_BANDS == 1u << unsigned (felitronics::session::Device::Bands));
static_assert ((FC_SESSION_DEVICES_ALL | FC_SESSION_DEVICE_EQ_BANDS) == felitronics::session::kAllDevices);
static_assert (FC_SESSION_KIT_PARSE_ACCEPTED == unsigned (felitronics::session::KitRefusal::None));
static_assert (FC_SESSION_KIT_PARSE_NOT_A_NUMBER == unsigned (felitronics::session::KitRefusal::NotANumber));
static_assert (FC_SESSION_KIT_PARSE_OUT_OF_DOMAIN == unsigned (felitronics::session::KitRefusal::OutOfDomain));
static_assert (FC_SESSION_KIT_ZONES == felitronics::session::kKitZones);
static_assert (FC_SESSION_KIT_EQ_POINTS == felitronics::session::kEqCurvePoints);

// A field id the kit can be asked about: a Term's 16 bits. Which of them it answers is the kit's.
bool fieldOf (std::uint32_t field, text::Term& out) noexcept
{
    if (field > 0xFFFFu) return false;
    out = text::Term (field);
    return true;
}

bool langOf (const char* lang, std::uint32_t bytes, text::Lang& out) noexcept
{
    const auto found = text::Text::langOf ({ lang, bytes });
    if (! found) return false;
    out = *found;
    return true;
}

fc_session_status doublesOut (const double* out, std::uint64_t count) noexcept
{
    return pointer (out, count * sizeof (double), alignof (double));
}
} // namespace

FC_EXPORT fc_session_status fc_kit_text (const char* fact, std::uint32_t fact_bytes, const char* lang, std::uint32_t lang_bytes,
                                        char* output, std::uint32_t capacity, std::uint32_t* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (output, capacity, 1, true); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (written, sizeof (*written), alignof (std::uint32_t)); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (fact, fact_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (lang, lang_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (output, capacity, written, sizeof (*written)) || overlap (fact, fact_bytes, output, capacity)
        || overlap (fact, fact_bytes, written, sizeof (*written)) || overlap (lang, lang_bytes, output, capacity)
        || overlap (lang, lang_bytes, written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    text::Lang language {};
    if (! langOf (lang, lang_bytes, language)) return FC_SESSION_ERR_CONTRACT;
    const auto answer = Kit::text ({ fact, fact_bytes }, language, { output, capacity });
    if (answer.status == CodecStatus::Ok || answer.status == CodecStatus::TooSmall) *written = std::uint32_t (answer.count);
    return status (answer.status);
}

FC_EXPORT fc_session_status fc_kit_parse (const char* typed, std::uint32_t typed_bytes, const char* lang, std::uint32_t lang_bytes,
                                         std::uint32_t field, std::uint32_t source_rate, double* value, std::uint32_t* refusal)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = doublesOut (value, 1); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (refusal, sizeof (*refusal), alignof (std::uint32_t)); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (typed, typed_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (lang, lang_bytes, 1, true); st != FC_SESSION_OK) return st;
    if (overlap (value, sizeof (*value), refusal, sizeof (*refusal)) || overlap (typed, typed_bytes, value, sizeof (*value))
        || overlap (typed, typed_bytes, refusal, sizeof (*refusal)) || overlap (lang, lang_bytes, value, sizeof (*value))
        || overlap (lang, lang_bytes, refusal, sizeof (*refusal))) return FC_SESSION_ERR_OVERLAP;
    text::Lang language {};
    text::Term term {};
    if (! langOf (lang, lang_bytes, language) || ! fieldOf (field, term)) return FC_SESSION_ERR_CONTRACT;
    const auto answer = Kit::parse ({ typed, typed_bytes }, language, term, source_rate);
    if (answer.status != CodecStatus::Ok) return status (answer.status);
    *refusal = unsigned (answer.refusal);
    *value = answer.refusal == felitronics::session::KitRefusal::None ? answer.value : 0.0;
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_kit_travel (std::uint32_t field, double* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = doublesOut (out, FC_SESSION_KIT_TRAVEL_VALUES); st != FC_SESSION_OK) return st;
    text::Term term {};
    if (! fieldOf (field, term)) return FC_SESSION_ERR_CONTRACT;
    const auto answer = Kit::travel (term);
    if (answer.status != CodecStatus::Ok) return status (answer.status);
    out[0] = answer.from; out[1] = answer.to; out[2] = answer.step;
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_kit_position (std::uint32_t field, double value, double* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = doublesOut (out, 1); st != FC_SESSION_OK) return st;
    text::Term term {};
    if (! fieldOf (field, term)) return FC_SESSION_ERR_CONTRACT;
    const auto answer = Kit::position (term, value);
    if (answer.status != CodecStatus::Ok) return status (answer.status);
    *out = answer.value;
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_kit_value_at (std::uint32_t field, double position, double* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = doublesOut (out, 1); st != FC_SESSION_OK) return st;
    text::Term term {};
    if (! fieldOf (field, term)) return FC_SESSION_ERR_CONTRACT;
    const auto answer = Kit::valueAt (term, position);
    if (answer.status != CodecStatus::Ok) return status (answer.status);
    *out = answer.value;
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_kit_heat (std::uint32_t field, double value, double* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = doublesOut (out, FC_SESSION_KIT_HEAT_VALUES); st != FC_SESSION_OK) return st;
    text::Term term {};
    if (! fieldOf (field, term)) return FC_SESSION_ERR_CONTRACT;
    const auto answer = Kit::heat (term, value);
    if (answer.status != CodecStatus::Ok) return status (answer.status);
    out[0] = answer.heat; out[1] = double (answer.side); out[2] = answer.window ? 1.0 : 0.0;
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_kit_mono_zones (double* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = doublesOut (out, 2 * FC_SESSION_KIT_ZONES); st != FC_SESSION_OK) return st;
    const auto answer = Kit::monoZones();
    if (answer.status != CodecStatus::Ok) return status (answer.status);
    for (std::size_t i = 0; i < FC_SESSION_KIT_ZONES; ++i) { out[2 * i] = answer.zones[i].fromHz; out[2 * i + 1] = answer.zones[i].toHz; }
    return FC_SESSION_OK;
}

FC_EXPORT fc_session_status fc_kit_mono_zones_at (double hz, std::uint32_t* out)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    if (const auto st = pointer (out, sizeof (*out), alignof (std::uint32_t)); st != FC_SESSION_OK) return st;
    *out = Kit::monoZonesAt (hz);
    return FC_SESSION_OK;
}

namespace
{
// The EQ curve from `count` params: the seven of fc_kit_eq_curve, and with thirteen the EQ bands' five gains and tick.
fc_session_status kitEqCurve (const double* params, std::uint32_t count, double rate, double* curve, double* peak)
{
    constexpr std::uint64_t curveBytes = 2 * FC_SESSION_KIT_EQ_POINTS * sizeof (double), peakBytes = FC_SESSION_KIT_EQ_PEAK_VALUES * sizeof (double);
    if (const auto st = doublesOut (curve, 2 * FC_SESSION_KIT_EQ_POINTS); st != FC_SESSION_OK) return st;
    if (const auto st = doublesOut (peak, FC_SESSION_KIT_EQ_PEAK_VALUES); st != FC_SESSION_OK) return st;
    const std::uint64_t paramBytes = count * sizeof (double);
    if (const auto st = pointer (params, paramBytes, alignof (double)); st != FC_SESSION_OK) return st;
    if (overlap (curve, curveBytes, peak, peakBytes) || overlap (params, paramBytes, curve, curveBytes)
        || overlap (params, paramBytes, peak, peakBytes)) return FC_SESSION_ERR_OVERLAP;
    // A tick is 0 or 1 and a slope a whole number of dB/oct inside int32, or the record is no record of the three knobs.
    const auto same = [] (double a, double b) { return a <= b && a >= b; };
    const auto tick = [&] (double v, bool& on) { on = same (v, 1.0); return same (v, 0.0) || on; };
    felitronics::session::KitEq eq;
    if (! tick (params[0], eq.hpf.on) || ! tick (params[3], eq.tilt.on) || ! tick (params[5], eq.low.on)
        || ! (std::fabs (params[2]) < 2147483648.0) || ! same (std::floor (params[2]), params[2])) return FC_SESSION_ERR_CONTRACT;
    eq.hpf.fq = params[1]; eq.hpf.slope = std::int32_t (params[2]);
    eq.tilt.db = params[4]; eq.low.db = params[6];
    if (count == FC_SESSION_KIT_EQ_BANDS_PARAMS)
    {
        if (! tick (params[12], eq.bands.on)) return FC_SESSION_ERR_CONTRACT;
        eq.bands.body = params[7]; eq.bands.mud = params[8]; eq.bands.forward = params[9];
        eq.bands.brightness = params[10]; eq.bands.air = params[11];
    }
    // The points land straight in the caller's doubles: an EqPoint is two of them (the snapshot's row says so).
    static_assert (sizeof (felitronics::session::EqPoint) == 2 * sizeof (double));
    felitronics::session::EqPoint points[FC_SESSION_KIT_EQ_POINTS];
    const auto answer = Kit::eqCurve (eq, rate, points);
    if (answer.status != CodecStatus::Ok) return status (answer.status);
    for (std::size_t i = 0; i < FC_SESSION_KIT_EQ_POINTS; ++i) { curve[2 * i] = points[i].hz; curve[2 * i + 1] = points[i].db; }
    peak[0] = answer.finding.hz; peak[1] = answer.finding.db; peak[2] = answer.finding.over ? 1.0 : 0.0;
    peak[3] = answer.finding.device == felitronics::session::Device::Low ? double (FC_SESSION_DEVICE_LOW_SHELF)
                                                                          : double (FC_SESSION_DEVICE_TILT);
    return FC_SESSION_OK;
}
} // namespace

FC_EXPORT fc_session_status fc_kit_eq_curve (const double* params, double rate, double* curve, double* peak)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    return kitEqCurve (params, FC_SESSION_KIT_EQ_PARAMS, rate, curve, peak);
}

FC_EXPORT fc_session_status fc_kit_eq_curve_bands (const double* params, double rate, double* curve, double* peak)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    return kitEqCurve (params, FC_SESSION_KIT_EQ_BANDS_PARAMS, rate, curve, peak);
}

FC_EXPORT fc_session_status fc_kit_saturation_curve (std::uint32_t type, double drive_db, double mix, double* curve)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    static_assert (FC_SESSION_KIT_SATURATION_POINTS == felitronics::session::kKitSaturationPoints);
    if (const auto st = doublesOut (curve, 2 * FC_SESSION_KIT_SATURATION_POINTS); st != FC_SESSION_OK) return st;
    // A number past the enumeration is no type: refused as the kit refuses a type no person may pick.
    if (type > 255u) return FC_SESSION_ERR_CONTRACT;
    const auto answer = Kit::saturationCurve (felitronics::session::SaturationType (type), drive_db, mix,
                                              { curve, 2 * std::size_t (FC_SESSION_KIT_SATURATION_POINTS) });
    return status (answer.status);
}

FC_EXPORT fc_session_status fc_kit_low_end_curve (const double* centre_hz, const double* energy, std::uint32_t bands,
                                                 double from_hz, double to_hz, double* output, std::uint32_t capacity,
                                                 std::uint32_t* written)
{
    const CallGuard call;
    if (call.refused()) return FC_SESSION_ERR_POISONED;
    const std::uint64_t outBytes = std::uint64_t (capacity) * 2 * sizeof (double), inBytes = std::uint64_t (bands) * sizeof (double);
    if (const auto st = pointer (output, outBytes, alignof (double), true); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (written, sizeof (*written), alignof (std::uint32_t)); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (centre_hz, inBytes, alignof (double), true); st != FC_SESSION_OK) return st;
    if (const auto st = pointer (energy, inBytes, alignof (double), true); st != FC_SESSION_OK) return st;
    if (overlap (output, outBytes, written, sizeof (*written)) || overlap (centre_hz, inBytes, output, outBytes)
        || overlap (centre_hz, inBytes, written, sizeof (*written)) || overlap (energy, inBytes, output, outBytes)
        || overlap (energy, inBytes, written, sizeof (*written))) return FC_SESSION_ERR_OVERLAP;
    const auto answer = Kit::lowEndCurve ({ centre_hz, bands }, { energy, bands }, from_hz, to_hz,
        { output, std::size_t (capacity) * 2 });
    if (answer.status == CodecStatus::Ok || answer.status == CodecStatus::TooSmall) *written = std::uint32_t (answer.count);
    return status (answer.status);
}
