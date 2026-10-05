// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "BuildGuards.h"
#include "MeasurementWorkspace.h"

namespace felitronics::session::detail
{
namespace
{
template<class T, class Prepare> bool start (std::unique_ptr<T>& owner, Prepare&& prepare) noexcept
{
    if (owner) return true;
    owner.reset (new T);
    if (prepare (*owner)) return true;
    owner.reset(); return false;
}
}
bool MeasurementWorkspace::prepare (Analyzer analyzer, const Pcm& pcm, const MeasurementPlan& plan, bool spectrum) noexcept
{
    const auto index = std::size_t (analyzer);
    if (index >= kAnalyzers || ! plan.analyzers[index].available) return false;
    const auto signature = MeasurementPlan::key (0, plan.parameters, 0, {}, {});
    if (bound && (parameterKey != signature || frames != pcm.frames || rate != pcm.sampleRate || channels != pcm.channelCount)) return false;
    parameterKey = signature; frames = pcm.frames; rate = pcm.sampleRate; channels = pcm.channelCount; bound = true;
    const auto& p = plan.parameters;
    const double sampleRate = double (pcm.sampleRate);
    const int channelCount = int (pcm.channelCount);
    const bool prepared = [&]
    {
    switch (analyzer)
    {
        case Analyzer::Loudness: return start (loudness, [&] (auto& a) { return a.prepare (sampleRate, channelCount, p.programme.maxDurationSec); });
        case Analyzer::Clipping: return start (clipping, [&] (auto& a) { a.setParams ({ p.clipRuns }); return a.prepare (sampleRate, p.maxBlock, channelCount); });
        case Analyzer::Programme: return start (programme, [&] (auto& a) { a.setParams (p.programme); return a.prepare (sampleRate, p.maxBlock, channelCount); });
        case Analyzer::LowEnd: return start (lowEnd, [&] (auto& a) { a.setParams (p.lowEnd); return spectrum
            ? a.prepare (sampleRate, p.maxBlock, channelCount) : a.prepareWithoutSpectrum (sampleRate, p.maxBlock, channelCount); });
        case Analyzer::LowEnd150: return start (lowEnd150, [&] (auto& a) { a.setParams (p.lowEnd150); return spectrum
            ? a.prepare (sampleRate, p.maxBlock, channelCount) : a.prepareWithoutSpectrum (sampleRate, p.maxBlock, channelCount); });
        case Analyzer::InfraLow: return start (infraLow, [&] (auto& a) { a.setParams (p.infraLow); return spectrum
            ? a.prepare (sampleRate, p.maxBlock, channelCount) : a.prepareWithoutSpectrum (sampleRate, p.maxBlock, channelCount); });
        case Analyzer::Forensics: return start (forensics, [&] (auto& a) { a.setParams (p.forensics); return a.prepare (sampleRate, p.maxBlock, channelCount); });
        case Analyzer::Stereo: return start (stereo, [&] (auto& a) { return a.prepare (channelCount, pcm.frames, p.columns); });
        case Analyzer::Waveform: return start (waveform, [&] (auto& a) { return a.prepare (pcm.sampleRate, pcm.channelCount, pcm.frames); });
        case Analyzer::StereoBursts: return start (bursts, [&] (auto& a) { a.setParams (p.bursts); return a.prepare (sampleRate, channelCount); });
        case Analyzer::Crest: return start (crest, [&] (auto& a) { a.setParams (p.crest); return a.prepare (sampleRate, channelCount, static_cast<long long> (pcm.frames)); });
        case Analyzer::Hum: return start (hum, [&] (auto& a) { a.setParams (p.hum); return a.prepare (sampleRate, p.maxBlock, channelCount); });
        case Analyzer::Tempo: return start (tempo, [&] (auto& a) { a.setParams (p.tempo); return a.prepare (sampleRate, channelCount, pcm.frames); });
        case Analyzer::Excursions: return false;
    }
    return false;
    }();
    if (prepared) allocations[index] = plan.analyzers[index].workspace + MeasurementPlan::workspaceAllowance;
    return prepared;
}
std::uint64_t MeasurementWorkspace::bytes() const noexcept
{
    std::uint64_t total = sizeof (MeasurementWorkspace);
    for (auto bytes : allocations) total += bytes;
    return total;
}
void MeasurementWorkspace::release (Analyzer analyzer) noexcept
{
    if (std::size_t (analyzer) >= kAnalyzers) return;
    allocations[std::size_t (analyzer)] = 0;
    switch (analyzer)
    {
        case Analyzer::Loudness: loudness.reset(); break;
        case Analyzer::Clipping: clipping.reset(); break;
        case Analyzer::Programme: programme.reset(); break;
        case Analyzer::LowEnd: lowEnd.reset(); break;
        case Analyzer::InfraLow: infraLow.reset(); break;
        case Analyzer::LowEnd150: lowEnd150.reset(); break;
        case Analyzer::Forensics: forensics.reset(); break;
        case Analyzer::Stereo: stereo.reset(); break;
        case Analyzer::Waveform: waveform.reset(); break;
        case Analyzer::StereoBursts: bursts.reset(); break;
        case Analyzer::Crest: crest.reset(); break;
        case Analyzer::Hum: hum.reset(); break;
        case Analyzer::Tempo: tempo.reset(); break;
        case Analyzer::Excursions: break;
    }
}
}
