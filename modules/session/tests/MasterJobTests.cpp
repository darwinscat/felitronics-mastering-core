// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "../../../tests/DeclaredBudget.h"
#include "Driver.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/analysis/ReferenceTruePeakMeter.h>
#include <felitronics_test.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace declared = felitronics::declared;
namespace felitronics::session::detail
{
struct Inspector
{
    static bool noMasterOwners (const Session& s) noexcept
    {
        return ! s.masterJob_ && ! s.masterAudio_.samples && ! s.masterRows_ && ! s.masters_
            && s.masterRoom_ == 0 && s.masterCount_ == 0 && s.masterJobBytes_ == 0;
    }
};
}

int main()
{
    constexpr std::uint32_t rate = 48000;
    std::vector<float> left (2u * rate), right (2u * rate);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i] = float (int ((i * 17u) % 251u) - 125) / 4096.0f;
        right[i] = float (int ((i * 19u + 7u) % 251u) - 125) / 4096.0f;
    }
    const float* planes[] { left.data(), right.data() };
    auto made = Session::create();
    ok (made.status == Status::Ok, "session created");
    auto& s = *made.session;
    const auto loaded = s.apply (command::Load { 1, { planes, 2, left.size(), rate }, { "test.wav", rate, true, 24 } });
    ok (loaded.rejection == Rejection::None, "source loaded");
    for (unsigned i = 0; i < 20000 && (s.state() == State::Loaded || ! s.snapshot().view().mandatoryMeasurementsReady); ++i)
        (void) s.step (16);
    ok (s.snapshot().view().mandatoryMeasurementsReady, "mandatory LUFS and true peak are ready");
    command::Master ready { 2 };
    ready.ready.version = 1;
    ready.ready.topology.eq = false;
    ready.ready.topology.compressor = false;
    ready.ready.topology.dither = false;
    ready.source = s.source().hash; ready.revision = s.revision();
    const auto declared = s.check (ready);
    Answer started;
    const auto spent = declared::spend ([&] { started = s.apply (ready); });
    ok (declared.rejection == Rejection::None && started.rejection == Rejection::None
        && declared::covers (declared.bytes, spent), "ready master preflight covers preparation");
    const auto recipe = s.jobRecipe();
    const auto target = s.apply (command::SetTarget { 3, "club" });
    ok (target.rejection == Rejection::None && s.jobRecipe().readyHash == recipe.readyHash
        && s.jobRecipe().project.target == recipe.project.target, "project edits do not change the running recipe");
    std::uint64_t largestStep = 0;
    for (unsigned i = 0; i < 40000 && s.job() != 0; ++i)
    {
        const auto work = declared::spend ([&] { (void) s.step (1); });
        largestStep = std::max (largestStep, std::uint64_t (work.bytes));
    }
    ok (s.job() == 0 && s.masters().size() == 1, "real search finished and retained one master");
    if (s.masters().empty()) return felitronics::test::report();
    ok (largestStep <= declared.bytes, "later solver allocations fit the command declaration");
    const auto& kept = s.masters().back();
    ok (kept.recipe.readyHash == recipe.readyHash && kept.landing && kept.landing->passes <= 12,
        "completed metadata keeps the ready recipe and one pass budget");
    ok (kept.landing && kept.landing->deliverable, "safe result retains transferable PCM");
    const auto token = s.pendingMaster();
    const auto shape = s.masterAudioShape (token);
    ok (token.source == s.source().hash && token.job == kept.id && token.master == kept.id
        && shape.frames == left.size() && shape.channels == 2 && shape.sampleRate == rate,
        "transfer identity and delivered shape are published");
    std::vector<float> copy (std::size_t (shape.frames * shape.channels));
    ok (s.copyMaster (token, copy) == MasterTransferStatus::Ok
        && s.copyMaster ({ token.source + 1u, token.revision, token.job, token.master }, copy) == MasterTransferStatus::Stale,
        "copy validates all transfer identity fields");
    float invalidSample = std::numeric_limits<float>::quiet_NaN();
    const float* invalidPlanes[] { &invalidSample, &invalidSample };
    Answer invalidLoad;
    const auto invalidSpent = declared::spend ([&] { invalidLoad = s.apply (command::Load {
        4, { invalidPlanes, 2, 1, rate }, { "invalid.wav", rate, true, 24 } }); });
    ok (invalidLoad.rejection == Rejection::NotFinite && invalidSpent.requests == 0
        && s.pendingMaster().master == token.master && s.masterAudioBytes (token) == copy.size() * sizeof (float),
        "rejected source replacement retains the finished transfer without allocating");

    std::vector<float> otherLeft = left;
    otherLeft[0] += 0.125f;
    const float* otherPlanes[] { otherLeft.data(), right.data() };
    auto otherCreated = Session::create();
    auto& other = *otherCreated.session;
    ok (other.apply (command::Load { 1, { otherPlanes, 2, otherLeft.size(), rate },
        { "other.wav", rate, true, 24 } }).rejection == Rejection::None, "another session loads an independent source");
    for (unsigned i = 0; i < 20000 && (other.state() == State::Loaded
         || ! other.snapshot().view().mandatoryMeasurementsReady); ++i) (void) other.step (16);
    auto otherReady = ready; otherReady.id = 2; otherReady.source = other.source().hash;
    otherReady.revision = other.revision();
    const auto otherStart = other.apply (otherReady);
    ok (otherStart.rejection == Rejection::None && other.apply (command::Cancel { 3, otherStart.job }).rejection == Rejection::None
        && other.job() == 0 && ! other.masterWavPlan (other.pendingMaster())
        && s.masterAudioBytes (token) != 0, "cancel in another session leaves this completed PCM intact and no false file");
    auto invalidReady = otherReady; invalidReady.id = 4; invalidReady.revision = other.revision();
    invalidReady.ready.params.inputGainDb = std::numeric_limits<double>::quiet_NaN();
    Answer invalidMaster;
    const auto refusalSpent = declared::spend ([&] { invalidMaster = other.apply (invalidReady); });
    ok (invalidMaster.rejection == Rejection::NotFinite && refusalSpent.requests == 0
        && other.job() == 0 && ! other.masterWavPlan (other.pendingMaster()),
        "invalid ready request refuses before allocation and creates no downloadable master");

    ok (s.releaseMaster (token) == MasterTransferStatus::Ok
        && s.releaseMaster (token) == MasterTransferStatus::Unknown
        && s.snapshot().view().canMaster, "release is explicit and permits the next master");
    const auto olderMasterId = kept.id;
    auto next = ready; next.id = 5; next.revision = s.revision();
    const auto again = s.apply (next);
    const auto forgottenWhileRunning = s.apply (command::Forget { 51, olderMasterId });
    ok (again.rejection == Rejection::None && forgottenWhileRunning.rejection == Rejection::None
        && s.job() == again.job && s.masters().empty(),
        "forgetting an older master leaves the active render intact");
    for (unsigned i = 0; i < 40000 && s.job() != 0; ++i) (void) s.step (16);
    ok (s.masters().size() == 1 && s.masters().front().landing
        && s.pendingMaster().master == again.job,
        "forgetting an older master preserves active rows through completion");
    const auto secondToken = s.pendingMaster();
    MasterAudio moved;
    ok (again.rejection == Rejection::None && secondToken.master != 0
        && s.takeMaster (secondToken, moved) == MasterTransferStatus::Ok
        && moved.samples && moved.frames == left.size() && s.pendingMaster().master == 0,
        "native take moves owned PCM without a full copy");
    next.id = 6; next.revision = s.revision();
    const auto cancelled = s.apply (next);
    ok (cancelled.rejection == Rejection::None
        && s.apply (command::Cancel { 7, cancelled.job }).rejection == Rejection::None
        && s.masters().size() == 1, "cancel discards only unfinished work and retains completed metadata");
    next.id = 8; next.revision = s.revision();
    const auto replacing = s.apply (next);
    for (unsigned i = 0; i < 40000 && s.job() != 0; ++i) (void) s.step (16);
    const auto oldToken = s.pendingMaster();
    ok (replacing.rejection == Rejection::None && oldToken.master != 0, "a later master is independently transferable");
    const auto replaced = s.apply (command::Load { 9, { otherPlanes, 2, otherLeft.size(), rate },
        { "other.wav", rate, true, 24 } });
    ok (replaced.rejection == Rejection::None && s.masterAudioBytes (oldToken) == 0
        && s.copyMaster (oldToken, copy) != MasterTransferStatus::Ok && s.masters().empty(),
        "new source fences the old transfer and drops its PCM while keeping the moved caller buffer");
    auto unavailableCreated = Session::create();
    auto& unavailable = *unavailableCreated.session;
    const float shortSamples[] { 0.0f, 0.25f, -0.25f, 0.0f };
    const float* shortPlanes[] { shortSamples, shortSamples };
    const auto shortLoad = unavailable.apply (command::Load { 1, { shortPlanes, 2, 4, rate }, {} });
    ok (shortLoad.rejection == Rejection::None
        && detail::Driver::measured1 (unavailable, shortLoad.job, unavailable.source().hash),
        "unavailable mandatory reading fixture reaches a measured state");
    auto impossible = ready; impossible.source = unavailable.source().hash;
    impossible.revision = unavailable.revision();
    const auto refused = unavailable.apply (impossible);
    ok (refused.rejection == Rejection::MandatoryUnavailable && unavailable.pendingMaster().master == 0
        && ! unavailable.masterWavPlan (unavailable.pendingMaster()),
        "unavailable mandatory LUFS or true peak forbids ready PCM");
    for (unsigned i = 0; i < 20000 && (s.state() == State::Loaded
         || ! s.snapshot().view().mandatoryMeasurementsReady); ++i) (void) s.step (16);
    const auto demanding = s.apply (command::EditTarget { 10, { -5.0, -6.0 } });
    auto missReady = ready; missReady.id = 11; missReady.source = s.source().hash;
    missReady.revision = s.revision();
    const auto missStart = s.apply (missReady);
    for (unsigned i = 0; i < 40000 && s.job() != 0; ++i) (void) s.step (16);
    ok (s.masters().size() == 1, "demanding target publishes one retained result");
    if (s.masters().empty()) return felitronics::test::report();
    const auto& miss = s.masters().back();
    ok (demanding.rejection == Rejection::None && missStart.rejection == Rejection::None
        && miss.landing && miss.landing->status == LandingStatus::PassLimit
        && miss.landing->deliverable && miss.landing->passes == 12
        && miss.landing->truePeakDbTp && *miss.landing->truePeakDbTp <= -6.0
        && s.pendingMaster().master == miss.id && s.masterWavPlan (s.pendingMaster()),
        "an unreachable loudness goal retains the best ceiling-safe PCM after twelve passes");
    const auto missToken = s.pendingMaster();
    const auto filePlan = s.masterWavPlan (missToken);
    std::vector<std::uint8_t> file (std::size_t (filePlan.bytes), 0u);
    bool fileCopied = bool (filePlan);
    for (std::size_t at = 0; fileCopied && at < file.size(); at += 4093u)
        fileCopied &= s.copyMasterWav (missToken, at,
            { file.data() + at, std::min<std::size_t> (4093u, file.size() - at) }) == MasterTransferStatus::Ok;
    ok (fileCopied && filePlan.bits == 24 && file.size() == 44u + filePlan.dataBytes + (filePlan.dataBytes & 1u),
        "safe miss yields one complete WAV through bounded copies");
    if (fileCopied && filePlan.bits == 24)
    {
        std::vector<float> decoded (std::size_t (filePlan.frames * filePlan.channels));
        for (std::size_t frame = 0; frame < filePlan.frames; ++frame)
            for (std::size_t channel = 0; channel < filePlan.channels; ++channel)
            {
                const auto at = 44u + (frame * filePlan.channels + channel) * 3u;
                const auto raw = std::uint32_t (file[at]) | (std::uint32_t (file[at + 1]) << 8)
                    | (std::uint32_t (file[at + 2]) << 16);
                const auto code = std::int32_t (raw) - ((raw & 0x800000u) ? 0x1000000 : 0);
                decoded[channel * filePlan.frames + frame] = float (double (code) / 8388608.0);
            }
        felitronics::analysis::ReferenceTruePeakMeter meter;
        bool measured = meter.prepare (filePlan.rate, 1024, int (filePlan.channels));
        for (std::size_t at = 0; measured && at < filePlan.frames; at += 1024u)
        {
            const float* block[] { decoded.data() + at, decoded.data() + filePlan.frames + at };
            measured &= meter.process (block, int (filePlan.channels),
                int (std::min<std::size_t> (1024u, filePlan.frames - at)));
        }
        if (measured) meter.drain();
        ok (measured && miss.report && meter.truePeakDb() <= miss.report->ceilingDbTp,
            "decoded safe-miss WAV respects the reference true-peak ceiling");
    }
    const auto sidecarToken = s.pendingMaster();
    const auto sidecarBefore = s.liveBytes();
    MeasuredSource replacementFacts { "sidecar.wav", s.source().hash + 1u, left.size(), rate, 2, rate, 24,
        true, -18.0, -2.0 };
    const auto sidecarDemand = s.loadMeasuredStorage (replacementFacts);
    Answer sidecarLoaded;
    const auto sidecarSpent = declared::spend ([&] { sidecarLoaded = s.loadMeasured (12, replacementFacts); });
    ok (sidecarDemand.rejection == Rejection::None && sidecarLoaded.rejection == Rejection::None
        && declared::covers (sidecarDemand.bytes, sidecarSpent)
        && s.pendingMaster().master == 0 && s.masterAudioBytes (sidecarToken) == 0
        && s.masters().empty() && detail::Inspector::noMasterOwners (s)
        && ! s.snapshot().view().canMaster && s.snapshot().view().pendingMasterBytes == 0.0
        && s.liveBytes() < sidecarBefore - double (left.size() * 2u * sizeof (float)),
        "sidecar replacement releases the prior PCM, token, rows and job owners");
    std::vector<float> transient (2u * rate, 0.003f);
    transient.back() = 1.0f;
    const float* transientPlanes[] { transient.data(), transient.data() };
    auto unsafeCreated = Session::create();
    auto& unsafe = *unsafeCreated.session;
    ok (unsafe.apply (command::Load { 1, { transientPlanes, 2, transient.size(), rate }, {} }).rejection == Rejection::None,
        "transient source loads for true-peak refusal");
    for (unsigned i = 0; i < 20000 && (unsafe.state() == State::Loaded
         || ! unsafe.snapshot().view().mandatoryMeasurementsReady); ++i) (void) unsafe.step (16);
    const auto sourceFacts = unsafe.snapshot();
    double inputLufs = std::numeric_limits<double>::quiet_NaN();
    for (const auto& number : sourceFacts.view().measurements[std::size_t (Analyzer::Loudness)].numbers)
        if (number.name == "integratedLufs" && number.value) inputLufs = *number.value;
    const auto unsafeTarget = unsafe.apply (command::EditTarget { 2, { -14.0, -6.0 } });
    auto unsafeReady = ready; unsafeReady.id = 3; unsafeReady.source = unsafe.source().hash;
    unsafeReady.revision = unsafe.revision(); unsafeReady.ready.topology.limiter = false;
    unsafeReady.ready.params.inputGainDb = 59.0 - (-18.0 - inputLufs);
    const auto unsafeStart = unsafe.apply (unsafeReady);
    for (unsigned i = 0; i < 40000 && unsafe.job() != 0; ++i) (void) unsafe.step (16);
    ok (unsafeTarget.rejection == Rejection::None && unsafeStart.rejection == Rejection::None
        && unsafe.masters().size() == 1 && unsafe.masters().back().landing
        && ! unsafe.masters().back().landing->deliverable && unsafe.pendingMaster().master == 0
        && ! unsafe.masterWavPlan (unsafe.pendingMaster()),
        "true-peak violation across all candidates yields no transferable PCM");
    // ...and the verdict says why (slice 5): the target out of reach, the true-peak ceiling holding it — fact 89 with its
    // limit — where it used to say nothing at all (Unavailable).
    if (! unsafe.masters().empty() && unsafe.masters().back().landing && unsafe.masters().back().report)
    {
        const auto& lost = *unsafe.masters().back().landing;
        const auto verdict = MasterReportText::landing (*unsafe.masters().back().report, lost, 0.1);
        ok (lost.status == LandingStatus::TargetUnreachable && lost.binding == LandingConstraint::TruePeakCeiling
            && verdict && verdict->id == text::FactId::MasterLandingUnreachable && verdict->argCount == 2
            && verdict->args[1].termId == text::Term::LandingLimitTruePeak,
            "no render under the ceiling: unreachable, the true-peak ceiling named, fact 89");
    }
    else ok (false, "PRECONDITION: the unsafe master keeps its landing and report");
    std::printf ("wav-outcomes=cancel:false,refusal:false,unavailable:false,miss:true,unsafe:false\n");
    return felitronics::test::report();
}
