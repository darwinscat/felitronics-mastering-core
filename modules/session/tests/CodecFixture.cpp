// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// Every scalar mapping, optional alternative, record and array crosses the real encoder here.
// The offline JS consumer validates its output against the generated declarations.
#include "JsonCodec.h"
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>

using namespace felitronics::session;
namespace
{
struct Fill
{
    unsigned mode;
    template <class T> void value (T& x)
    {
        if constexpr (std::is_same_v<T, bool>) x = mode % 2 != 0;
        else if constexpr (std::is_same_v<T, double>)
        {
            const double values[] = { 1.5, -0.0, std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 0.125 };
            x = values[mode];
        }
        else if constexpr (std::is_same_v<T, std::string_view>) x = "name \xF0\x9F\x8E\xB5";
        else if constexpr (std::is_enum_v<T>) x = T (mode % (detail::enumLast<T>() + 1));
        else if constexpr (std::is_integral_v<T>) x = mode % 2 ? std::numeric_limits<T>::max() : std::numeric_limits<T>::min();
        else detail::describe (*this, x);
    }
    template <class T> void value (std::optional<T>& x)
    {
        if (mode == 0) x.reset();
        else { T v {}; value (v); x = v; }
    }
    template <class T> void value (std::span<const T>&) {}
    void value (text::Fact& f)
    {
        double number = 0.0; value (number);
        f = text::Fact::of (text::FactId::PlanWaiting, text::Arg::value (number, text::Unit::Percent, 1, text::Sign::Always, text::Bound::AtMost),
            text::Arg::term (text::Term::DeviceGlue), text::Arg::midi (40), text::Arg::count (-5), text::Arg::text (""));
    }
    template <class T, std::size_t N> void value (BoundedList<T, N>& x)
    {
        x = {};
        if (mode != 0) { x.count = 1; value (x.items[0]); }
    }
    template <class T, class D> void optionalField (std::string_view name, T& x, const D&) { field (name, x); }
    template <class T> void field (std::string_view, T& x) { value (x); }
};
}
int main()
{
    for (unsigned mode = 0; mode < 6; ++mode)
    {
        Fill fill { mode };
        SnapshotView view;
        Kept kept[1]; LandingPass pass[1]; ReadingPoint points[1]; ReadingRun runs[1]; MachineDifference differences[1]; EqPoint curve[1];
        LandingTraceBucket traceRow { 0.0, 1.5, 0.75, 4, 0 };
        double crestRows[10] { 0.5, 0.125, 0.4, 0.08, 0.3, 0.045, 0.2, 0.02, 0.6, 0.18 };
        double crestMask[5] { 1.0, 1.0, 0.0, 1.0, 1.0 };
        MasterSection section { 0, 48000, -15.0, -14.0, 0.0, true };
        MasterWaveformBucket wave { 0, 48000, 0, -0.5, 0.5, 0.2, 48000 };
        fill.value (view); fill.value (kept[0]); fill.value (points[0]); fill.value (runs[0]); fill.value (differences[0]); fill.value (curve[0]);
        view.pendingMasterBytes = 0.0; // byte counts are finite and nonnegative, even in hostile scalar fixtures
        fill.value (pass[0]);
        if (kept[0].landing)
        {
            kept[0].landing->passes = 1;
            kept[0].landing->log = pass;
            kept[0].landing->deliverable = false; kept[0].landing->peaksAboveCeiling = false;
            LandingTrace trace;
            trace.toFrame = 1; trace.sampleRateHz = 48000; trace.columns = 1;
            trace.samples = 4; trace.complete = trace.valid = true; trace.rows = { &traceRow, 1 };
            kept[0].landing->limiterTrace = trace;
            kept[0].landing->peakClipTrace = trace;
        }
        if (mode == 1 && kept[0].report)
        {
            auto& report = *kept[0].report;
            report.status = MeasurementStatus::Ready; report.reason = MeasurementReason::None;
            report.targetLufs = -14.0; report.ceilingDbTp = -1.0;
            report.achievedLufs = -14.0; report.truePeakDbTp = -2.0;
            report.lraLu = 2.0; report.lraReason = MeasurementReason::None;
            report.plrDb = 8.0; report.plrReason = MeasurementReason::None;
            report.gainFromSourceDb = 1.0; report.missLu = 0.0;
            report.peakSafe = true; report.deliverable = false; report.peaksAboveCeiling = false;
            report.targetMet = kept[0].landing->status == LandingStatus::Solved;
            report.checkPasses = 0;
            report.crest.status = MeasurementStatus::Ready;
            report.crest.reason = MeasurementReason::None;
            report.crest.version = 1;
            report.crest.sampleRateHz = 48000; report.crest.hopFrames = 480;
            report.crest.blockHops = 4; report.crest.frames = 48000;
            report.crest.blocks = 1; report.crest.complete = true;
            report.crest.sourceRateCheck = false;
            report.crest.rows = crestRows; report.crest.sourceMask = crestMask;
            report.firstHint = MasterHint { LandingReason::ExcessSubBass, 35.0, true };
            report.secondHint = MasterHint { LandingReason::TruePeak, -2.0, false };
            MasterCost cost;
            cost.sourceRateHz = cost.masterRateHz = 48000;
            cost.sourceFrames = cost.masterFrames = 48000;
            cost.crestFullDb = { MeasurementReason::None, 1.5, 1 };
            cost.shapeP95Lu = { MeasurementReason::None, 0.0, 1 };
            cost.sections = { &section, 1 }; cost.waveform = { &wave, 1 };
            report.cost = cost;
        }
        else kept[0].report.reset();
        view.measurementStorage = {};
        view.needlesBytes = view.needlesLargestBlockBytes = 0;
        MeasurementValue number { "peak", 1.0, MeasurementReason::None, 0 };
        double values[] { 0.25, 0.5 };
        MeasurementArray array { "peaks", { 0, 1, 2, 48000 }, 1, 2, 2, true, values };
        MeasurementResult result;
        result.status = MeasurementStatus::Ready; result.reason = MeasurementReason::None;
        result.numbers = { &number, 1 }; result.arrays = { &array, 1 };
        view.measurements = { &result, 1 };
        view.sourceBytes = mode ? 9007199254740991.0 : 0.0;
        if (mode) { view.masters = kept; view.momentary = points; view.shortTerm = points; view.runs = runs; view.machineDifferences = differences; view.eqCurve = curve; }
        const auto need = Codec::encodedBytes (view);
        if (need.status != CodecStatus::Ok) return 1;
        std::string json (std::size_t (need.bytes), '\0');
        if (Codec::encode (view, json) != CodecStatus::Ok) return 2;
        std::puts (json.c_str());
    }
    MeasurementChange change;
    detail::Writer counter; counter.value (change);
    std::string json (std::size_t (counter.size), '\0');
    detail::Writer writer; writer.output = json.data(); writer.value (change);
    std::puts (json.c_str());
    QueryView query;
    double queryValues[] { 2, 0.5, 0 };
    query.values = queryValues;
    query.stride = 3; query.stored = query.total = 1;
    detail::Writer queryCounter; queryCounter.value (query);
    std::string queryJson (std::size_t (queryCounter.size), '\0');
    detail::Writer queryWriter; queryWriter.output = queryJson.data(); queryWriter.value (query);
    std::puts (queryJson.c_str());
    MeasuredSource measured;
    Fill fill { 1 }; fill.value (measured);
    detail::Writer measuredCounter; measuredCounter.value (measured);
    std::string measuredJson (std::size_t (measuredCounter.size), '\0');
    detail::Writer measuredWriter; measuredWriter.output = measuredJson.data(); measuredWriter.value (measured);
    std::puts (measuredJson.c_str());
    return 0;
}
