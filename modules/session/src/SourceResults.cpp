// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "BuildGuards.h"
#include "SourceMeasurements.h"
#include "MeasurementWorkspace.h"
#include "BuildContract.h"
#include "Rules.h"
#include <algorithm>
#include <cmath>

namespace felitronics::session::detail
{
namespace
{
double configured (toml::embedded::View v) noexcept
{
    if (const auto d = v.decimal()) return d->toDouble();
    if (const auto i = v.integer()) return double (*i);
    storageOverflow();
}
}
text::FactId SourceWarnings::quiet (double lufs) noexcept
{
    const auto q = rules().engine.find ("input").find ("quiet");
    return lufs < configured (q.find ("gainOnlyLufs")) ? text::FactId::SourceGainOnly
         : lufs < configured (q.find ("warningLufs")) ? text::FactId::SourceQuiet : text::FactId::Value;
}
bool SourceWarnings::wideBass (double side) noexcept
{
    return side >= configured (rules().engine.find ("observations").find ("wideBass").find ("sideFractionAtLeast"));
}
void MeasurementStore::number (std::string_view name, double value, int channel, MeasurementReason reason, unsigned nativeReason) noexcept
{
    debugBound (numberCount < kMeasurementNumbers && name.size() + 3 < kMeasurementNameBytes && channel < 10);
    auto* text = names[numberCount];
    std::copy (name.begin(), name.end(), text);
    auto n = name.size();
    if (channel >= 0) { text[n++] = '['; text[n++] = char ('0' + channel); text[n++] = ']'; }
    numbers[numberCount++] = { { text, n }, reason == MeasurementReason::None ? std::optional<double> (value) : std::nullopt, reason, nativeReason };
}
double* MeasurementStore::array (std::string_view name, std::uint32_t columns, std::uint64_t total,
                                  std::uint64_t stored, bool complete, MeasurementGrid grid) noexcept
{
    debugBound (arrayCount < kMeasurementArrays && columns != 0 && stored <= (capacity - used) / columns);
    auto* data = stored == 0 ? nullptr : rows.get() + std::size_t (used);
    arrays[arrayCount++] = { name, grid, columns, total, stored, complete, { data, std::size_t (stored * columns) } };
    used += stored * columns;
    return data;
}
namespace
{
MeasurementReason lowReason (analysis::LowEndReason reason) noexcept
{
    using R = analysis::LowEndReason;
    switch (reason)
    {
        case R::Ok: return MeasurementReason::None;
        case R::NotFinished: return MeasurementReason::Pending;
        case R::NoFiniteSamples: case R::NoUsableFrames: case R::Overflowed: return MeasurementReason::NonFinite;
        case R::NoEnergy: return MeasurementReason::NoSignal;
        case R::ShorterThanWindow: return MeasurementReason::TooShort;
    }
    storageOverflow();
}
}
void SourceResults::lowEnd (MeasurementStore& out, const analysis::LowEnd& a, double duty, double margin) noexcept
{
    out.number ("crossoverHz", double (a.crossoverHz())); out.number ("sampleRate", double (a.sampleRate())); out.number ("samplesProcessed", double (a.samplesProcessed())); out.number ("blockSamples", double (a.blockSamples()));
    out.number ("lowMidEnergy", double (a.lowMidEnergy())); out.number ("lowSideEnergy", double (a.lowSideEnergy())); out.number ("highMidEnergy", double (a.highMidEnergy())); out.number ("highSideEnergy", double (a.highSideEnergy()));
    out.number ("rawMidEnergy", double (a.rawMidEnergy())); out.number ("rawSideEnergy", double (a.rawSideEnergy())); out.number ("lowBandEnergy", double (a.lowBandEnergy())); out.number ("infraLowShare", double (a.infraLowShare()));
    out.number ("finiteSamples", double (a.finiteSamples())); out.number ("holeSamples", double (a.holeSamples())); out.number ("nonFiniteSamples", double (a.nonFiniteSamples())); out.number ("absentSamples", double (a.absentSamples()));
    out.number ("filterNonFiniteSamples", double (a.filterNonFiniteSamples())); out.number ("firstHoleSample", double (a.firstHoleSample())); out.number ("lastHoleSample", double (a.lastHoleSample()));
    out.number ("blockCount", double (a.blockCount())); out.number ("storedBlockCount", double (a.storedBlockCount())); out.number ("blocksComplete", double (a.blocksComplete())); out.number ("bandCount", double (a.bandCount()));
    out.number ("underResolvedBands", double (a.underResolvedBands())); out.number ("firstResolvedBand", double (a.firstResolvedBand())); out.number ("resolvedAboveHz", double (a.resolvedAboveHz())); out.number ("dutyFrames", double (a.dutyFrames()));
    out.number ("dutyThresholdDb", double (a.dutyThresholdDb())); out.number ("usedFrames", double (a.usedFrames())); out.number ("holedFrames", double (a.holedFrames())); out.number ("tailUncoveredSamples", double (a.tailUncoveredSamples()));
    out.number ("windowSamples", double (a.windowSamples())); out.number ("hopSamples", double (a.hopSamples())); out.number ("binHz", double (a.binHz())); out.number ("frameEnergy", double (a.frameEnergy())); out.number ("bandRangeShare", double (a.bandRangeShare()));
    out.number ("peakBand", double (a.peakBand())); out.number ("peakDensityBand", double (a.peakDensityBand())); out.number ("secondBand", double (a.secondBand())); out.number ("backgroundDensity", double (a.backgroundDensity()));
    out.number ("totalBandEnergy", double (a.totalBandEnergy())); out.number ("peakShare", double (a.peakShare())); out.number ("peakLowSideAmplitude", double (a.peakLowSideAmplitude()));
    const auto width = lowReason (a.widthReason()), note = lowReason (a.noteReason());
    out.number ("lowSideFraction", a.lowSideFraction(), -1, width, unsigned (a.widthReason()));
    out.number ("rawSideFraction", a.rawSideFraction(), -1, width, unsigned (a.widthReason()));
    out.number ("peakMidi", a.peakMidi(), -1, note, unsigned (a.noteReason()));
    out.number ("peakNoteHz", a.peakNoteHz(), -1, note, unsigned (a.noteReason()));
    out.number ("peakCentroidHz", a.peakCentroidHz(), -1, note, unsigned (a.noteReason()));
    out.number ("peakCentsOffset", a.peakCentsOffset(), -1, note, unsigned (a.noteReason()));
    // An uncertain or unresolved lowest occupied band never substitutes the loudest band.
    const auto occupied = a.lowestOccupiedBand (duty);
    const bool confident = a.noteValid() && occupied.band >= a.firstResolvedBand() && occupied.band >= 0
                         && occupied.marginWhenOnDb >= margin;
    const auto missing = note == MeasurementReason::None ? MeasurementReason::NoSignal : note;
    out.number ("lowestOccupiedMidi", occupied.midi, -1, confident ? MeasurementReason::None : missing, unsigned (a.noteReason()));
    out.number ("lowestOccupiedHz", occupied.centreHz, -1, confident ? MeasurementReason::None : missing, unsigned (a.noteReason()));
    out.number ("lowestOccupiedDuty", occupied.duty, -1, confident ? MeasurementReason::None : missing);
    out.number ("lowestOccupiedMarginDb", occupied.marginWhenOnDb, -1, confident ? MeasurementReason::None : missing);
    out.number ("hpfFloorRequired", confident ? 0 : 1);
    const MeasurementGrid grid { 0, std::uint64_t (a.blockSamples()), std::uint64_t (a.samplesProcessed()), std::uint32_t (a.sampleRate()) };
    (void) out.array ("blocks", 8, std::uint64_t (a.blockCount()), std::uint64_t (a.storedBlockCount()), a.blocksComplete(), grid);
    auto* bands = out.array ("bands", 16, std::uint64_t (a.bandCount()), std::uint64_t (a.bandCount()), true, { 0, 0, grid.framesRead, grid.sampleRate });
    for (int i = 0; i < a.bandCount(); ++i)
    {
        const auto b = a.band (i);
        const double row[] { double (b.midi), b.centreHz, b.widthHz, b.binsPerBand, b.midEnergy, b.sideEnergy,
            b.energy, b.density, b.centroidHz, b.centsOffset, b.sideFraction(), double (a.dutyCount (i)), a.duty (i),
            a.levelWhenOnDb (i), a.marginWhenOnDb (i), b.binsPerBand >= a.lobeBins() ? 1.0 : 0.0 };
        std::copy_n (row, 16, bands + std::size_t (i) * 16);
    }
    auto* histogram = out.array ("sideHistogram", 1, analysis::LowEnd::kHistogramBins, analysis::LowEnd::kHistogramBins, true, grid);
    for (int i = 0; i < analysis::LowEnd::kHistogramBins; ++i) histogram[i] = double (a.histogram (i));
}
int SourceResults::forensicsMetadata (MeasurementStore& out, int bitDepth) noexcept
{
    // The exact grid belongs to the PCM; unused container bits also depend on the current file metadata.
    int mostUnused = 0;
    for (std::size_t i = 0; i < out.numberCount; ++i)
    {
        const auto& exact = out.numbers[i];
        if (! exact.name.starts_with ("grid.minExactPcmBits[")) continue;
        for (std::size_t j = 0; j < out.numberCount; ++j)
        {
            auto& unused = out.numbers[j];
            if (! unused.name.starts_with ("grid.alwaysZeroLowBits[")
                || unused.name.substr (unused.name.size() - 3) != exact.name.substr (exact.name.size() - 3)) continue;
            unused.value.reset(); unused.reason = MeasurementReason::Unsupported;
            if (exact.value && bitDepth > 0)
            {
                const auto bits = *exact.value;
                unused.value = bits > 0 && bitDepth > bits ? bitDepth - bits : 0;
                unused.reason = MeasurementReason::None;
                mostUnused = std::max (mostUnused, int (*unused.value));
            }
        }
    }
    return mostUnused;
}
void SourceResults::forensics (MeasurementStore& out, const analysis::SourceForensics& a, int bitDepth) noexcept
{
    out.number ("sampleRate", double (a.sampleRate())); out.number ("windowSamples", double (a.windowSamples())); out.number ("hopSamples", double (a.hopSamples())); out.number ("binHz", double (a.binHz())); out.number ("cellHz", double (a.cellHz()));
    out.number ("bins", double (a.bins())); out.number ("cellCount", double (a.cellCount())); out.number ("binsPerCell", double (a.binsPerCell())); out.number ("searchFromHz", double (a.searchFromHz())); out.number ("searchToHz", double (a.searchToHz()));
    out.number ("plateauSpanCells", double (a.plateauSpanCells())); out.number ("floorSpanCells", double (a.floorSpanCells())); out.number ("exemptCells", double (a.exemptCells())); out.number ("distinctLimit", double (a.distinctLimit()));
    out.number ("samplesProcessed", double (a.samplesProcessed())); out.number ("tailUncoveredSamples", double (a.tailUncoveredSamples()));
    for (int c = 0; c < a.channels(); ++c)
    {
        const auto w = a.wall (c); const auto g = a.sampleGrid (c);
        out.number ("wall." "valid", double (w.valid), c); out.number ("wall." "reason", double (w.reason), c); out.number ("wall." "sharp", double (w.sharp), c); out.number ("wall." "nearNyquist", double (w.nearNyquist), c); out.number ("wall." "cutoffFractionOfNyquist", double (w.cutoffFractionOfNyquist), c);
        out.number ("wall." "steepestHz", double (w.steepestHz), c); out.number ("wall." "transitionEndHz", double (w.transitionEndHz), c); out.number ("wall." "transitionHz", double (w.transitionHz), c); out.number ("wall." "transitionClipped", double (w.transitionClipped), c); out.number ("wall." "truncatedAtNyquist", double (w.truncatedAtNyquist), c);
        out.number ("wall." "plateauPower", double (w.plateauPower), c); out.number ("wall." "floorLocalPower", double (w.floorLocalPower), c); out.number ("wall." "maxAbovePower", double (w.maxAbovePower), c); out.number ("wall." "sufMaxPower", double (w.sufMaxPower), c); out.number ("wall." "exemptedCells", double (w.exemptedCells), c);
        out.number ("wall." "dropDb", double (w.dropDb), c); out.number ("wall." "strictDropDb", double (w.strictDropDb), c); out.number ("wall." "localDropDb", double (w.localDropDb), c); out.number ("wall." "recoveryDb", double (w.recoveryDb), c); out.number ("wall." "plateauSpreadDb", double (w.plateauSpreadDb), c);
        out.number ("wall." "steepnessDbPerOctave", double (w.steepnessDbPerOctave), c); out.number ("wall." "secondValid", double (w.secondValid), c); out.number ("wall." "secondSharp", double (w.secondSharp), c); out.number ("wall." "secondTransitionClipped", double (w.secondTransitionClipped), c);
        out.number ("wall." "secondTruncatedAtNyquist", double (w.secondTruncatedAtNyquist), c); out.number ("wall." "secondReason", double (w.secondReason), c); out.number ("wall." "secondCutoffHz", double (w.secondCutoffHz), c); out.number ("wall." "secondDropDb", double (w.secondDropDb), c);
        out.number ("wall." "secondTransitionHz", double (w.secondTransitionHz), c); out.number ("wall." "emptyAboveValid", double (w.emptyAboveValid), c); out.number ("wall." "emptyAboveReason", double (w.emptyAboveReason), c); out.number ("wall." "emptyAboveHz", double (w.emptyAboveHz), c);
        out.number ("wall." "emptyAboveFractionOfNyquist", double (w.emptyAboveFractionOfNyquist), c); out.number ("wall." "emptyThresholdPower", double (w.emptyThresholdPower), c); out.number ("wall." "peakCellPower", double (w.peakCellPower), c);
        out.number ("wall." "framesUsed", double (w.framesUsed), c); out.number ("wall." "framesHoled", double (w.framesHoled), c);
        out.number ("wall.cutoffHz", w.cutoffHz, c, w.valid ? MeasurementReason::None
            : w.reason == analysis::ForensicsReason::ShorterThanWindow ? MeasurementReason::TooShort : MeasurementReason::NoSignal, unsigned (w.reason));
        out.number ("grid." "valid", double (g.valid), c); out.number ("grid." "reason", double (g.reason), c); out.number ("grid." "gridExponent", double (g.gridExponent), c); out.number ("grid." "pcmCompatible", double (g.pcmCompatible), c); out.number ("grid." "outsidePcmRange", double (g.outsidePcmRange), c);
        out.number ("grid." "absPeak", double (g.absPeak), c); out.number ("grid." "sampleMin", double (g.sampleMin), c); out.number ("grid." "sampleMax", double (g.sampleMax), c); out.number ("grid." "nonZeroSamples", double (g.nonZeroSamples), c); out.number ("grid." "zeroSamples", double (g.zeroSamples), c);
        out.number ("grid." "nonFiniteSamples", double (g.nonFiniteSamples), c); out.number ("grid." "absentSamples", double (g.absentSamples), c); out.number ("grid." "offGridSamples", double (g.offGridSamples), c); out.number ("grid." "firstOffGridSample", double (g.firstOffGridSample), c);
        out.number ("grid." "firstMaxGridSample", double (g.firstMaxGridSample), c); out.number ("grid." "robustGridExponent", double (g.robustGridExponent), c); out.number ("grid." "robustPcmBits", double (g.robustPcmBits), c); out.number ("grid." "distinctValues", double (g.distinctValues), c); out.number ("grid." "distinctComplete", double (g.distinctComplete), c);
        out.number ("grid.minExactPcmBits", g.minExactPcmBits, c, g.pcmCompatible ? MeasurementReason::None : MeasurementReason::NoSignal, unsigned (g.reason));
        out.number ("grid.alwaysZeroLowBits", g.alwaysZeroLowBits (bitDepth), c,
            g.pcmCompatible && bitDepth > 0 ? MeasurementReason::None : MeasurementReason::Unsupported, unsigned (g.reason));
    }
    const MeasurementGrid grid { 0, 0, std::uint64_t (a.samplesProcessed()), std::uint32_t (a.sampleRate()) };
    (void) out.array ("meanPower", std::uint32_t (a.channels()), std::uint64_t (a.bins()), std::uint64_t (a.bins()), true, grid);
    auto* histogram = out.array ("gridExponentHistogram", std::uint32_t (a.channels()), a.gridExponentBuckets(), a.gridExponentBuckets(), true, grid);
    for (int i = 0; i < a.gridExponentBuckets(); ++i)
        for (int c = 0; c < a.channels(); ++c) histogram[i * a.channels() + c] = double (a.gridExponentHistogram (c)[i]);
}
void SourceResults::hum (MeasurementStore& out, const analysis::HumDetector& a) noexcept
{
    out.number ("sampleRate", a.sampleRate()); out.number ("windowSamples", double (a.windowSamples()));
    out.number ("hopSamples", double (a.hopSamples())); out.number ("binHz", a.binHz());
    for (int c = 0; c < a.channels(); ++c)
    {
        const auto r = a.report (c);
        out.number ("valid", double (r.valid), c); out.number ("reason", double (r.reason), c); out.number ("mains", double (r.mains), c); out.number ("fundamentalObserved", double (r.fundamentalObserved), c); out.number ("fundamentalDerived", double (r.fundamentalDerived), c);
        out.number ("baseHarmonic", double (r.baseHarmonic), c); out.number ("harmonicsObserved", double (r.harmonicsObserved), c); out.number ("frames", double (r.frames), c); out.number ("finiteFrames", double (r.finiteFrames), c); out.number ("holedFrames", double (r.holedFrames), c);
        out.number ("quietFrames", double (r.quietFrames), c); out.number ("silentFrames", double (r.silentFrames), c); out.number ("quietStretches", double (r.quietStretches), c); out.number ("eligibleStretches", double (r.eligibleStretches), c);
        out.number ("storedStretches", double (r.storedStretches), c); out.number ("stretchesComplete", double (r.stretchesComplete), c); out.number ("totalSamples", double (r.totalSamples), c); out.number ("tailUncoveredSamples", double (r.tailUncoveredSamples), c);
        const auto reason = r.valid ? MeasurementReason::None
            : r.reason == analysis::HumReason::ShorterThanWindow ? MeasurementReason::TooShort : MeasurementReason::NoSignal;
        out.number ("fundamentalHz", r.fundamentalHz, c, reason, unsigned (r.reason));
        out.number ("prominenceDb", r.line.prominenceDb, c, reason, unsigned (r.reason));
        out.number ("tonePower", r.line.tonePower, c, reason, unsigned (r.reason));
    }
    const MeasurementGrid grid { 0, 0, std::uint64_t (a.samplesProcessed()), std::uint32_t (a.sampleRate()) };
    auto* candidates = out.array ("candidates", 36, std::uint64_t (a.channels() * 2), std::uint64_t (a.channels() * 2), true, grid);
    for (int c = 0; c < a.channels(); ++c) for (int k = 0; k < 2; ++k)
    {
        const auto v = a.candidate (c, k);
        const double row[] { double (c), v.nominalHz, double (v.baseFound), double (v.baseHarmonic), v.fundamentalHz,
            double (v.fundamentalObserved), double (v.harmonicsObserved), double (v.lowestHarmonicObserved),
            double (v.combWithoutBase), v.combFundamentalHz, double (v.stretchObservations), double (v.stretchOffTolerance),
            double (v.frameObservations), v.stretchSpreadHz, v.frameSpreadHz, v.maxIntraStretchSpreadHz, double (v.stationary), double (v.passed),
            double (v.base.found), double (v.base.prominent), double (v.base.accepted), double (v.base.bin), v.base.hz,
            v.base.tonePower, v.base.peakBinPower, v.base.floorPower, v.base.prominenceDb,
            double (v.windowPeak.found), double (v.windowPeak.prominent), double (v.windowPeak.accepted), double (v.windowPeak.bin), v.windowPeak.hz,
            v.windowPeak.tonePower, v.windowPeak.peakBinPower, v.windowPeak.floorPower, v.windowPeak.prominenceDb };
        std::copy_n (row, 36, candidates + std::size_t (c * 2 + k) * 36);
    }
    const auto harmonics = std::uint64_t (a.channels() * 2 * a.committedParams().maxHarmonic);
    auto* rows = out.array ("harmonics", 13, harmonics, harmonics, true, grid);
    for (int c = 0; c < a.channels(); ++c) for (int k = 0; k < 2; ++k) for (int h = 1; h <= a.committedParams().maxHarmonic; ++h)
    {
        const auto v = a.harmonic (c, k, h);
        const double row[] { double (c), double (k), double (v.index), double (v.inBand), double (v.peak.found),
            double (v.peak.prominent), double (v.peak.accepted), double (v.peak.bin), v.peak.hz, v.peak.tonePower,
            v.peak.peakBinPower, v.peak.floorPower, v.peak.prominenceDb };
        std::copy_n (row, 13, rows); rows += 13;
    }
    std::uint64_t total = 0, stored = 0;
    for (int c = 0; c < a.channels(); ++c) { total += std::uint64_t (a.stretchCount (c)); stored += std::uint64_t (a.storedStretchCount (c)); }
    (void) out.array ("stretches", 5, total, stored, total == stored, grid);
}
void SourceResults::bursts (MeasurementStore& out, const analysis::StereoBandBursts& a) noexcept
{
    out.number ("sampleRate", a.sampleRate()); out.number ("sideAbsent", a.sideAbsent() ? 1 : 0);
    out.number ("domeShare", a.domeShare()); out.number ("domeHz", a.domeHz());
    for (int axis = 0; axis < 2; ++axis)
    {
        const auto& b = a.axis (axis);
        out.number ("hopSamples", double (b.hopSamples()), axis); out.number ("baselineHops", double (b.baselineHops()), axis); out.number ("bandLowHz", double (b.bandLowHz()), axis); out.number ("bandHighHz", double (b.bandHighHz()), axis); out.number ("hopCount", double (b.hopCount()), axis);
        out.number ("eligibleHops", double (b.eligibleHops()), axis); out.number ("zeroBaselineHops", double (b.zeroBaselineHops()), axis); out.number ("burstHops", double (b.burstHops()), axis); out.number ("damagedHops", double (b.damagedHops()), axis); out.number ("tailPartialSamples", double (b.tailPartialSamples()), axis);
        out.number ("tailPartialEnergy", double (b.tailPartialEnergy()), axis); out.number ("eventCount", double (b.eventCount()), axis); out.number ("storedEventCount", double (b.storedEventCount()), axis); out.number ("eventsComplete", double (b.eventsComplete()), axis);
        out.number ("eventsInvalidReason", double (b.eventsInvalidReason()), axis); out.number ("programmeEnergyInvalidReason", double (b.programmeEnergyInvalidReason()), axis); out.number ("overflowSamples", double (b.overflowSamples()), axis); out.number ("firstNonFiniteAt", double (b.firstNonFiniteAt()), axis);
        out.number ("onsetCount", double (b.onsetCount()), axis); out.number ("onsetsPerSecond", double (b.onsetsPerSecond()), axis); out.number ("intervalCount", double (b.intervalCount()), axis); out.number ("intervalOverflow", double (b.intervalOverflow()), axis);
        out.number ("modalIntervalHops", double (b.modalIntervalHops()), axis); out.number ("modalIntervalMass", double (b.modalIntervalMass()), axis);
        out.number ("bandEnergy", b.bandEnergy (0), axis, a.sideAbsent() && axis == 1 ? MeasurementReason::Unsupported : MeasurementReason::None);
        out.number ("nonFiniteSamples", double (b.nonFiniteSamples (0)), axis);
        out.number ("eventCountLowerBound", double (b.eventCount()), axis);
        const MeasurementGrid grid { 0, std::uint64_t (b.hopSamples()), std::uint64_t (b.samplesProcessed()), std::uint32_t (a.sampleRate()) };
        (void) out.array (axis == 0 ? "midEvents" : "sideEvents", 16, std::uint64_t (b.eventCount()), std::uint64_t (b.storedEventCount()), b.eventsComplete(), grid);
        auto* intervals = out.array (axis == 0 ? "midIntervals" : "sideIntervals", 2, analysis::BandBursts::kIoiBins, analysis::BandBursts::kIoiBins, true, grid);
        for (int i = 1; i <= analysis::BandBursts::kIoiBins; ++i)
        { intervals[2 * (i - 1)] = double (b.intervalBin (i)); intervals[2 * (i - 1) + 1] = double (b.lagBin (i)); }
    }
}
void SourceResults::tempo (MeasurementStore& out, const tempo::TempoDetector& a, std::uint32_t rate,
                           std::uint64_t frames) noexcept
{
    const auto reason = a.nonFiniteSamples() != 0 ? MeasurementReason::NonFinite
        : double (a.onsetFrames()) < a.odfSampleRate() * tempo::TempoDetector::kMinAnalysisSec
            ? MeasurementReason::TooShort : MeasurementReason::NoSignal;
    const auto ready = a.headline().determined && a.nonFiniteSamples() == 0;
    const auto missing = ready ? MeasurementReason::None : reason;
    const auto headline = [&] (std::string_view prefix, const tempo::TempoHeadline& h)
    {
        // Names are fixed facts; missing readings carry their cause instead of a numeric zero.
        if (prefix == "headline")
        {
            out.number ("headlineBpm", h.bpm, -1, missing);
            out.number ("headlineConfidence", h.confidence, -1, missing);
            out.number ("headlineLabel", double (h.label), -1, missing);
            out.number ("headlineBeatPeriodSec", h.beatPeriodSec, -1, missing);
            out.number ("headlineBeatOffsetSec", h.beatOffsetSec, -1, missing);
            out.number ("headlineAlternativeCount", h.altCount, -1, missing);
            if (ready) for (int i = 0; i < h.altCount; ++i) out.number ("headlineAlternative", h.alts[i], i);
        }
        else
        {
            out.number ("wholeTrackBpm", h.bpm, -1, missing);
            out.number ("wholeTrackConfidence", h.confidence, -1, missing);
            out.number ("wholeTrackLabel", double (h.label), -1, missing);
            out.number ("wholeTrackBeatPeriodSec", h.beatPeriodSec, -1, missing);
            out.number ("wholeTrackBeatOffsetSec", h.beatOffsetSec, -1, missing);
            out.number ("wholeTrackAlternativeCount", h.altCount, -1, missing);
            if (ready) for (int i = 0; i < h.altCount; ++i) out.number ("wholeTrackAlternative", h.alts[i], i);
        }
    };
    headline ("headline", a.headline()); headline ("wholeTrack", a.wholeTrack());
    out.number ("varies", a.varies() ? 1 : 0, -1, missing);
    out.number ("hasRange", a.hasRange() ? 1 : 0, -1, missing);
    out.number ("rangeLowBpm", a.rangeLow(), -1, ! ready ? missing : a.hasRange() ? MeasurementReason::None : MeasurementReason::TooShort);
    out.number ("rangeHighBpm", a.rangeHigh(), -1, ! ready ? missing : a.hasRange() ? MeasurementReason::None : MeasurementReason::TooShort);
    out.number ("anchorBpm", a.anchorBpm(), -1, missing);
    out.number ("anchorConfidence", a.anchorConfidence(), -1, missing);
    out.number ("anchorLag", a.anchorLag(), -1, missing);
    out.number ("nonFiniteSamples", double (a.nonFiniteSamples()));
    out.number ("onsetFrames", double (a.onsetFrames()));
    out.number ("odfSampleRate", a.odfSampleRate());
    out.number ("candidateCount", ready ? a.candidateCount() : 0);
    out.number ("curvePoints", ready ? double (a.pointCount()) : 0);
    const auto candidates = ready ? std::uint64_t (a.candidateCount()) : 0u;
    const auto points = ready ? std::uint64_t (a.pointCount()) : 0u;
    const MeasurementGrid grid { 0, std::uint64_t (a.hopFrames()) * tempo::TempoDetector::kHop, frames, rate };
    (void) out.array ("candidates", 2, candidates, candidates, true, grid);
    (void) out.array ("curve", 6, points, points, true, grid);
}
} // namespace felitronics::session::detail
