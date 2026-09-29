// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "BuildGuards.h"
#include "BuildContract.h"
#include "QueryState.h"
#include <felitronics/session/Session.h>
#include <felitronics/session/Config.h>
#include <algorithm>

namespace felitronics::session
{
void Session::stepWaveform() noexcept
{
    auto& run = *waveform_;
    auto& result = measurementResults_[std::size_t (Analyzer::Waveform)];
    if (! run.prepared)
    {
        const auto storage = analysis::WaveformIndex::storageFor (source_.sampleRate, source_.channels, source_.frames);
        Checked request; request.bytes = storage.ok ? storage.bytes() + 256u : 0;
        request.largestBlockBytes = request.bytes;
        const auto checked = demand (request);
        if (! storage.ok || checked.rejection != Rejection::None)
        {
            run.finished = true;
            result.status = MeasurementStatus::Unavailable;
            result.reason = storage.ok ? MeasurementReason::Memory : MeasurementReason::Unsupported;
        }
        else
        {
            if (! run.index.prepare (source_.sampleRate, source_.channels, source_.frames)) detail::storageOverflow();
            run.bytes = request.bytes;
        }
        run.prepared = true;
    }
    else
    {
        const auto at = run.index.framesSeen();
        const auto count = std::uint32_t (std::min<std::uint64_t> (1024, source_.frames - at));
        const float* planes[] { samples_.get() + at, source_.channels == 2 ? samples_.get() + source_.frames + at : nullptr };
        analysis::WaveformBuilder builder (run.index);
        if (! builder.process (planes, source_.channels, count)) detail::storageOverflow();
        result.framesRead = run.index.framesSeen();
        if (run.index.complete())
        {
            run.finished = result.complete = true;
            result.status = MeasurementStatus::Ready; result.reason = MeasurementReason::None;
            run.numbers[0] = { "leafFrames", double (run.index.leafFrames()), MeasurementReason::None, 0 };
            run.numbers[1] = { "envelopeBoxFrames", double (run.index.boxFrames()), MeasurementReason::None, 0 };
            run.numbers[2] = { "rightPresent", source_.channels == 2 ? 1.0 : 0.0, MeasurementReason::None, 0 };
            run.numbers[3] = { "sidePresent", source_.channels == 2 ? 1.0 : 0.0, MeasurementReason::None, 0 };
            result.numbers = run.numbers;
        }
    }
    const auto frames = run.index.framesSeen();
    measurementProgress_ = { PhaseName::Stream, 0.05 * double (frames) / double (source_.frames),
        config::Config::versions().all, 0, 0, std::uint32_t (std::min<std::uint64_t> (frames / 1024u + 1u, 4294967295u)),
        std::uint32_t (std::min<std::uint64_t> ((source_.frames + 1023u) / 1024u + 1u, 4294967295u)) };
    Notification event; event.jobId = measurementJob_;
    if (run.finished)
    {
        event.kind = EventKind::Measurement;
        event.payload.measurement = { result.analyzer, result.status, result.reason, result.key, source_.hash,
            ++revision_, result.framesRead, result.total, result.stored, result.complete };
        emit (event);
        event.kind = EventKind::Fact;
        const auto status = result.status == MeasurementStatus::Ready ? text::Term::StatusReady
            : result.reason == MeasurementReason::Memory ? text::Term::StatusMemory : text::Term::StatusUnsupported;
        (void) event.payload.fact.assign (text::Fact::of (text::FactId::AnalyzerStatus,
            text::Arg::term (text::Term::AnalyzerWaveform), text::Arg::term (status)));
        emit (event);
    }
    event.kind = EventKind::Phase; event.payload.phase = measurementProgress_; emit (event);
}
} // namespace felitronics::session
