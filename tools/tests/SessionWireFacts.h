// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once
#include <algorithm>
#include "../../modules/session/src/JsonCodec.h"
#include "../../modules/session/src/Devices.h"
#include "../../modules/session/src/Driver.h"
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
    HpfFields<Value> hpf;
    std::uint8_t frequencyId = 255;
    detail::DeviceOf<decltype (hpf)>::each (detail::rules(), [&] (std::uint8_t id, const detail::FieldRule&, auto& field)
    { if (static_cast<const void*> (&field) == &hpf.fq) frequencyId = id; }, hpf);
    const MachineDifference difference[] { { Device::Hpf, frequencyId, 24, 32 } };
    v.machineDifferences = difference;
    const EqPoint curve[] { { 20, -3.5 }, { 1000, 0.25 } };
    v.eqCurve = curve; v.handFieldCount = 2;
    v.momentary = points; v.shortTerm = { points, 1 }; v.runs = runs;
    v.sourceBytes = 9007199254740991.0; v.integratedLufs = -std::numeric_limits<double>::infinity();
    const double values[] { 0.25, 0.5, 0.75 };
    const MeasurementArray array { "peaks", { 0, 1024, 3072, 48000 }, 1, 3, 3, true, values };
    MeasurementResult measurement;
    measurement.analyzer = Analyzer::Waveform; measurement.status = MeasurementStatus::Ready;
    measurement.reason = MeasurementReason::None; measurement.key = 9007199254740993ull;
    measurement.framesRead = 3072; measurement.total = 3; measurement.stored = 3; measurement.complete = true;
    measurement.arrays = { &array, 1 };
    if (! frozen) v.measurements = { &measurement, 1 };
    auto need = Wire::snapshotBytes (v);
    std::string json (need.jsonBytes, ' '); std::vector<double> binary (need.rowBytes / 8);
    if (Wire::snapshot (v, json, binary) != CodecStatus::Ok) return 1;
    print (json, binary);
    Notification e[9];
    for (unsigned i = 0; i < 9; ++i) { e[i].seq = 9007199254741000ull + i; e[i].jobId = 42; }
    for (unsigned i = 0; i < 3; ++i) { e[i].kind = EventKind::Phase; e[i].payload.phase.name = PhaseName (5 + i); }
    e[3].kind = EventKind::Fact;
    (void) e[3].payload.fact.assign (text::Fact::of (text::FactId::DefaultsConverted, text::Arg::text ("line\nvoice"),
        text::Arg::count (std::numeric_limits<std::int64_t>::min()), text::Arg::value (-14.0, text::Unit::Lufs, 1)));
    e[4].kind = EventKind::Reading;
    e[4].payload.reading.momentary[0] = points[0]; e[4].payload.reading.momentaryCount = 1;
    e[4].payload.reading.shortTerm[0] = points[1]; e[4].payload.reading.shortTermCount = 1;
    e[4].payload.reading.runs[0] = runs[0]; e[4].payload.reading.runCount = 1;
    if (! frozen)
    {
        e[4].payload.reading.clipCount = 1;
        const double clip[] { 7, 2, 0, 1, 0.5, 1 };
        std::copy_n (clip, 6, e[4].payload.reading.clips);
    }
    e[5].kind = EventKind::Done; e[5].payload.done.masterId = 99;
    e[6].kind = EventKind::Rejected; e[6].payload.rejected = { 9007199254740993ull, Rejection::RateAboveLimit };
    e[7].kind = EventKind::Error; e[7].payload.error.code = ErrorCode::Memory; e[7].payload.error.recover = Recover::Replay;
    e[7].payload.error.needBytes = 9007199254740991.0;
    e[8].kind = EventKind::Measurement;
    e[8].payload.measurement = { Analyzer::Waveform, MeasurementStatus::Ready, MeasurementReason::None,
                                measurement.key, 7, 8, 3072, 3, 3, true };
    const std::span<const Notification> batch { e, frozen ? 8u : 9u };
    need = Wire::eventsBytes (batch); json.resize (need.jsonBytes); binary.resize (need.rowBytes / 8);
    if (Wire::events (batch, json, binary) != CodecStatus::Ok) return 2;
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
    const float samples[] { 0, 0.25f, -0.25f, 0 }; const float* pcm[] { samples, samples };
    if (Wire::load (*made.session, 10, { pcm, 2, 4, 48000 },
        R"({"name":"wire.wav","fileRate":44100,"rateKnown":true,"bitDepth":24})", reply, size) != CodecStatus::Ok) return 7;
    std::printf ("%.*s\n", int (size), reply);
    // Observe the parsed metadata as well as its answer, without freezing config-dependent values.
    char effect[8192]; detail::Writer writer; writer.output = effect;
    writer.value (made.session->snapshot().view().source);
    std::printf ("json load-source %.*s\n", int (writer.size), effect);
    // Freeze commands from a measured state, independent of added analyzer publications.
    // The live pump and its revisions are exercised by the measurement suites.
    const auto job = made.session->measurementJob();
    const auto source = made.session->source().hash;
    if (job != 0 && (! detail::Driver::measured1 (*made.session, job, source)
        || ! detail::Driver::measured2 (*made.session, job, source))) return 10;
    unsigned fixture = 0;
    for (const auto request : {
        R"({"kind":"setTarget","commandId":"11","target":"cd"})",
        R"({"kind":"editTarget","commandId":"12","fields":{"lufs":-13.25,"tp":-1.25}})",
        R"({"kind":"editDevice","commandId":"13","device":0,"fields":{"on":true,"fq":36,"slope":24}})",
        R"({"kind":"editDevice","commandId":"14","device":1,"fields":{"on":true,"fq":120,"width":0.5}})",
        R"({"kind":"editDevice","commandId":"15","device":2,"fields":{"on":true,"upToDb":1.5}})",
        R"({"kind":"editDevice","commandId":"16","device":3,"fields":{"on":true,"drive":1,"mix":0.5,"output":-1}})",
        R"({"kind":"editDevice","commandId":"17","device":4,"fields":{"on":true,"db":1.25}})",
        R"({"kind":"editDevice","commandId":"18","device":5,"fields":{"needles":1,"needlesDb":2}})",
        R"({"kind":"editDevice","commandId":"19","device":6,"fields":{"on":true}})",
        R"({"kind":"editDevice","commandId":"20","device":7,"fields":{"on":true,"db":0.75}})",
        R"({"kind":"revertEdits","commandId":"21","device":0,"fields":{"on":true,"fq":true,"slope":true}})",
        R"({"kind":"revertEdits","commandId":"22","device":1,"fields":{"on":true,"fq":true,"width":true}})",
        R"({"kind":"revertEdits","commandId":"23","device":2,"fields":{"on":true,"upToDb":true}})",
        R"({"kind":"revertEdits","commandId":"24","device":3,"fields":{"on":true,"drive":true,"mix":true,"output":true}})",
        R"({"kind":"revertEdits","commandId":"25","device":4,"fields":{"on":true,"db":true}})",
        R"({"kind":"revertEdits","commandId":"26","device":5,"fields":{"needles":true,"needlesDb":true}})",
        R"({"kind":"revertEdits","commandId":"27","device":6,"fields":{"on":true}})",
        R"({"kind":"revertEdits","commandId":"28","device":7,"fields":{"on":true,"db":true}})",
        R"({"kind":"editTarget","commandId":"29","fields":{"lufs":null,"tp":null}})",
        R"({"kind":"setTarget","commandId":"30","target":"lp"})",
        R"({"kind":"master","commandId":"31"})",
        R"({"kind":"cancel","commandId":"32","jobId":2})",
        R"({"kind":"forget","commandId":"33","masterId":99})",
        R"({"kind":"master","commandId":"34","unknown":1})",
        R"({"kind":"master"})",
        R"({"kind":"setManual","commandId":"36","on":true,"on":false})",
        R"({"kind":"setManual","commandId":"37","on":1})" })
    {
        if (Wire::command (*made.session, request, reply, size) != CodecStatus::Ok) return 8;
        std::printf ("%.*s\n", int (size), reply);
        const auto& project = made.session->project();
        writer = {}; writer.output = effect; writer.put ('{');
        writer.field ("target", made.session->targetName()); writer.field ("manual", project.manual);
        writer.field ("targetEdit", project.targetEdit);
        detail::eachDevice (project.devices, [&] (Device, const auto& layer) {
            using Of = detail::DeviceOf<std::remove_cvref_t<decltype (layer.hand)>>;
            writer.field (Of::name, layer.hand);
        });
        writer.put ('}');
        std::printf ("json command-effect-%u %.*s\n", fixture++, int (writer.size), effect);
    }
    if (Wire::importProject (*made.session, 38, "invalid TOML", reply, size) != CodecStatus::Ok) return 9;
    std::printf ("%.*s\n", int (size), reply);
    return 0;
}
