// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once
#include "../../modules/session/src/JsonCodec.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Wire.h>
#include <bit>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>
using namespace felitronics::session;
namespace
{
void print (const std::string& json, const std::vector<double>& rows)
{
    std::puts (json.c_str());
    const auto* bytes = reinterpret_cast<const unsigned char*> (rows.data());
    for (std::size_t i = 0; i < rows.size() * sizeof (double); ++i) std::printf ("%02x", unsigned (bytes[i]));
    std::putchar ('\n');
}
}
int sessionWireFixture (bool frozen = false)
{
    // A size walk must not wrap at wasm32's address limit before the transfer gate can refuse it.
    detail::Writer count;
    count.size = 4294967295ull;
    count.put ('x');
    if (count.size != 4294967296ull) return 3;
    const ReadingPoint one[] { { 1, 2 } };
    count.binaryRows = true; count.rowSize = 4294967295ull;
    count.value (std::span<const ReadingPoint> (one));
    if (count.rowSize != 4294967297ull) return 4;
    SnapshotView v;
    const ReadingPoint points[] { { 9007199254740991ull, -std::numeric_limits<double>::infinity() },
                                 { 4, std::numeric_limits<double>::quiet_NaN() } };
    const ReadingRun runs[] { { 7, 2, 0.5 } };
    const MachineDifference difference[] { { Device::Hpf, 1, 24, 32 } };
    v.machineDifferences = difference;
    const EqPoint curve[] { { 20, -3.5 }, { 1000, 0.25 } };
    v.eqCurve = curve; v.handFieldCount = 2;
    v.momentary = points; v.shortTerm = { points, 1 }; v.runs = runs;
    v.sourceBytes = 9007199254740991.0; v.integratedLufs = -std::numeric_limits<double>::infinity();
    auto need = Wire::snapshotBytes (v);
    std::string json (need.jsonBytes, ' '); std::vector<double> binary (need.rowBytes / 8);
    if (Wire::snapshot (v, json, binary) != CodecStatus::Ok) return 1;
    print (json, binary);
    Notification e[8];
    for (unsigned i = 0; i < 8; ++i) { e[i].seq = 9007199254741000ull + i; e[i].jobId = 42; }
    for (unsigned i = 0; i < 3; ++i) { e[i].kind = EventKind::Phase; e[i].payload.phase.name = PhaseName (5 + i); }
    e[3].kind = EventKind::Fact;
    (void) e[3].payload.fact.assign (text::Fact::of (text::FactId::DefaultsConverted, text::Arg::text ("line\nvoice"),
        text::Arg::count (std::numeric_limits<std::int64_t>::min()), text::Arg::value (-14.0, text::Unit::Lufs, 1)));
    e[4].kind = EventKind::Reading;
    e[4].payload.reading.momentary[0] = points[0]; e[4].payload.reading.momentaryCount = 1;
    e[4].payload.reading.shortTerm[0] = points[1]; e[4].payload.reading.shortTermCount = 1;
    e[4].payload.reading.runs[0] = runs[0]; e[4].payload.reading.runCount = 1;
    e[5].kind = EventKind::Done; e[5].payload.done.masterId = 99;
    e[6].kind = EventKind::Rejected; e[6].payload.rejected = { 9007199254740993ull, Rejection::RateAboveLimit };
    e[7].kind = EventKind::Error; e[7].payload.error.code = ErrorCode::Memory; e[7].payload.error.recover = Recover::Replay;
    e[7].payload.error.needBytes = 9007199254740991.0;
    need = Wire::eventsBytes (e); json.resize (need.jsonBytes); binary.resize (need.rowBytes / 8);
    if (Wire::events (e, json, binary) != CodecStatus::Ok) return 2;
    print (json, binary);
    if (! frozen) std::printf ("%016llx\n", static_cast<unsigned long long> (config::Config::versions().all));
    auto made = Session::create();
    if (! made.session) return 5;
    char reply[kAnswerBytes]; std::uint32_t size = 0;
    for (const auto request : { R"({"kind":"setManual","commandId":"9007199254740993","on":true})",
                                R"({"kind":"master","commandId":"9007199254740994"})",
                                R"({"kind":"master","commandId":"9007199254740995","unknown":1})" })
    {
        if (Wire::command (*made.session, request, reply, size) != CodecStatus::Ok) return 6;
        std::printf ("%.*s\n", int (size), reply);
    }
    return 0;
}
