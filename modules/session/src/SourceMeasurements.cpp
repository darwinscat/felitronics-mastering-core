// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "BuildGuards.h"
#include "SourceMeasurements.h"
#include "MeasurementWorkspace.h"
#include "Driver.h"
#include "BuildContract.h"
#include "Rules.h"
#include <felitronics/session/Session.h>
#include <felitronics/session/Config.h>
#include <felitronics/core/DetMath.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace felitronics::session
{
namespace
{
using Store = detail::MeasurementStore;
using Workspace = detail::MeasurementWorkspace;
analysis::LowEnd* lowEnd (Workspace& w, Analyzer id) noexcept
{
    return id == Analyzer::LowEnd ? w.lowEnd.get() : id == Analyzer::LowEnd150 ? w.lowEnd150.get() : w.infraLow.get();
}
bool isLowEnd (Analyzer id) noexcept { return id == Analyzer::LowEnd || id == Analyzer::LowEnd150 || id == Analyzer::InfraLow; }
double configured (toml::embedded::View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto i = v.integer()) return double (*i);
    detail::storageOverflow();
}
void process (Workspace& w, Analyzer id, const float* const* planes, int channels, int count) noexcept
{
    bool ok = false;
    if (isLowEnd (id)) ok = lowEnd (w, id)->process (planes, channels, count);
    else switch (id)
    {
        case Analyzer::Forensics: ok = w.forensics->process (planes, channels, count); break;
        case Analyzer::Stereo: ok = w.stereo->process (planes, channels, count); break;
        case Analyzer::Crest: ok = w.crest->process (planes, channels, count); break;
        case Analyzer::Hum: ok = w.hum->process (planes, channels, count); break;
        case Analyzer::StereoBursts: ok = w.bursts->process (planes, channels, count); break;
        case Analyzer::Tempo: ok = w.tempo->process (planes, channels, count); break;
        case Analyzer::Loudness: case Analyzer::Clipping: case Analyzer::Programme: case Analyzer::LowEnd:
        case Analyzer::InfraLow: case Analyzer::LowEnd150: case Analyzer::Waveform: case Analyzer::Excursions: break;
    }
    if (! ok) detail::storageOverflow();
}
bool finish (Workspace& w, Analyzer id, Store& out, detail::SourceMeasurements& run, const Source& source,
             const detail::MeasurementParameters& params) noexcept
{
    if (isLowEnd (id))
    {
        auto& a = *lowEnd (w, id); (void) a.finish();
        const auto config = detail::rules().engine.find ("lowEnd");
        detail::SourceResults::lowEnd (out, a, configured (config.find ("occupiedFromDuty")), configured (config.find ("occupiedMarginWhenOnDb")));
    }
    else if (id == Analyzer::Forensics) { (void) w.forensics->finish(); detail::SourceResults::forensics (out, *w.forensics, source.bitDepth); }
    else if (id == Analyzer::Hum) { (void) w.hum->finish(); detail::SourceResults::hum (out, *w.hum); }
    else if (id == Analyzer::StereoBursts) { (void) w.bursts->finish(); detail::SourceResults::bursts (out, *w.bursts); }
    else if (id == Analyzer::Stereo)
    {
        const auto& a = *w.stereo;
        const auto reason = run.stereoFinite == 0 || run.stereo.ll + run.stereo.rr <= 0 ? MeasurementReason::NoSignal : MeasurementReason::None;
        out.number ("mono", source.channels == 1 ? 1 : 0); out.number ("dualMono", source.channels == 2 && run.dualMono ? 1 : 0);
        out.number ("finiteFrames", double (run.stereoFinite)); out.number ("nonFiniteFrames", double (run.stereoNonFinite));
        out.number ("columns", a.columns()); out.number ("maxRms", a.maxRms());
        out.number ("width", run.stereo.width (run.stereoFinite), -1, reason); out.number ("correlation", run.stereo.correlation(), -1, reason);
        out.number ("rms", run.stereo.rms (run.stereoFinite), -1, reason);
        double minimum = 1;
        for (const auto value : a.correlation()) minimum = std::min (minimum, double (value));
        out.number ("minimumCorrelation", minimum, -1, reason);
        (void) out.array ("columns", 3, std::uint64_t (a.columns()), std::uint64_t (a.columns()), true, { 0, 0, source.frames, source.sampleRate });
    }
    else if (id == Analyzer::Crest)
    {
        auto& a = *w.crest; (void) a.finish();
        out.number ("sampleRate", double (a.sampleRate())); out.number ("hopSamples", double (a.hopSamples())); out.number ("blockHops", double (a.blockHops())); out.number ("blockCount", double (a.blockCount())); out.number ("basePeakHops", double (a.basePeakHops()));
        out.number ("droppedHops", double (a.droppedHops())); out.number ("samplesProcessed", double (a.samplesProcessed())); out.number ("nonFiniteSamples", double (a.nonFiniteSamples())); out.number ("overflowedSamples", double (a.overflowedSamples()));
        out.number ("narrowedSamples", double (a.narrowedSamples())); out.number ("widestChannels", double (a.widestChannels())); out.number ("firstNonFiniteAt", double (a.firstNonFiniteAt())); out.number ("invalidReason", double (a.invalidReason()));
        for (int b = 0; b < 3; ++b) out.number ("bandEdgeHz", params.crest.bandEdgeHz[b], b);
        out.number ("configuredHopMs", params.crest.hopMs);
        out.number ("configuredProgrammeFloorDb", params.crest.programmeFloorDb);
        out.number ("bandShareFloorDb", params.crest.bandShareFloorDb);
        const auto count = std::uint64_t (a.blockCount());
        const MeasurementGrid grid { 0, std::uint64_t (a.hopSamples()), source.frames, source.sampleRate };
        (void) out.array ("blocks", 10, count, count, a.droppedHops() == 0, grid);
        (void) out.array ("active", 5, count, count, a.droppedHops() == 0, grid);
        (void) out.array ("oversampledMeanSquare", 1, count, count, a.droppedHops() == 0, grid);
    }
    else if (id == Analyzer::Tempo)
    {
        if (! w.tempo->finishStep()) return false;
        detail::SourceResults::tempo (out, *w.tempo, source.sampleRate, source.frames);
    }
    return true;
}
// Only the unwritten rows of each array are touched. Every unit copies at most 128 source records.
bool copy (Workspace& w, Analyzer id, Store& out, detail::SourceMeasurements& run) noexcept
{
    const auto begin = run.copied;
    const auto limit = begin + 128;
    if (isLowEnd (id))
    {
        const auto& a = *lowEnd (w, id);
        const auto end = std::min (limit, std::uint64_t (a.storedBlockCount()));
        for (; run.copied < end; ++run.copied)
        {
            const auto b = a.block (std::int64_t (run.copied));
            const double row[] { double (b.start), double (b.samples), double (b.finiteSamples), double (b.holes), b.midEnergy, b.sideEnergy, double (b.valid), double (b.index) };
            std::copy_n (row, 8, out.rows.get() + std::size_t (run.copied * 8));
        }
        return run.copied == std::uint64_t (a.storedBlockCount());
    }
    if (id == Analyzer::Forensics)
    {
        const auto& a = *w.forensics;
        const auto end = std::min (limit, std::uint64_t (a.bins()));
        for (; run.copied < end; ++run.copied)
            for (int c = 0; c < a.channels(); ++c) out.rows[std::size_t (run.copied * std::uint64_t (a.channels()) + std::uint64_t (c))] = a.meanPower (c, int (run.copied));
        return run.copied == std::uint64_t (a.bins());
    }
    if (id == Analyzer::Stereo)
    {
        const auto& a = *w.stereo;
        const auto end = std::min (limit, std::uint64_t (a.columns()));
        for (; run.copied < end; ++run.copied)
        {
            const auto i = std::size_t (run.copied);
            out.rows[i * 3] = a.width()[i]; out.rows[i * 3 + 1] = a.correlation()[i]; out.rows[i * 3 + 2] = a.rms()[i];
        }
        return run.copied == std::uint64_t (a.columns());
    }
    if (id == Analyzer::Crest)
    {
        const auto& a = *w.crest;
        const auto count = std::uint64_t (a.blockCount()), end = std::min (limit, count);
        const auto gate = core::det::pow10 (analysis::BandCrest::kProgrammeGateDb / 10);
        for (; run.copied < end; ++run.copied)
        {
            const auto i = run.copied;
            for (int b = 0; b < 5; ++b)
            {
                out.rows[std::size_t (i * 10 + std::uint64_t (b * 2))] = a.blockPeakLin (static_cast<long long> (i), b);
                out.rows[std::size_t (i * 10 + std::uint64_t (b * 2 + 1))] = a.blockMeanSq (static_cast<long long> (i), b);
            }
            out.rows[std::size_t (count * 15 + i)] = a.blockOversampledMeanSq (static_cast<long long> (i));
            const double ms = out.rows[std::size_t (i * 10 + 9)];
            if (std::isfinite (ms) && ms >= gate) { run.crestSum += ms; ++run.crestCount; }
        }
        return run.copied == count;
    }
    if (id == Analyzer::Hum)
    {
        const auto& a = *w.hum;
        const auto count = out.arrays[2].stored, end = std::min (limit, count);
        auto* rows = out.rows.get() + out.arrays[0].values.size() + out.arrays[1].values.size();
        for (; run.copied < end; ++run.copied)
        {
            const auto left = std::uint64_t (a.storedStretchCount (0));
            const int c = run.copied < left ? 0 : 1;
            const auto v = a.stretch (c, std::int64_t (c == 0 ? run.copied : run.copied - left));
            const double row[] { double (c), double (v.index), double (v.startSample), double (v.endSample), double (v.frames) };
            std::copy_n (row, 5, rows + std::size_t (run.copied * 5));
        }
        return run.copied == count;
    }
    if (id == Analyzer::StereoBursts)
    {
        const auto& a = *w.bursts;
        const auto left = std::uint64_t (a.axis (0).storedEventCount());
        const auto count = left + std::uint64_t (a.axis (1).storedEventCount()), end = std::min (limit, count);
        for (; run.copied < end; ++run.copied)
        {
            const int axis = run.copied < left ? 0 : 1;
            const auto i = std::int64_t (axis == 0 ? run.copied : run.copied - left);
            const auto v = a.axis (axis).event (i); const auto other = a.crossAt (axis, i);
            const double row[] { double (v.start), double (v.length), double (v.peakAt), v.peakPower, v.peakBaseline,
                v.peakExcessDb, v.peakWidePower, v.energy, double (v.hops), double (v.touchedNonFinite),
                double (v.baselineTouchedNonFinite), double (v.closedByFinish), other.power, other.baseline, double (other.eligible), double (other.hop) };
            auto* rows = out.rows.get() + (out.arrays[axis * 2].values.data() - out.rows.get());
            std::copy_n (row, 16, rows + std::size_t (i) * 16);
        }
        return run.copied == count;
    }
    if (id == Analyzer::Tempo)
    {
        const auto& a = *w.tempo;
        if (run.copied == 0)
        {
            for (std::uint64_t i = 0; i < out.arrays[0].stored; ++i)
            {
                const auto c = a.candidate (int (i));
                out.rows[std::size_t (i * 2)] = c.bpm;
                out.rows[std::size_t (i * 2 + 1)] = c.score;
            }
        }
        const auto count = out.arrays[1].stored;
        const auto end = std::min (limit, count);
        auto* rows = out.rows.get() + out.arrays[0].values.size();
        for (; run.copied < end; ++run.copied)
        {
            const auto p = a.point (std::int64_t (run.copied));
            const auto raw = a.rawPoint (std::int64_t (run.copied));
            const double row[] { p.t, p.bpm, p.conf, p.hasBpm ? 1.0 : 0.0, raw.bpm, raw.conf };
            std::copy_n (row, 6, rows + std::size_t (run.copied * 6));
        }
        return run.copied == count;
    }
    return true;
}
}
void Session::stepSourceMeasurements() noexcept
{
    auto& run = *sourceMeasurements_; auto& workspace = *measurementWorkspace_;
    Notification event; event.jobId = measurementJob_;
    if (run.warningRead < run.warningCount)
    {
        event.kind = EventKind::Fact;
        (void) event.payload.fact.assign (run.warnings[run.warningRead++]); emit (event); return;
    }
    const auto warn = [&] (text::Fact fact)
    {
        detail::debugBound (run.warningCount < 8);
        run.warnings[run.warningCount++] = fact;
    };
    if (! run.initialWarnings)
    {
        run.initialWarnings = true;
        for (const auto& n : measurementResults_[0].numbers) if (n.name == "integratedLufs" && n.value)
        {
            const auto id = detail::SourceWarnings::quiet (*n.value);
            if (id != text::FactId::Value) warn (text::Fact::of (id, text::Arg::value (*n.value, text::Unit::Lufs, 1)));
        }
        const auto threshold = configured (detail::rules().engine.find ("observations").find ("dcOffset").find ("from"));
        double dc = 0;
        for (const auto& n : measurementResults_[1].numbers)
            if (n.name.starts_with ("dcOffset[") && n.value) dc = std::max (dc, std::abs (*n.value));
        if (dc >= threshold) warn (text::Fact::of (text::FactId::SourceDc, text::Arg::value (dc, text::Unit::None, 4)));
        return;
    }
    // Phase completion is a consequence of results, not a count of calls to step(). The first measurement — the
    // loudness and the true peak — has ended once the source's own runs begin: the needles are asked, then the devices
    // placed, a field that waits for one of the runs below "not measured yet" until it ends (placeAgain).
    if (run.cursor == run.firstCount && ! run.firstPublished)
    {
        if (! run.firstReady) { run.firstReady = true; requestNeedles(); return; }
        run.firstPublished = true;
        if (mandatoryReady())
        {
            // The first measurement ended: the planner places the devices for the project's target.
            state_ = State::Measured1; devicesPlaced_ = true; place (project_); replan(); refreshEqCurve(); ++revision_;
            event.kind = EventKind::Fact;
            (void) event.payload.fact.assign (text::Fact::of (text::FactId::Measurement1)); emit (event);
        }
        return;
    }
    if (run.cursor == 8 && run.tempoPublishedEarly) { ++run.cursor; run.tempoPublishedEarly = false; }
    if (run.cursor == run.order.size())
    {
        for (const auto& result : measurementResults_)
            if (result.analyzer != Analyzer::Excursions)
                detail::debugBound (result.status == MeasurementStatus::Ready || result.status == MeasurementStatus::Unavailable);
        run.finished = true;
        const auto job = measurementJob_;
        measurementProgress_ = { PhaseName::Analyzers, 1.0, config::Config::versions().all, 0, 0, 1, 1, std::nullopt };
        measurementProgress_.analyzers.emplace();
        if (mandatoryReady() && detail::Driver::measured2 (*this, job, source_.hash))
        {
            event.kind = EventKind::Fact;
            (void) event.payload.fact.assign (text::Fact::of (text::FactId::Measurement2));
            emit (event, measurementProgress_);
        }
        else measurementJob_ = 0;
        return;
    }
    const auto id = run.order[run.cursor]; const auto index = std::size_t (id);
    auto& result = measurementResults_[index];
    const Pcm pcm { nullptr, source_.channels, source_.frames, source_.sampleRate };
    const auto params = detail::MeasurementPlan::parametersFor (pcm);
    const auto publish = [&]
    {
        if (run.results[index])
        {
            const auto& out = *run.results[index];
            result.numbers = { out.numbers, out.numberCount }; result.arrays = { out.arrays, out.arrayCount };
            result.framesRead = run.frames; result.complete = true;
            for (const auto& a : result.arrays) { result.total += a.total; result.stored += a.stored; result.complete = result.complete && a.complete; }
        }
        event.kind = EventKind::Measurement;
        event.payload.measurement = { id, result.status, result.reason, result.key, source_.hash, ++revision_, result.framesRead, result.total, result.stored, result.complete };
        emit (event);
        constexpr text::Term analyzers[] { text::Term::AnalyzerLowEnd, text::Term::AnalyzerLowEnd150,
            text::Term::AnalyzerInfraLow, text::Term::AnalyzerForensics, text::Term::AnalyzerStereo,
            text::Term::AnalyzerCrest, text::Term::AnalyzerHum, text::Term::AnalyzerBursts, text::Term::AnalyzerTempo };
        auto status = text::Term::StatusReady;
        if (! result.complete && result.status == MeasurementStatus::Ready) status = text::Term::StatusCapacity;
        if (result.status == MeasurementStatus::Unavailable)
        {
            status = result.reason == MeasurementReason::Memory ? text::Term::StatusMemory
                : result.reason == MeasurementReason::TooShort ? text::Term::StatusShort
                : result.reason == MeasurementReason::NonFinite ? text::Term::StatusNonFinite
                : result.reason == MeasurementReason::NoSignal ? text::Term::StatusNoSignal : text::Term::StatusUnsupported;
        }
        event.kind = EventKind::Fact;
        (void) event.payload.fact.assign (text::Fact::of (text::FactId::AnalyzerStatus, text::Arg::term (analyzers[run.cursor]), text::Arg::term (status))); emit (event);
        workspace.release (id); ++run.cursor; run.stage = 0; run.frames = run.copied = 0;
        placeAgain (id);
        if (id == Analyzer::Tempo && run.tempoReturnCursor < run.order.size())
        { run.cursor = run.tempoReturnCursor; run.tempoReturnCursor = unsigned (run.order.size()); run.tempoPublishedEarly = true; }
    };
    ++run.work;
    const auto chunks = (source_.frames + 1023u) / 1024u;
    const auto done = std::uint64_t (run.cursor) * (chunks + 4) + run.frames / 1024;
    const auto total = run.order.size() * (chunks + 4);
    // THE BAR by [progress.analysis.weights]: the analyzers that ended, and the share of the current one read, of the sum
    // of all nine. The infra-low run is the low end's geometry at another crossover: it weighs lowEnd120.
    const auto weights = detail::rules().engine.find ("progress").find ("analysis").find ("weights");
    const auto weightOf = [&] (Analyzer a)
    {
        switch (a)
        {
            case Analyzer::LowEnd: case Analyzer::InfraLow: return configured (weights.find ("lowEnd120"));
            case Analyzer::LowEnd150: return configured (weights.find ("lowEnd150"));
            case Analyzer::Forensics: return configured (weights.find ("forensics"));
            case Analyzer::Stereo: return configured (weights.find ("stereo"));
            case Analyzer::Crest: return configured (weights.find ("crest"));
            case Analyzer::Hum: return configured (weights.find ("hum"));
            case Analyzer::StereoBursts: return configured (weights.find ("stereoBursts"));
            case Analyzer::Tempo: return configured (weights.find ("tempo"));
            case Analyzer::Loudness: case Analyzer::Clipping: case Analyzer::Programme: case Analyzer::Waveform:
            case Analyzer::Excursions: break;
        }
        detail::storageOverflow();
    };
    double weighed = 0, weightTotal = 0;
    for (const auto a : run.order)
    {
        const auto w = weightOf (a); weightTotal += w;
        const auto status = measurementResults_[std::size_t (a)].status;
        if (status != MeasurementStatus::Pending && status != MeasurementStatus::Cancelled) weighed += w;
    }
    if (source_.frames != 0 && measurementResults_[index].status == MeasurementStatus::Pending)
        weighed += weightOf (id) * double (run.frames) / double (source_.frames);
    measurementProgress_ = { PhaseName::Analyzers, weightTotal > 0 ? weighed / weightTotal : 0,
        config::Config::versions().all, 0, 0, std::uint32_t (std::min<std::uint64_t> (done, 4294967295u)),
        std::uint32_t (std::min<std::uint64_t> (total, 4294967295u)), std::nullopt };
    auto& current = measurementProgress_.analyzers.emplace();
    current.count = 1;
    current.items[0] = { id, source_.frames == 0 ? 0.0 : double (run.frames) / double (source_.frames) };
    if (run.stage == 0)
    {
        const auto plan = detail::MeasurementPlan::storageFor (pcm, params); const auto& price = plan.analyzers[index];
        if (! price.available) { result.status = MeasurementStatus::Unavailable; result.reason = MeasurementReason::Unsupported; publish(); return; }
        const auto retained = sizeof (Store) + price.rowValues * sizeof (double) + detail::MeasurementPlan::rowAllowance;
        const auto scratch = price.workspace + detail::MeasurementPlan::workspaceAllowance;
        Checked request; request.bytes = retained + scratch; request.largestBlockBytes = std::max (retained, scratch);
        if (demand (request).rejection != Rejection::None)
        { result.status = MeasurementStatus::Unavailable; result.reason = MeasurementReason::Memory; publish(); return; }
        run.results[index].reset (new Store); auto& out = *run.results[index]; out.capacity = price.rowValues;
        if (out.capacity != 0) out.rows.reset (new double[std::size_t (out.capacity)]);
        run.bytes += retained;
        if (! workspace.prepare (id, pcm, plan)) detail::storageOverflow();
        ++run.stage;
    }
    else if (run.stage == 1)
    {
        const auto n = std::min<std::uint64_t> (1024, source_.frames - run.frames);
        const float* planes[2] { samples_.get() + run.frames, source_.channels == 2 ? samples_.get() + source_.frames + run.frames : nullptr };
        process (workspace, id, planes, int (source_.channels), int (n));
        if (id == Analyzer::Stereo)
            for (std::size_t i = 0; i < std::size_t (n); ++i)
            {
                const float left = planes[0][i], right = source_.channels == 2 ? planes[1][i] : left;
                if (std::isfinite (left) && std::isfinite (right)) { run.stereo.add (left, right); ++run.stereoFinite; }
                else ++run.stereoNonFinite;
                if (! core::exactlyEqual (left, right)) run.dualMono = false;
            }
        run.frames += n;
        if (run.frames == source_.frames) ++run.stage;
    }
    else if (run.stage == 2)
    { if (finish (workspace, id, *run.results[index], run, source_, params)) ++run.stage; }
    else if (run.stage == 3)
    {
        if (copy (workspace, id, *run.results[index], run))
        {
            ++run.stage; run.copied = 0;
            if (id == Analyzer::Crest)
            {
                const auto level = run.crestCount == 0 ? analysis::BandCrest::kSilenceDb : 10 * core::det::log10 (run.crestSum / double (run.crestCount));
                const auto config = detail::rules().engine.find ("crest");
                const auto floor = std::max (configured (config.find ("floorDb")), level - configured (config.find ("belowMeanSquareDb")));
                run.crestFloor = core::det::pow10 (floor / 10);
                run.results[index]->number ("programmeMeanSquareDb", level, -1, run.crestCount == 0 ? MeasurementReason::NoSignal : MeasurementReason::None);
                run.results[index]->number ("activityFloorDb", floor);
            }
        }
    }
    else if (run.stage == 4 && id == Analyzer::Crest)
    {
        auto& out = *run.results[index]; const auto count = out.arrays[0].stored;
        const auto end = std::min (count, run.copied + 128);
        const auto share = core::det::pow10 (params.crest.bandShareFloorDb / 10);
        for (; run.copied < end; ++run.copied)
        {
            const auto i = run.copied; const auto power = out.rows[std::size_t (i * 10 + 9)];
            const auto os = out.rows[std::size_t (count * 15 + i)];
            for (unsigned b = 0; b < 5; ++b)
                out.rows[std::size_t (count * 10 + i * 5 + b)] = power >= run.crestFloor &&
                    (b == 4 || (os > 0 && out.rows[std::size_t (i * 10 + b * 2 + 1)] >= share * os)) ? 1 : 0;
        }
        if (run.copied == count) ++run.stage;
    }
    else
    {
        result.status = MeasurementStatus::Ready; result.reason = MeasurementReason::None;
        if (id == Analyzer::Crest && ! workspace.crest->valid())
        { result.status = MeasurementStatus::Unavailable; result.reason = workspace.crest->blockCount() == 0 ? MeasurementReason::TooShort : MeasurementReason::NonFinite; }
        if (isLowEnd (id) && ! lowEnd (workspace, id)->widthValid())
        {
            result.status = MeasurementStatus::Unavailable;
            result.reason = lowEnd (workspace, id)->widthReason() == analysis::LowEndReason::NoFiniteSamples
                ? MeasurementReason::NonFinite : MeasurementReason::NoSignal;
        }
        if (id == Analyzer::Hum)
        {
            bool valid = false;
            for (int c = 0; c < int (source_.channels); ++c) valid = valid || workspace.hum->report (c).valid;
            if (! valid)
            {
                result.status = MeasurementStatus::Unavailable;
                result.reason = workspace.hum->report (0).reason == analysis::HumReason::ShorterThanWindow ? MeasurementReason::TooShort : MeasurementReason::NoSignal;
            }
        }
        if (id == Analyzer::Tempo && (workspace.tempo->nonFiniteSamples() != 0 || ! workspace.tempo->determined()))
        {
            result.status = MeasurementStatus::Unavailable;
            result.reason = workspace.tempo->nonFiniteSamples() != 0 ? MeasurementReason::NonFinite
                : double (workspace.tempo->onsetFrames()) < workspace.tempo->odfSampleRate() * tempo::TempoDetector::kMinAnalysisSec
                    ? MeasurementReason::TooShort : MeasurementReason::NoSignal;
        }
        publish(); return;
    }
    event.kind = EventKind::Phase; event.payload.phase = measurementProgress_; emit (event);
}
} // namespace felitronics::session
