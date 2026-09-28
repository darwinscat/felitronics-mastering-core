// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include <alloc_counter.h>
#include <felitronics_test.h>
#include <felitronics/session/Config.h>
#include <felitronics/session/Wire.h>
#include <felitronics/toml/Schema.h>
#include "../../modules/session/src/ProjectIO.h"
#include "fc_session_abi.h"
#include "../../modules/session/tests/FpEnvironmentControl.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace alloc = felitronics::test::alloc;
// Internal implementation checks, intentionally outside the C/wire manifest.
static_assert (unsigned (Command::Load) == 0);
static_assert (unsigned (Command::SetTarget) == 1);
static_assert (unsigned (Command::EditTarget) == 2);
static_assert (unsigned (Command::EditDevice) == 3);
static_assert (unsigned (Command::RevertEdits) == 4);
static_assert (unsigned (Command::SetManual) == 5);
static_assert (unsigned (Command::Master) == 6);
static_assert (unsigned (Command::Cancel) == 7);
static_assert (unsigned (Command::Forget) == 8);
static_assert (unsigned (Command::ImportProject) == 9);
static_assert (unsigned (Event::Measured1) == 0);
static_assert (unsigned (Event::Measured2) == 1);
static_assert (unsigned (Event::Mastered) == 2);
static_assert (unsigned (Column::Empty) == 0);
static_assert (unsigned (Column::Loaded) == 1);
static_assert (unsigned (Column::Measured1) == 2);
static_assert (unsigned (Column::Measured2) == 3);
static_assert (unsigned (Column::Mastering1) == 4);
static_assert (unsigned (Column::Mastering2) == 5);
static_assert (unsigned (EventKind::Phase) == 0);
static_assert (unsigned (EventKind::Fact) == 1);
static_assert (unsigned (EventKind::Reading) == 2);
static_assert (unsigned (EventKind::Done) == 3);
static_assert (unsigned (EventKind::Rejected) == 4);
static_assert (unsigned (EventKind::Error) == 5);
static_assert (unsigned (CodecStatus::Ok) == 0);
static_assert (unsigned (CodecStatus::Invalid) == 1);
static_assert (unsigned (CodecStatus::TooSmall) == 2);
static_assert (unsigned (CodecStatus::FloatingPointEnvironment) == 3);
static_assert (unsigned (Status::Ok) == 0);
static_assert (unsigned (Status::FloatingPointEnvironment) == 1);
static_assert (unsigned (Status::ConfigVersion) == 2);
static_assert (unsigned (Status::Capabilities) == 3);
static_assert (unsigned (Status::Memory) == 4);
static_assert (unsigned (text::Lang::En) == 0);
static_assert (unsigned (text::Lang::De) == 1);
static_assert (unsigned (text::Lang::Ru) == 2);
static_assert (unsigned (text::Lang::Uk) == 3);
static_assert (unsigned (text::Lang::Cs) == 4);
static_assert (unsigned (text::Lang::Es) == 5);
static_assert (unsigned (text::Lang::Fr) == 6);
static_assert (unsigned (text::Lang::It) == 7);
static_assert (unsigned (text::Lang::Pl) == 8);
static_assert (unsigned (text::Lang::Pt) == 9);
static_assert (unsigned (text::Lang::Ro) == 10);
static_assert (unsigned (text::Lang::Tr) == 11);
static_assert (unsigned (text::Plural::Zero) == 0);
static_assert (unsigned (text::Plural::One) == 1);
static_assert (unsigned (text::Plural::Two) == 2);
static_assert (unsigned (text::Plural::Few) == 3);
static_assert (unsigned (text::Plural::Many) == 4);
static_assert (unsigned (text::Plural::Other) == 5);
namespace
{
constexpr char meta[] = R"({"name":"signal","fileRate":48000,"bitDepth":24,"rateKnown":true})";
const fc_session_capabilities full { sizeof (fc_session_capabilities), 9007199254740991.0, 48000, FC_SESSION_DEVICES_ALL, 9007199254740991.0 };
std::uint64_t version() { return config::Config::versions().all; }
fc_session_status create (const fc_session_capabilities& caps, fc_session* out)
{
    return fc_session_create (&caps, std::uint32_t (version()), std::uint32_t (version() >> 32), out);
}
std::string command (fc_session s, std::string_view json)
{
    char reply[FC_SESSION_ANSWER_BYTES]; std::uint32_t n = 0;
    const auto st = fc_session_command (s, json.data(), std::uint32_t (json.size()), reply, sizeof (reply), &n);
    ok (st == FC_SESSION_OK, "command returns a JSON answer"); return { reply, n };
}
bool contains (std::string_view s, std::string_view part) { return s.find (part) != s.npos; }
std::string snapshot (fc_session s)
{
    fc_session_sizes n { sizeof (fc_session_sizes) };
    ok (fc_session_snapshot_size (s, &n) == FC_SESSION_OK, "snapshot size");
    std::string json (n.jsonBytes, '?'); std::vector<double> rows (n.rowBytes / 8);
    ok (fc_session_snapshot_copy (s, json.data(), n.jsonBytes, rows.data(), n.rowBytes) == FC_SESSION_OK, "snapshot copy");
    return json;
}
std::string events (fc_session s)
{
    fc_session_sizes n { sizeof (fc_session_sizes) };
    ok (fc_session_events_size (s, &n) == FC_SESSION_OK, "events size");
    std::string json (n.jsonBytes, '?'); std::vector<double> rows (n.rowBytes / 8);
    ok (fc_session_events_copy (s, json.data(), n.jsonBytes, rows.data(), n.rowBytes) == FC_SESSION_OK, "events copy");
    return json;
}
std::string load (fc_session s, std::uint32_t rate = 48000)
{
    const float samples[] = { 0.0f, 0.25f, -0.25f, 0.0f }; const float* pcm[] = { samples, samples };
    char answer[FC_SESSION_ANSWER_BYTES]; std::uint32_t written = 0;
    ok (fc_session_load (s, 42, 1, pcm, 2, 4, rate, meta, sizeof (meta) - 1, answer, sizeof (answer), &written) == FC_SESSION_OK,
        "load returns its answer");
    return { answer, written };
}
void capabilities()
{
    felitronics::test::group ("capabilities: the C and C++ entries enforce identical limits before work");
    double bytes = 0; fc_session h = 777;
    const auto before = alloc::count.load();
    const auto priced = fc_session_create_bytes (&full, &bytes);
    const auto cppMismatch = Session::create ({}, version() ^ 1);
    const auto mismatch = fc_session_create (&full, std::uint32_t (version()) ^ 1, std::uint32_t (version() >> 32), &h);
    auto small = full; small.heapCeilingBytes = bytes - 1;
    const auto memory = create (small, &h);
    const auto cpp = Session::create ({ small.heapCeilingBytes, small.maxRateHz, small.offeredDevices }, version());
    const auto spent = alloc::count.load() - before;
    ok (priced == FC_SESSION_OK && bytes == double (Session::createBytes()), "create demand precedes creation and is exact");
    ok (cppMismatch.status == Status::ConfigVersion && !cppMismatch.session, "C++ config mismatch refused");
    ok (mismatch == FC_SESSION_ERR_CONFIG_VERSION && h == 777, "config mismatch has its own status and preserves output");
    ok (memory == FC_SESSION_ERR_MEMORY && cpp.status == Status::Memory && !cpp.session, "both APIs refuse unaffordable create");
    ok (spent == 0, "query and refused creates allocate nothing");
    for (double ceiling : { -1.0, 0.5, 9007199254740992.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
    {
        auto c = full; c.heapCeilingBytes = ceiling;
        ok (create (c, &h) == FC_SESSION_ERR_CAPABILITIES && h == 777, "invalid byte ceiling refused");
    }
    auto invalid = full; invalid.maxRateHz = 7999;
    ok (create (invalid, &h) == FC_SESSION_ERR_CAPABILITIES, "invalid max rate");
    invalid = full; invalid.offeredDevices = 256;
    ok (create (invalid, &h) == FC_SESSION_ERR_CAPABILITIES, "unknown device bit");
    auto tight = full; tight.heapCeilingBytes = bytes + 1;
    ok (create (tight, &h) == FC_SESSION_OK, "tiny command budget session");
    const auto snap = snapshot (h);
    const auto count = alloc::count.load();
    float samples[] = { std::numeric_limits<float>::quiet_NaN(), 0 }; const float* pcm[] = { samples };
    char answer[FC_SESSION_ANSWER_BYTES]; std::uint32_t written = 0;
    const auto st = fc_session_load (h, 1, 0, pcm, 1, 2, 48000, meta, sizeof (meta) - 1, answer, sizeof (answer), &written);
    const auto used = alloc::count.load() - count;
    const std::string_view refused (answer, written);
    ok (st == FC_SESSION_OK && contains (refused, "\"code\":30") && contains (refused, "\"needBytes\":"), "memory refuses before scanning NaN samples");
    ok (used == 0 && snapshot (h) == snap, "memory refusal allocates nothing and keeps state");
    const auto batch = events (h);
    ok (contains (batch, "\"kind\":\"error\"") && contains (batch, "\"code\":3") && contains (batch, "\"needBytes\":"), "memory publishes ErrorCode::Memory with demand");
    ok (fc_session_destroy (h) == FC_SESSION_OK, "destroy tight session");
    auto direct = Session::create ({ bytes + 1, 48000, 255 }, version());
    const auto checked = direct.session->check (command::Load { 1, { pcm, 1, 2, 48000 }, {} });
    ok (checked.rejection == Rejection::Memory && checked.needBytes == bytes + 8, "direct C++ memory demand is live plus new demand");
    ok (direct.session->apply (command::Load { 1, { pcm, 1, 2, 48001 }, {} }).rejection == Rejection::RateAboveLimit, "direct C++ maximum rate");
    ok (create (full, &h) == FC_SESSION_OK, "normal session");
    ok (contains (load (h, 48001), "\"code\":29"), "ABI rate limit");
    ok (contains (load (h), "\"kind\":\"accepted\""), "rate boundary accepted");
    ok (fc_session_destroy (h) == FC_SESSION_OK, "destroy normal session");
}
void directCapabilities()
{
    felitronics::test::group ("direct C++ live accounting, import preflight and all offered-device bits");
    const float audio[] { 0, 0.25f, -0.25f, 0 }; const float* pcm[] { audio, audio };
    const auto base = double (Session::createBytes());
    auto tight = Session::create ({ base + 32, 48000, 255 }, version());
    ok (tight.session->apply (command::Load { 1, { pcm, 2, 4, 48000 }, {} }).rejection == Rejection::None,
        "load fits exactly at ceiling");
    const auto revision = tight.session->revision();
    const auto before = alloc::count.load();
    const auto reload = tight.session->apply (command::Load { 2, { pcm, 1, 1, 48000 }, {} });
    const auto stepped = tight.session->step (16);
    const auto imported = tight.session->importProject (3, "not even TOML");
    const auto master = tight.session->apply (command::Master { 4 });
    const auto spent = alloc::count.load() - before;
    ok (reload.rejection == Rejection::Memory && reload.needBytes == base + 36 && reload.revision == revision,
        "reload counts old live PCM plus new demand before disarming");
    ok (stepped.state == StepState::Done && imported.rejection == Rejection::Memory && master.rejection == Rejection::Memory && spent == 0,
        "import refuses before parsing and master before reserving");
    auto restricted = Session::create ({ 9007199254740991.0, 48000, 0 }, version());
    auto& r = *restricted.session;
    ok (r.apply (command::Load { 1, { pcm, 2, 4, 48000 }, {} }).rejection == Rejection::None, "restricted direct load");
    (void) r.step (16); (void) r.apply (command::SetManual { 2, true });
    for (const auto target : { "allStreaming", "cd", "lp" })
    {
        const auto changed = r.apply (command::SetTarget { 3, target });
        ok (changed.rejection == Rejection::None, "every restricted-device target is selected successfully");
        const auto& d = r.project().devices;
        ok (!d.hpf.machine.on && !d.monoBass.machine.on && !d.glue.machine.on && !d.saturation.machine.on
            && !d.tilt.machine.on && !d.dither.machine.on && !d.low.machine.on && d.limiter.machine.needles == Needles::Off,
            "no unoffered machine is active, including after target changes");
    }
    const DeviceEdit edits[] { HpfFields<Touched> {}, MonoBassFields<Touched> {}, GlueFields<Touched> {}, SaturationFields<Touched> {},
                              TiltFields<Touched> {}, LimiterFields<Touched> {}, DitherFields<Touched> {}, LowFields<Touched> {} };
    const DeviceMask masks[] { HpfFields<Mark> {}, MonoBassFields<Mark> {}, GlueFields<Mark> {}, SaturationFields<Mark> {},
                              TiltFields<Mark> {}, LimiterFields<Mark> {}, DitherFields<Mark> {}, LowFields<Mark> {} };
    for (unsigned i = 0; i < 8; ++i)
        ok (r.apply (command::EditDevice { 4, edits[i] }).rejection == Rejection::NotOffered
            && r.apply (command::RevertEdits { 5, masks[i] }).rejection == Rejection::NotOffered, "each unoffered device refuses both edit paths");
    auto ordinary = Session::create(); auto& o = *ordinary.session;
    (void) o.apply (command::Load { 1, { pcm, 2, 4, 48000 }, {} }); (void) o.step (16);
    (void) o.apply (command::SetManual { 2, true }); HpfFields<Touched> on; on.on = true;
    (void) o.apply (command::EditDevice { 3, on }); const auto saved = o.exportProject();
    ok (r.importProject (6, saved.view()).rejection == Rejection::NotOffered, "project cannot restore an unoffered hand");
    std::string machine (saved.view()); const auto hand = machine.find ("on.hand");
    ok (hand != machine.npos, "project control contains an authored activation");
    if (hand != machine.npos) machine.replace (hand, 7, "on.machine");
    const auto core = machine.find ("core = \""); const auto end = machine.find ('"', core + 8);
    machine.replace (core + 8, end - core - 8, "99.0.0");
    ok (r.importProject (7, machine).rejection == Rejection::NotOffered, "a foreign core's project cannot activate an unoffered machine");
}
void guards()
{
    felitronics::test::group ("every v1 output is checked before the handle and input, with no allocation or write");
    fc_session h = 0; ok (create (full, &h) == FC_SESSION_OK, "guard session");
    alignas (8) unsigned char raw[FC_SESSION_ANSWER_BYTES + 16]; std::fill (std::begin (raw), std::end (raw), 0xA5);
    auto* bytes = reinterpret_cast<char*> (raw); auto* odd32 = reinterpret_cast<std::uint32_t*> (raw + 1);
    const float samples[2] {}; const float* pcm[] { samples };
    std::uint32_t out = 777; fc_session_sizes sizes { sizeof (fc_session_sizes), 777, 888 }; double row = 123;
    const auto count = alloc::count.load();
    bool good = fc_session_create_bytes (nullptr, nullptr) == FC_SESSION_ERR_NULL
        && fc_session_create_bytes (nullptr, reinterpret_cast<double*> (raw + 1)) == FC_SESSION_ERR_ALIGNMENT
        && fc_session_create_bytes (nullptr, &row) == FC_SESSION_ERR_NULL
        && fc_session_create (nullptr, 0, 0, odd32) == FC_SESSION_ERR_ALIGNMENT
        && fc_session_create (nullptr, 0, 0, &out) == FC_SESSION_ERR_NULL;
    for (const auto size : { fc_session_events_size, fc_session_snapshot_size })
    {
        good = good && size (0, nullptr) == FC_SESSION_ERR_NULL
            && size (0, reinterpret_cast<fc_session_sizes*> (raw + 1)) == FC_SESSION_ERR_ALIGNMENT
            && size (0, &sizes) == FC_SESSION_ERR_HANDLE;
    }
    for (const auto copy : { fc_session_events_copy, fc_session_snapshot_copy })
    {
        good = good && copy (0, nullptr, 1, nullptr, 0) == FC_SESSION_ERR_NULL
            && copy (0, bytes, 1, reinterpret_cast<double*> (raw + 1), 8) == FC_SESSION_ERR_ALIGNMENT
            && copy (0, bytes, 1, nullptr, 0) == FC_SESSION_ERR_HANDLE
            && copy (h, bytes, 1, nullptr, 0) == FC_SESSION_ERR_TOO_SMALL
            && copy (h, bytes, 16, reinterpret_cast<double*> (raw), 8) == FC_SESSION_ERR_OVERLAP
            && copy (h, bytes, 1, &row, 7) == FC_SESSION_ERR_ALIGNMENT;
    }
    good = good && fc_session_step (0, 1, nullptr) == FC_SESSION_ERR_NULL
        && fc_session_step (0, 1, odd32) == FC_SESSION_ERR_ALIGNMENT
        && fc_session_step (0, 1, &out) == FC_SESSION_ERR_HANDLE
        && fc_session_export_project_size (0, nullptr) == FC_SESSION_ERR_NULL
        && fc_session_export_project_size (0, odd32) == FC_SESSION_ERR_ALIGNMENT
        && fc_session_export_project_size (0, &out) == FC_SESSION_ERR_HANDLE
        && fc_session_export_project_size (h, &out) == FC_SESSION_ERR_NO_SOURCE
        && fc_session_export_project_copy (0, nullptr, 1, nullptr) == FC_SESSION_ERR_NULL
        && fc_session_export_project_copy (0, bytes, 1, odd32) == FC_SESSION_ERR_ALIGNMENT
        && fc_session_export_project_copy (0, bytes, 1, &out) == FC_SESSION_ERR_HANDLE
        && fc_session_export_project_copy (h, bytes, 1, &out) == FC_SESSION_ERR_NO_SOURCE;
    for (int mode = 0; mode < 3; ++mode)
    {
        const auto call = [&] (fc_session handle, const char* input, char* output, std::uint32_t capacity, std::uint32_t* n)
        {
            if (mode == 0) return fc_session_command (handle, input, 1, output, capacity, n);
            if (mode == 1) return fc_session_load (handle, 0, 0, pcm, 1, 2, 48000, input, 1, output, capacity, n);
            return fc_session_import_project (handle, 0, 0, input, 1, output, capacity, n);
        };
        good = good && call (0, nullptr, nullptr, 1, nullptr) == FC_SESSION_ERR_NULL
            && call (0, nullptr, bytes, 1, odd32) == FC_SESSION_ERR_ALIGNMENT
            && call (0, nullptr, bytes, 1, &out) == FC_SESSION_ERR_HANDLE
            && call (h, nullptr, bytes, 1, &out) == FC_SESSION_ERR_NULL
            && call (h, "x", bytes, 1, &out) == FC_SESSION_ERR_TOO_SMALL
            && call (h, bytes, bytes, FC_SESSION_ANSWER_BYTES, &out) == FC_SESSION_ERR_OVERLAP
            && call (h, "x", bytes, FC_SESSION_ANSWER_BYTES, reinterpret_cast<std::uint32_t*> (raw)) == FC_SESSION_ERR_OVERLAP;
    }
    good = good && fc_session_load (h, 0, 0, reinterpret_cast<const float* const*> (raw + 1), 1, 1, 1, nullptr, 0,
                                    bytes, FC_SESSION_ANSWER_BYTES, &out) == FC_SESSION_ERR_ALIGNMENT;
    const auto spent = alloc::count.load() - count;
    ok (good, "all guards return the first error in documented order");
    ok (spent == 0 && out == 777 && sizes.jsonBytes == 777 && sizes.rowBytes == 888 && row == 123, "guard failures allocate and write nothing");
    ok (std::all_of (std::begin (raw), std::end (raw), [] (auto b) { return b == 0xA5; }), "all destination bytes remain untouched");
    ok (fc_session_destroy (h) == FC_SESSION_OK, "guard session destroyed");
}
void fpRefusals()
{
    namespace fp = felitronics::session::testing;
    fc_session h = 0; ok (create (full, &h) == FC_SESSION_OK, "FP session");
    (void) load (h);
    const auto saved = fp::saveFpEnvironment();
    if (!fp::setRounding (fp::kRoundUpward)) { fp::restoreFpEnvironment (saved); (void) fc_session_destroy (h); return; }
    char output[FC_SESSION_ANSWER_BYTES]; std::fill (std::begin (output), std::end (output), '?');
    std::uint32_t n = 777; fc_session_sizes sizes { sizeof (fc_session_sizes), 777, 888 }; double demand = 999;
    const float a[] { 0 }; const float* pcm[] { a };
    const auto before = alloc::count.load();
    const bool refused = fc_session_create_bytes (&full, &demand) == FC_SESSION_ERR_FP_ENVIRONMENT
        && fc_session_command (h, "{}", 2, output, sizeof (output), &n) == FC_SESSION_ERR_FP_ENVIRONMENT
        && fc_session_load (h, 0, 0, pcm, 1, 1, 48000, meta, sizeof (meta) - 1, output, sizeof (output), &n) == FC_SESSION_ERR_FP_ENVIRONMENT
        && fc_session_import_project (h, 0, 0, "x", 1, output, sizeof (output), &n) == FC_SESSION_ERR_FP_ENVIRONMENT
        && fc_session_export_project_size (h, &n) == FC_SESSION_ERR_FP_ENVIRONMENT
        && fc_session_export_project_copy (h, output, sizeof (output), &n) == FC_SESSION_ERR_FP_ENVIRONMENT
        && fc_session_step (h, 1, &n) == FC_SESSION_ERR_FP_ENVIRONMENT
        && fc_session_events_size (h, &sizes) == FC_SESSION_ERR_FP_ENVIRONMENT
        && fc_session_snapshot_size (h, &sizes) == FC_SESSION_ERR_FP_ENVIRONMENT
        && fc_session_events_copy (h, output, sizeof (output), nullptr, 0) == FC_SESSION_ERR_FP_ENVIRONMENT
        && fc_session_snapshot_copy (h, output, sizeof (output), nullptr, 0) == FC_SESSION_ERR_FP_ENVIRONMENT;
    const auto spent = alloc::count.load() - before;
    fp::restoreFpEnvironment (saved);
    ok (refused && spent == 0 && n == 777 && sizes.jsonBytes == 777 && sizes.rowBytes == 888 && demand == 999,
        "all computing ABI entries forward the FP refusal without allocation or output");
    ok (std::all_of (std::begin (output), std::end (output), [] (char c) { return c == '?'; }), "FP refusals leave buffers untouched");
    ok (fc_session_step (h, 16, &n) == FC_SESSION_OK && n == FC_SESSION_DONE, "FP refusal leaves work resumable");
    ok (fc_session_destroy (h) == FC_SESSION_OK, "destroy restored FP session");
}

fc_session_status innerPoison = FC_SESSION_OK;
void poisonDuringWork (std::string_view which)
{
    fc_session h = 0; ok (create (full, &h) == FC_SESSION_OK, "poison work session");
    (void) load (h); std::uint32_t stepped = 0; (void) fc_session_step (h, 16, &stepped);
    std::uint32_t size = 0; (void) fc_session_export_project_size (h, &size);
    std::string project (size, ' '); (void) fc_session_export_project_copy (h, project.data(), size, &size);
    char output[FC_SESSION_ANSWER_BYTES]; std::fill (std::begin (output), std::end (output), '?');
    const float a[] { 0 }; const float* pcm[] { a }; std::uint32_t written = 777;
    alloc::onNext = +[] () noexcept { innerPoison = fc_session_destroy (0); };
    fc_session_status st = FC_SESSION_OK;
    if (which == "command")
    {
        constexpr char json[] = R"({"kind":"master","commandId":"44"})";
        st = fc_session_command (h, json, sizeof (json) - 1, output, sizeof (output), &written);
    }
    else if (which == "load") st = fc_session_load (h, 44, 0, pcm, 1, 1, 48000, meta, sizeof (meta) - 1, output, sizeof (output), &written);
    else st = fc_session_import_project (h, 44, 0, project.data(), size, output, sizeof (output), &written);
    ok (innerPoison == FC_SESSION_ERR_POISONED && st == FC_SESSION_ERR_POISONED, "reentry during work poisons the inner and outer calls");
    ok (written == 777 && std::all_of (std::begin (output), std::end (output), [] (char c) { return c == '?'; }),
        "a call poisoned during work publishes no staged answer");
    fc_session_sizes sizes { sizeof (fc_session_sizes) };
    ok (fc_session_snapshot_size (h, &sizes) == FC_SESSION_ERR_POISONED, "the poisoned session cannot publish a snapshot");
}
void scenario()
{
    felitronics::test::group ("named commands, small work budgets, project replay and owned transfer");
    auto caps = full; caps.offeredDevices = FC_SESSION_DEVICE_HPF;
    fc_session h = 0; ok (create (caps, &h) == FC_SESSION_OK, "restricted devices session");
    const auto initial = snapshot (h);
    for (const auto json : { R"({"kind":"setManual","commandId":"7"})", R"({"kind":"setManual","commandId":"7","on":true,"surprise":1})",
                             R"({"kind":"setManual","commandId":"7","on":true,"on":false})", R"({"kind":"master","commandId":7})" })
        ok (contains (command (h, json), "\"code\":\"contract\""), "invalid named input is a loud refusal");
    ok (snapshot (h) == initial, "malformed commands change no state");
    ok (contains (command (h, R"({"kind":"setManual","commandId":"7"})"), "\"field\":\"on\""), "missing field is named");
    ok (contains (command (h, R"({"kind":"setManual","commandId":"7","on":true,"surprise":1})"), "\"field\":\"surprise\""), "unknown field is named");
    ok (contains (load (h), "\"commandId\":\"4294967338\""), "load command id halves are lossless");
    std::uint32_t more = 99;
    ok (fc_session_step (h, 0, &more) == FC_SESSION_OK && more == FC_SESSION_MORE, "zero budget polls");
    unsigned calls = 0;
    do
    {
        ok (fc_session_step (h, 1, &more) == FC_SESSION_OK, "one unit step");
        ok (contains (events (h), "\"kind\":\"phase\""), "phase batch copies");
        ++calls;
    } while (more == FC_SESSION_MORE && calls < 100);
    ok (more == FC_SESSION_DONE && calls == 10, "small budgets finish exactly ten units");
    auto state = snapshot (h);
    ok (contains (state, "\"offeredDevices\":1") && contains (state, "\"state\":3"), "snapshot carries offered set and measured state");
    ok (contains (state, "\"momentary\":{\"byteOffset\":") && contains (state, "\"integratedLufs\":\"NaN\""), "snapshot rows are descriptors and scalar NaN is explicit");
    ok (contains (command (h, R"({"kind":"setManual","commandId":"8","on":true})"), "accepted"), "manual mode");
    ok (contains (command (h, R"({"kind":"editDevice","commandId":"9","device":1,"fields":{"on":true}})"), "\"code\":9"), "unoffered edit refused");
    ok (contains (command (h, R"({"kind":"revertEdits","commandId":"9","device":1,"fields":{"on":true}})"), "\"code\":9"), "unoffered revert refused");
    ok (contains (command (h, R"({"fields":{"fq":32},"device":0,"commandId":"10","kind":"editDevice"})"), "accepted"), "field order is free; an offered device is editable");
    ok (contains (command (h, R"({"kind":"revertEdits","commandId":"11","device":0,"fields":{"fq":true}})"), "accepted"), "revert named field");
    ok (contains (command (h, R"({"kind":"editTarget","commandId":"12","fields":{"lufs":-14}})"), "accepted"), "edit target");
    ok (contains (command (h, R"({"kind":"editDevice","commandId":"12","device":0,"fields":{"on":false,"fq":36}})"), "accepted"), "device edits before target change");
    ok (contains (snapshot (h), "\"handFieldCount\":2"), "shell can count device edits before warning");
    ok (contains (command (h, R"({"kind":"setTarget","commandId":"13","target":"cd"})"), "accepted"), "set target");
    ok (contains (snapshot (h), "\"handFieldCount\":0") && contains (snapshot (h), "\"hand\":{\"fq\":null,\"on\":null,\"slope\":null}"),
        "wire target change resets device edits and snapshot count");
    std::uint32_t projectSize = 0;
    ok (fc_session_export_project_size (h, &projectSize) == FC_SESSION_OK, "project size");
    std::string project (projectSize, '?'); std::uint32_t written = 777;
    const auto before = alloc::count.load();
    auto st = fc_session_export_project_copy (h, project.data(), projectSize - 1, &written);
    const auto spent = alloc::count.load() - before;
    ok (st == FC_SESSION_ERR_TOO_SMALL && written == 777 && project[0] == '?' && spent == 0, "short export writes and allocates nothing");
    ok (fc_session_export_project_copy (h, project.data(), projectSize, &written) == FC_SESSION_OK && written == projectSize, "export bytes");
    char reply[FC_SESSION_ANSWER_BYTES]; written = 0;
    ok (fc_session_import_project (h, 14, 0, project.data(), projectSize, reply, sizeof (reply), &written) == FC_SESSION_OK
        && contains ({ reply, written }, "accepted"), "same-capabilities project imports through byte buffer");
    ok (contains (command (h, R"({"kind":"master","commandId":"15"})"), "accepted"), "master");
    ok (contains (command (h, R"({"kind":"cancel","commandId":"16","jobId":2})"), "accepted"), "cancel keeps session alive");
    ok (contains (command (h, R"({"kind":"master","commandId":"17"})"), "accepted"), "master again");
    do { ok (fc_session_step (h, 2, &more) == FC_SESSION_OK, "master step"); } while (more == FC_SESSION_MORE);
    ok (contains (events (h), "\"kind\":\"done\""), "done event");
    ok (contains (command (h, R"({"kind":"forget","commandId":"18","masterId":3})"), "accepted"), "forget");
    ok (fc_session_destroy (h) == FC_SESSION_OK && contains (state, "\"state\":3"), "copied snapshot remains readable after destruction");
}
void rows()
{
    felitronics::test::group ("binary rows preserve indices and IEEE-754 non-finite values");
    const ReadingPoint points[] { { 7, -std::numeric_limits<double>::infinity() }, { 8, std::numeric_limits<double>::quiet_NaN() } };
    const ReadingRun runs[] { { 7, 2, 0.5 } };
    SnapshotView v; v.momentary = points; v.shortTerm = { points, 1 }; v.runs = runs;
    const auto need = Wire::snapshotBytes (v); std::string json (need.jsonBytes, '?'); std::vector<double> binary (need.rowBytes / 8, 123);
    const auto before = alloc::count.load();
    const auto shortCopy = Wire::snapshot (v, json, { binary.data(), binary.size() - 1 });
    const auto copied = Wire::snapshot (v, json, binary);
    const auto spent = alloc::count.load() - before;
    ok (shortCopy == CodecStatus::TooSmall && copied == CodecStatus::Ok && spent == 0, "snapshot transfer checks both sizes without allocating");
    ok (need.rowBytes == 72 && binary[0] == 7 && std::isinf (binary[1]) && binary[1] < 0 && std::isnan (binary[3])
        && binary[4] == 7 && binary[5] == 2 && binary[6] == 0.5, "point and run strides hold exact f64 values");
    ok (contains (json, "\"momentary\":{\"byteOffset\":0,\"length\":2,\"stride\":2}")
        && contains (json, "\"runs\":{\"byteOffset\":32,\"length\":1,\"stride\":3}"), "JSON locates binary rows by byte offset");
    const unsigned char half[] { 0, 0, 0, 0, 0, 0, 224, 63 };
    ok (std::memcmp (reinterpret_cast<const unsigned char*> (binary.data()) + 6 * 8, half, 8) == 0,
        "actual encoder output for 0.5 is little-endian IEEE-754 bytes");
    Notification event; event.kind = EventKind::Reading; event.payload.reading.momentary[0] = points[0]; event.payload.reading.momentaryCount = 1;
    const auto n = Wire::eventsBytes ({ &event, 1 }); json.resize (n.jsonBytes); binary.resize (n.rowBytes / 8);
    ok (Wire::events ({ &event, 1 }, json, binary) == CodecStatus::Ok && binary[0] == 7 && std::isinf (binary[1]), "reading events use the same binary representation");
    ReadingPoint tooBig { 9007199254740992ull, 0 }; v.momentary = { &tooBig, 1 };
    ok (Wire::snapshotBytes (v).status == CodecStatus::Invalid, "row indices cannot silently lose integer precision");
}
// Exercise combined faults, so moving a guard can never be hidden by isolated cases.
void demandGuards()
{
    felitronics::test::group ("demand/capacity guard matrix: outputs, handle, input, overlap, session");
    fc_session h = 0; ok (create (full, &h) == FC_SESSION_OK, "matrix session");
    using Query = fc_session_status (*) (fc_session, const char*, std::uint32_t, fc_session_storage*);
    const Query queries[] { fc_session_command_bytes, fc_session_import_project_bytes,
        +[] (fc_session session, const char* input, std::uint32_t n, fc_session_storage* out) {
            return fc_session_load_bytes (session, 0, 0, 0, input, n, out);
        } };
    alignas (fc_session_storage) unsigned char raw[sizeof (fc_session_storage) + 8];
    std::fill (std::begin (raw), std::end (raw), 0xA5);
    const auto* badInput = reinterpret_cast<const char*> (UINTPTR_MAX - 7);
    auto* badOutput = reinterpret_cast<fc_session_storage*> (UINTPTR_MAX - 7);
    for (const auto query : queries)
    {
        fc_session_storage out { sizeof (fc_session_storage), 777, 888, 999, 111 };
        unsigned char unchanged[sizeof (out)]; std::memcpy (unchanged, &out, sizeof (out));
        const auto count = alloc::count.load();
        bool good = true;
        const auto expect = [&] (fc_session handle, const char* input, std::uint32_t n, fc_session_storage* output, fc_session_status want) {
            unsigned char previous[sizeof (out)]; std::memcpy (previous, &out, sizeof (out));
            const auto got = query (handle, input, n, output);
            good = good && got == want && std::memcmp (&out, previous, sizeof (out)) == 0;
        };
        expect (0, nullptr, 1, nullptr, FC_SESSION_ERR_NULL);
        expect (0, nullptr, 1, reinterpret_cast<fc_session_storage*> (raw + 1), FC_SESSION_ERR_ALIGNMENT);
        expect (0, nullptr, 1, badOutput, FC_SESSION_ERR_SPAN);
        for (const auto size : { 0u, unsigned (sizeof (out) - 1), unsigned (sizeof (out) + 1) })
        {
            out.size = size;
            expect (0, reinterpret_cast<const char*> (&out), sizeof (out), &out,
                size < sizeof (out) ? FC_SESSION_ERR_STRUCT_TOO_SMALL : FC_SESSION_ERR_STRUCT_TOO_LARGE);
        }
        out.size = sizeof (out);
        expect (0, nullptr, 1, &out, FC_SESSION_ERR_HANDLE);
        expect (0, badInput, 16, &out, FC_SESSION_ERR_HANDLE);
        expect (0, reinterpret_cast<const char*> (&out), sizeof (out), &out, FC_SESSION_ERR_HANDLE);
        expect (h, nullptr, 1, &out, FC_SESSION_ERR_NULL);
        expect (h, badInput, 16, &out, FC_SESSION_ERR_SPAN);
        expect (h, reinterpret_cast<const char*> (&out), sizeof (out), &out, FC_SESSION_ERR_OVERLAP);
        expect (h, reinterpret_cast<const char*> (&out) + sizeof (out) - 1, 1, &out, FC_SESSION_ERR_OVERLAP);
        const auto spent = alloc::count.load() - count;
        ok (good, "query combined faults follow the declared refusal order");
        ok (spent == 0 && std::memcmp (&out, unchanged, sizeof (out)) == 0
            && std::all_of (std::begin (raw), std::end (raw), [] (unsigned char c) { return c == 0xA5; }),
            "every refused query leaves all output bytes unchanged and allocates zero");
    }
    fc_session_capacity cap { sizeof (fc_session_capacity), -1, -1 };
    unsigned char unchanged[sizeof (cap)]; std::memcpy (unchanged, &cap, sizeof (cap));
    const auto count = alloc::count.load();
    bool good = true;
    const auto expect = [&] (fc_session handle, const fc_session_capacity* input, fc_session_status want) {
        unsigned char previous[sizeof (cap)]; std::memcpy (previous, &cap, sizeof (cap));
        const auto got = fc_session_set_capacity (handle, input);
        good = good && got == want && std::memcmp (&cap, previous, sizeof (cap)) == 0;
    };
    expect (0, nullptr, FC_SESSION_ERR_HANDLE);
    expect (0, reinterpret_cast<const fc_session_capacity*> (raw + 1), FC_SESSION_ERR_HANDLE);
    expect (0, reinterpret_cast<const fc_session_capacity*> (badInput), FC_SESSION_ERR_HANDLE);
    expect (h, nullptr, FC_SESSION_ERR_NULL);
    expect (h, reinterpret_cast<const fc_session_capacity*> (raw + 1), FC_SESSION_ERR_ALIGNMENT);
    expect (h, reinterpret_cast<const fc_session_capacity*> (badInput), FC_SESSION_ERR_SPAN);
    for (const auto size : { 0u, unsigned (sizeof (cap) - 1), unsigned (sizeof (cap) + 1) })
    {
        cap.size = size;
        expect (0, &cap, FC_SESSION_ERR_HANDLE);
        expect (h, &cap, size < sizeof (cap) ? FC_SESSION_ERR_STRUCT_TOO_SMALL : FC_SESSION_ERR_STRUCT_TOO_LARGE);
    }
    cap.size = sizeof (cap); expect (h, &cap, FC_SESSION_ERR_CAPABILITIES);
    const auto spent = alloc::count.load() - count;
    ok (good && spent == 0 && std::memcmp (&cap, unchanged, sizeof (cap)) == 0,
        "capacity handle precedes input guards and value checks, without writes or allocations");
    ok (contains (load (h), "accepted"), "refused capacity updates preserve the session capacity");
    ok (fc_session_destroy (h) == FC_SESSION_OK, "matrix session destroyed");
}
void freezeRegressions()
{
    felitronics::test::group ("v1 freeze: size prefixes, live demand, protocol events, first-fault order, export reasons");
    fc_session h = 0; ok (create (full, &h) == FC_SESSION_OK, "freeze session");
    for (const auto size : { 0u, unsigned (sizeof (full) - 1), unsigned (sizeof (full) + 1) })
    {
        auto caps = full; caps.size = size; fc_session out = 777; double bytes = 777;
        const auto want = size < sizeof (full) ? FC_SESSION_ERR_STRUCT_TOO_SMALL : FC_SESSION_ERR_STRUCT_TOO_LARGE;
        ok (create (caps, &out) == want && out == 777 && fc_session_create_bytes (&caps, &bytes) == want && bytes == 777,
            "capabilities size is checked without writes");
    }
    for (const auto size : { 0u, 11u, 13u })
    {
        fc_session_sizes sizes { size, 777, 888 };
        const auto want = size < sizeof (sizes) ? FC_SESSION_ERR_STRUCT_TOO_SMALL : FC_SESSION_ERR_STRUCT_TOO_LARGE;
        ok (fc_session_snapshot_size (0, &sizes) == want && fc_session_events_size (0, &sizes) == want
            && sizes.jsonBytes == 777 && sizes.rowBytes == 888, "output size check precedes bad handle");
    }
    char json[FC_SESSION_ANSWER_BYTES]; std::uint32_t written = 777;
    alignas (8) double rows[2] {};
    for (const auto copy : { fc_session_events_copy, fc_session_snapshot_copy })
        ok (copy (0, json, 1, rows, 7) == FC_SESSION_ERR_ALIGNMENT
            && copy (h, reinterpret_cast<char*> (rows), 8, rows, 7) == FC_SESSION_ERR_ALIGNMENT,
            "row capacity alignment precedes bad handle and overlap");
    ok (fc_session_export_project_size (h, &written) == FC_SESSION_ERR_NO_SOURCE
        && fc_session_export_project_copy (h, json, sizeof (json), &written) == FC_SESSION_ERR_NO_SOURCE, "export retains NoSource");
    fc_session_storage price { sizeof (fc_session_storage) };
    const auto count = alloc::count.load();
    const auto quoted = fc_session_load_bytes (h, 2, 4, 48000, meta, sizeof (meta) - 1, &price);
    const auto spent = alloc::count.load() - count;
    ok (quoted == FC_SESSION_OK && price.rejection == 0 && price.bytes == 38 && price.largestBlockBytes == 32 && spent == 0,
        "load demand uses shape and metadata without PCM or allocation");
    auto direct = Session::create();
    const auto cppPrice = direct.session->storageFor (command::Load { 0, { nullptr, 2, 4, 48000 }, { "signal" } });
    ok (cppPrice.bytes == std::uint64_t (price.bytes) && cppPrice.largestBlockBytes == 32, "C load demand is C++ storageFor");
    const auto base = price.liveBytes;
    for (const bool fragmented : { false, true })
    {
        fc_session_capacity cap { sizeof (fc_session_capacity), fragmented ? base + 38 : base + 37, fragmented ? 31.0 : 32.0 };
        ok (fc_session_set_capacity (h, &cap) == FC_SESSION_OK, "capacity can shrink between calls");
        const auto before = alloc::count.load();
        const float a[] { std::numeric_limits<float>::quiet_NaN(), 0, 0, 0 }; const float* pcm[] { a, a };
        const auto st = fc_session_load (h, 1, 0, pcm, 2, 4, 48000, meta, sizeof (meta) - 1, json, sizeof (json), &written);
        const auto allocations = alloc::count.load() - before;
        ok (st == FC_SESSION_OK && contains ({ json, written }, "\"code\":30") && contains ({ json, written }, "\"needBytes\":")
            && allocations == 0, "ceiling and largest block refuse before NaN sample scan");
        ok (direct.session->setCapacity ({ cap.heapCeilingBytes, cap.largestFreeBlockBytes }) == Status::Ok
            && direct.session->apply (command::Load { 1, { pcm, 2, 4, 48000 }, { "signal" } }).rejection == Rejection::Memory,
            "C++ uses the same live capacity and largest-block check");
    }
    fc_session_capacity restored { sizeof (fc_session_capacity), 9007199254740991.0, 9007199254740991.0 };
    ok (fc_session_set_capacity (h, &restored) == FC_SESSION_OK && contains (load (h), "accepted"), "capacity can grow again");
    ok (fc_session_export_project_size (h, &written) == FC_SESSION_ERR_NOT_PLACED
        && fc_session_export_project_copy (h, json, sizeof (json), &written) == FC_SESSION_ERR_NOT_PLACED, "export retains NotPlaced");
    for (const auto malformed : { "{", R"({"kind":"master"})", R"({"kind":"master","commandId":"5","unknown":1})" })
    {
        ok (fc_session_step (h, 1, &written) == FC_SESSION_OK, "step before malformed command");
        const auto phase = events (h); const auto seqAt = phase.find ("\"seq\":\"");
        const auto seq = std::stoull (phase.substr (seqAt + 7));
        ok (contains (command (h, malformed), "\"code\":\"contract\""), "protocol refusal answer");
        const auto rejected = events (h);
        ok (contains (rejected, "\"kind\":\"rejected\"") && ! contains (rejected, "\"kind\":\"phase\"")
            && contains (rejected, "\"seq\":\"" + std::to_string (seq + 1) + "\"") && contains (rejected, "\"code\":31"),
            "malformed command replaces batch and advances sequence once");
    }
    (void) fc_session_step (h, 16, &written);
    constexpr char master[] = R"({"kind":"master","commandId":"8"})";
    ok (fc_session_command_bytes (h, master, sizeof (master) - 1, &price) == FC_SESSION_OK && price.rejection == 0 && price.bytes > 0,
        "command demand comes from the same JSON before allocation");
    const auto masterPrice = price;
    restored.largestFreeBlockBytes = price.largestBlockBytes - 1;
    ok (fc_session_set_capacity (h, &restored) == FC_SESSION_OK && contains (command (h, master), "\"code\":30"), "fragmented master refused");
    ok (fc_session_command_bytes (h, master, sizeof (master) - 1, &price) == FC_SESSION_OK && price.bytes == masterPrice.bytes,
        "demand remains available under insufficient capacity");
    constexpr char project[] = "invalid TOML";
    ok (fc_session_import_project_bytes (h, project, sizeof (project) - 1, &price) == FC_SESSION_OK && price.rejection == 0 && price.bytes > 0,
        "import demand is quoted from bytes before its allocating parse");
    constexpr std::string_view required[] { "defaults", "core", "manual", "target", "target.name" };
    const auto library = felitronics::toml::storageFor (project, felitronics::toml::ReadStorage { required });
    const auto importBytes = library.parse + library.read + detail::ImportedProject::storageBytes();
    ok (price.bytes == double (importBytes) && price.largestBlockBytes == price.bytes,
        "C import demand is exactly TOML parse + read + owned storage, with the conservative block bound");
    restored.largestFreeBlockBytes = price.largestBlockBytes - 1;
    (void) fc_session_set_capacity (h, &restored);
    const auto before = alloc::count.load();
    const auto imported = fc_session_import_project (h, 9, 0, project, sizeof (project) - 1, json, sizeof (json), &written);
    const auto allocations = alloc::count.load() - before;
    ok (imported == FC_SESSION_OK && contains ({ json, written }, "\"code\":30") && allocations == 0,
        "import checks largest block before parsing malformed bytes");
    price.size = sizeof (price) + 1;
    ok (fc_session_command_bytes (0, nullptr, 1, &price) == FC_SESSION_ERR_STRUCT_TOO_LARGE, "demand output size before handle");
    restored.size = sizeof (restored) - 1;
    ok (fc_session_set_capacity (0, &restored) == FC_SESSION_ERR_HANDLE
        && fc_session_set_capacity (h, &restored) == FC_SESSION_ERR_STRUCT_TOO_SMALL, "capacity handle before input size");
    ok (fc_session_destroy (h) == FC_SESSION_OK, "freeze session destroyed");
}

void poison()
{
    felitronics::test::group ("every new entry refuses poison before null outputs or handles");
    fc_session h = 0;
    alloc::onNext = +[] () noexcept { (void) fc_session_destroy (0); };
    const auto outer = create (full, &h);
    ok (outer == FC_SESSION_ERR_POISONED && h == 0, "reentered create publishes no handle");
    const auto before = alloc::count.load();
    const bool all = fc_session_create_bytes (nullptr, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_set_capacity (0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_command_bytes (0, nullptr, 0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_load_bytes (0, 0, 0, 0, nullptr, 0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_import_project_bytes (0, nullptr, 0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_command (0, nullptr, 0, nullptr, 0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_load (0, 0, 0, nullptr, 0, 0, 0, nullptr, 0, nullptr, 0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_import_project (0, 0, 0, nullptr, 0, nullptr, 0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_export_project_size (0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_export_project_copy (0, nullptr, 0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_step (0, 0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_events_size (0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_snapshot_size (0, nullptr) == FC_SESSION_ERR_POISONED
        && fc_session_events_copy (0, nullptr, 0, nullptr, 0) == FC_SESSION_ERR_POISONED
        && fc_session_snapshot_copy (0, nullptr, 0, nullptr, 0) == FC_SESSION_ERR_POISONED;
    const auto spent = alloc::count.load() - before;
    ok (all && spent == 0, "poison is first and requests no heap memory");
}
}
int main (int argc, char** argv)
{
    if (argc == 2) poisonDuringWork (argv[1]);
    else { capabilities(); directCapabilities(); guards(); fpRefusals(); scenario(); rows(); demandGuards(); freezeRegressions(); poison(); }
    return felitronics::test::report();
}
