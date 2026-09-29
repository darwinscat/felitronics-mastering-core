// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "BuildGuards.h"
#include "LiveMeasurements.h"
#include "MeasurementWorkspace.h"
#include "BuildContract.h"
#include <felitronics/session/Session.h>
#include <felitronics/session/Config.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace felitronics::session
{
namespace
{
MeasurementReason reason (analysis::ProgrammeReason r) noexcept
{
    using R = analysis::ProgrammeReason;
    switch (r)
    {
        case R::None: return MeasurementReason::None;
        case R::MonoProgramme: case R::NoStereoFrames: return MeasurementReason::Unsupported;
        case R::SilentProgramme: case R::ZeroDenominator: return MeasurementReason::NoSignal;
        case R::NoFiniteSamples: case R::NonFiniteInput: case R::NonFiniteIntermediate: return MeasurementReason::NonFinite;
        case R::ShorterThanTailWindow: case R::ShorterThanLoudnessWindow: case R::TooFewObservations: return MeasurementReason::TooShort;
        case R::LoudnessCapacityExceeded: return MeasurementReason::Capacity;
    }
    detail::storageOverflow();
}
void number (detail::LiveMeasurements& live, unsigned analyzer, std::string_view name, int channel,
             std::optional<double> value, MeasurementReason why = MeasurementReason::None, unsigned nativeReason = 0) noexcept
{
    const auto n = live.numberCount[analyzer]++;
    detail::debugBound (n < kMeasurementNumbers && name.size() + 3 < kMeasurementNameBytes);
    auto* text = live.names[analyzer][n];
    std::copy (name.begin(), name.end(), text);
    auto length = name.size();
    if (channel >= 0) { text[length++] = '['; text[length++] = char ('0' + channel); text[length++] = ']'; }
    live.numbers[analyzer][n] = { { text, length }, value, why, nativeReason };
}
}
void Session::stepMeasurements() noexcept
{
    auto& live = *liveMeasurements_;
    auto& work = *measurementWorkspace_;
    const Pcm pcm { nullptr, source_.channels, source_.frames, source_.sampleRate };
    const auto rate = source_.sampleRate;
    const auto channels = source_.channels;
    const auto firstEvent = eventCount_;
    Notification event; event.jobId = measurementJob_;
    const auto publish = [&] (unsigned index)
    {
        auto& r = measurementResults_[index];
        r.numbers = { live.numbers[index], live.numberCount[index] };
        r.framesRead = live.frames;
        ++revision_;
        event.kind = EventKind::Measurement;
        event.payload.measurement = { r.analyzer, r.status, r.reason, r.key, source_.hash,
                                      revision_, r.framesRead, r.total, r.stored, r.complete };
        emit (event);
    };
    if (live.stage < 3)
    {
        const auto plan = detail::MeasurementPlan::storageFor (pcm, detail::MeasurementPlan::parametersFor (pcm));
        const auto id = detail::MeasurementPlan::streaming[live.stage];
        const auto& demand = plan.analyzers[std::size_t (id)];
        const auto workspace = demand.available ? demand.workspace + detail::MeasurementPlan::workspaceAllowance : 0;
        const auto rows = demand.rowValues == 0 ? 0 : demand.rowValues * sizeof (double) + detail::MeasurementPlan::rowAllowance;
        Checked request; request.bytes = workspace + rows;
        request.largestBlockBytes = std::max (workspace, rows);
        const auto checked = this->demand (request);
        // The source phase's policy: a refused instrument is unavailable for memory and the job goes on without it —
        // never the same refusal again on every unit. One error names the demand; the stage still advances.
        const bool refused = checked.rejection != Rejection::None;
        const bool prepared = demand.available && ! refused;
        if (refused)
        {
            auto& r = measurementResults_[std::size_t (id)];
            r.status = MeasurementStatus::Unavailable; r.reason = MeasurementReason::Memory;
            event.kind = EventKind::Error; event.payload.error.code = ErrorCode::Memory;
            event.payload.error.needBytes = checked.needBytes; event.payload.error.recover = Recover::Continue;
            (void) event.payload.error.fact.assign (text::Fact::of (text::FactId::SessionMemory)); emit (event);
        }
        if (prepared && ! work.prepare (id, pcm, plan)) detail::storageOverflow();
        if (live.stage == 0)
        {
            live.hop = 10u * std::max<std::uint64_t> (1, std::uint64_t (std::floor (double (rate) * 0.01 + 0.5)));
            live.rowCapacity = prepared ? source_.frames / live.hop + 1 : 0;
            if (live.rowCapacity != 0) live.loudness.reset (new double[std::size_t (4 * live.rowCapacity)]);
            if (prepared) live.bytes += rows;
            constexpr std::string_view names[] { "momentary", "shortTerm", "momentaryReasons", "shortTermReasons" };
            for (unsigned i = 0; i < 4; ++i)
                live.arrays[0][i] = { names[i], { live.hop, live.hop, 0, rate }, 1, 0, 0, false, {} };
            measurementResults_[0].arrays = live.arrays[0];
        }
        if (live.stage == 1)
        {
            live.clipCapacity = prepared ? std::uint64_t (plan.parameters.clipRuns) : 0;
            if (live.clipCapacity != 0) live.clips.reset (new double[std::size_t (live.clipCapacity * 6)]);
            if (prepared) live.bytes += rows;
            live.arrays[1][0] = { "clips", { 0, 0, 0, rate }, 6, 0, 0, false, {} };
            measurementResults_[1].arrays = { live.arrays[1], 1 };
        }
        ++live.stage;
    }
    else if (live.stage == 3 || live.stage == 4)
    {
        auto& reading = event.payload.reading;
        if (work.clipping && live.runs < std::uint64_t (work.clipping->storedRunCount()))
        {
            while (reading.runCount < 4 && live.runs < std::uint64_t (work.clipping->storedRunCount()))
            {
                const auto run = work.clipping->run (std::int64_t (live.runs));
                double row[] { double (run.start), double (run.length), double (run.channel), double (run.sign), run.level, double (run.evidence) };
                std::copy_n (row, 6, live.clips.get() + std::size_t (6 * live.runs++));
                std::copy_n (row, 6, reading.clips + 6 * reading.runCount);
                reading.runs[reading.runCount++] = { std::uint64_t (run.start), std::uint64_t (run.length), run.level };
            }
        }
        else if (live.stage == 3 && live.frames < source_.frames)
        {
            const auto n = std::min ({ std::uint64_t (1024), source_.frames - live.frames, live.hop - live.frames % live.hop });
            const float* planes[2] { samples_.get() + live.frames, channels == 2 ? samples_.get() + source_.frames + live.frames : nullptr };
            if (work.loudness && ! work.loudness->process (planes, int (channels), int (n))) detail::storageOverflow();
            if (work.clipping && ! work.clipping->process (planes, int (channels), int (n))) detail::storageOverflow();
            if (work.programme && ! work.programme->process (planes, int (channels), int (n))) detail::storageOverflow();
            live.frames += n;
            if (live.frames % live.hop == 0 && work.loudness)
            {
                auto& meter = *work.loudness;
                const double values[] { meter.momentaryLufs(), meter.shortTermLufs() };
                MeasurementReason reasons[2] {};
                for (unsigned i = 0; i < 2; ++i)
                {
                    const auto required = (i == 0 ? 4u : 30u) * live.hop;
                    reasons[i] = meter.nonFiniteSubHops() != 0 ? MeasurementReason::NonFinite
                               : live.frames < required ? MeasurementReason::TooShort
                               : values[i] <= -120.0 ? MeasurementReason::NoSignal : MeasurementReason::None;
                    const double value = reasons[i] == MeasurementReason::None ? values[i]
                        : reasons[i] == MeasurementReason::NoSignal ? -std::numeric_limits<double>::infinity()
                        : std::numeric_limits<double>::quiet_NaN();
                    live.loudness[std::size_t (i * live.rowCapacity + live.rows)] = value;
                    live.loudness[std::size_t ((i + 2) * live.rowCapacity + live.rows)] = double (reasons[i]);
                }
                reading.momentary[0] = { live.rows, live.loudness[std::size_t (live.rows)] };
                reading.shortTerm[0] = { live.rows, live.loudness[std::size_t (live.rowCapacity + live.rows)] };
                reading.momentaryCount = reading.shortTermCount = 1;
                reading.momentaryReason = reasons[0]; reading.shortTermReason = reasons[1];
                ++live.rows;
            }
        }
        else if (live.stage == 3)
        {
            if (work.clipping) work.clipping->finish();
            live.stage = 4;
        }
        else live.stage = 5;
        for (unsigned i = 0; i < 4; ++i)
        {
            auto& a = live.arrays[0][i];
            a.grid.framesRead = live.frames; a.total = a.stored = live.rows;
            a.values = live.rows == 0 ? std::span<const double> {}
                : std::span<const double> (live.loudness.get() + std::size_t (i * live.rowCapacity), std::size_t (live.rows));
        }
        auto& clips = live.arrays[1][0];
        clips.grid.framesRead = live.frames; clips.stored = live.runs;
        clips.total = work.clipping ? std::uint64_t (work.clipping->runCount()) : 0;
        clips.values = { live.clips.get(), std::size_t (6 * live.runs) };
        for (unsigned i = 0; i < 3; ++i) measurementResults_[i].framesRead = live.frames;
        reading.clipCount = reading.runCount;
        reading.grid = { live.hop, live.hop, live.frames, rate };
        reading.totalRuns = clips.total; reading.storedRuns = live.runs;
        reading.runsComplete = ! work.clipping || work.clipping->runsComplete();
        reading.tailFrames = live.frames % live.hop; reading.finished = live.stage == 5;
        event.kind = EventKind::Reading; emit (event);
        if (clips.total != 0 && ! live.clippingWarned)
        {
            live.clippingWarned = true; event.kind = EventKind::Fact;
            (void) event.payload.fact.assign (text::Fact::of (text::FactId::SourceClipping)); emit (event);
        }
    }
    else if (live.stage == 5)
    {
        if (! work.programme || work.programme->finishStep (1024)) ++live.stage;
    }
    else if (live.stage == 6)
    {
        if (work.programme)
        {
            const auto& report = work.programme->report();
            const auto add = [&] (std::string_view name, int channel, const analysis::ProgrammeValue& value)
            { number (live, 2, name, channel, value.valid ? std::optional<double> (value.value) : std::nullopt,
                      value.valid ? MeasurementReason::None : reason (value.reason), unsigned (value.reason)); };
            report.visitValues (add);
            report.visitCounts ([&] (std::string_view name, int channel, std::int64_t value)
            { number (live, 2, name, channel, double (value)); });
            number (live, 2, "sampleRate", -1, report.sampleRate);
            const auto addLoudness = [&] (std::string_view name, const analysis::ProgrammeValue& value)
            { number (live, 0, name, -1, value.valid ? std::optional<double> (value.value) : std::nullopt,
                      value.valid ? MeasurementReason::None : reason (value.reason), unsigned (value.reason)); };
            addLoudness ("integratedLufs", report.integratedLufs); addLoudness ("truePeakDb", report.truePeakDbtp);
            number (live, 0, "tailFrames", -1, double (live.frames % live.hop));
            number (live, 0, "momentaryWindowFrames", -1, double (4 * live.hop));
            number (live, 0, "shortTermWindowFrames", -1, double (30 * live.hop));
        }
        auto& r = measurementResults_[0];
        // Two instruments feed this result. The meter's rows: refused for memory at its preparation (stage 0 marked the
        // result so), they are missing and the result is incomplete. The report's integrated loudness and true peak:
        // they alone decide whether the mandatory readings are usable — a missing row series does not unmake them.
        const bool rowsRefused = r.status == MeasurementStatus::Unavailable && r.reason == MeasurementReason::Memory;
        for (auto& a : live.arrays[0]) a.complete = ! rowsRefused;
        r.total = r.stored = live.rows; r.complete = ! rowsRefused;
        const bool noReport = ! work.programme && measurementResults_[2].reason == MeasurementReason::Memory;
        const bool usable = live.numberCount[0] >= 2 && live.numbers[0][0].value && live.numbers[0][1].value;
        r.status = usable ? MeasurementStatus::Ready : MeasurementStatus::Unavailable;
        r.reason = usable ? MeasurementReason::None : live.numberCount[0] ? live.numbers[0][0].reason
                 : noReport ? MeasurementReason::Memory : MeasurementReason::Unsupported;
        if (! usable && r.reason == MeasurementReason::None) r.reason = live.numbers[0][1].reason;
        publish (0); work.release (Analyzer::Loudness); ++live.stage;
        if (! usable || ! r.complete)
        {
            // Unusable: why. Usable without its rows: incomplete for storage, as a capped clip list says.
            event.kind = EventKind::Fact;
            (void) event.payload.fact.assign (text::Fact::of (MeasurementText::fact (usable ? MeasurementReason::Capacity : r.reason)));
            emit (event);
        }
    }
    else if (live.stage == 7)
    {
        auto& r = measurementResults_[1];
        if (work.clipping)
        {
            const auto& c = *work.clipping;
            number (live, 1, "samplePeak", -1, c.samplePeak());
            number (live, 1, "samplePeakDb", -1, c.samplePeakDb());
            number (live, 1, "runCount", -1, double (c.runCount()));
            number (live, 1, "storedRunCount", -1, double (c.storedRunCount()));
            number (live, 1, "runsComplete", -1, c.runsComplete() ? 1.0 : 0.0);
            number (live, 1, "runCountLowerBound", -1, double (c.runCount()));
            for (int channel = 0; channel < int (channels); ++channel)
            {
                number (live, 1, "samplePeak", channel, c.samplePeak (channel));
                number (live, 1, "dcOffset", channel, c.dcOffset (channel));
                number (live, 1, "finiteSamples", channel, double (c.finiteSamples (channel)));
                number (live, 1, "nonFiniteSamples", channel, double (c.nonFiniteSamples (channel)));
                number (live, 1, "runCount", channel, double (c.runCount (channel)));
                number (live, 1, "clippedSamples", channel, double (c.clippedSamples (channel)));
                number (live, 1, "longestRun", channel, double (c.longestRun (channel)));
            }
            r.status = MeasurementStatus::Ready; r.reason = MeasurementReason::None;
            r.total = std::uint64_t (c.runCount()); r.stored = live.runs; r.complete = c.runsComplete();
            live.arrays[1][0].complete = r.complete;
        }
        publish (1); work.release (Analyzer::Clipping); ++live.stage;
        if (r.status == MeasurementStatus::Ready && ! r.complete)
        {
            event.kind = EventKind::Fact;
            (void) event.payload.fact.assign (text::Fact::of (MeasurementText::fact (MeasurementReason::Capacity))); emit (event);
        }
    }
    else
    {
        auto& r = measurementResults_[2];
        if (work.programme) { r.status = MeasurementStatus::Ready; r.reason = MeasurementReason::None; r.complete = true; }
        publish (2); work.release (Analyzer::Programme); ++live.stage;
        measurementUnit_ = 2;
    }
    ++live.work;
    const bool stream = live.stage < 5;
    measurementProgress_ = { stream ? PhaseName::Stream : PhaseName::Report,
        stream ? 0.1 * double (live.frames) / double (source_.frames) : live.stage >= 9 ? 0.2 : 0.15,
        config::Config::versions().all, 0, 0, std::uint32_t (std::min<std::uint64_t> (live.work, 4294967295u)),
        std::uint32_t (std::min<std::uint64_t> (source_.frames + live.clipCapacity + 32, 4294967295u)) };
    // Publications from this unit describe its completed work, including the last reading/report.
    for (auto i = firstEvent; i < eventCount_; ++i)
    {
        events_[i].phase = measurementProgress_.name;
        events_[i].completedWork = measurementProgress_.completedUnits;
        events_[i].totalWork = measurementProgress_.totalUnits;
    }
    event.kind = EventKind::Phase; event.payload.phase = measurementProgress_; emit (event);
}
}
