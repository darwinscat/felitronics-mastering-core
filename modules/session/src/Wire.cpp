// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "BuildContract.h"
#include "JsonCodec.h"
#include "Devices.h"
#include <felitronics/session/Text.h>
#include <felitronics/session/Wire.h>
#include <algorithm>
#include <limits>
#include <bit>

static_assert (std::endian::native == std::endian::little, "fc_session v1 requires little-endian f64 rows");

namespace felitronics::session
{
namespace
{
using detail::Writer;
using detail::Reader;
using detail::Storage;

struct Problem
{
    std::string_view reason;
    char name[kCommandJsonBytes] {};
    std::size_t size = 0;
    void set (std::string_view why, std::string_view field) noexcept
    {
        if (! reason.empty()) return;
        reason = why; size = std::min (field.size(), sizeof (name));
        std::copy_n (field.data(), size, name);
    }
};

// A bounded object index over the original bytes. Values are read with the snapshot codec's
// scalar grammar. No parse or refusal asks the heap for memory; nesting and member counts are bounded.
bool skip (Reader& r, unsigned depth) noexcept
{
    if (depth > 16) return false;
    r.space();
    if (r.pos == r.input.size()) return false;
    const char c = r.input[r.pos];
    if (c == '{' || c == '[')
    {
        ++r.pos; const char end = c == '{' ? '}' : ']';
        if (r.take (end)) return true;
        do
        {
            if (c == '{') { (void) r.string (nullptr, 0); r.expect (':'); }
            if (! r.good || ! skip (r, depth + 1)) return false;
        } while (r.take (','));
        return r.take (end);
    }
    if (c == '"') (void) r.string (nullptr, 0);
    else if (! r.literal ("null") && ! r.literal ("true") && ! r.literal ("false")) (void) r.number();
    return r.good;
}
struct Object
{
    struct Member { std::string_view key, value; bool used = false; };
    Member members[32] {};
    std::size_t count = 0;
    char text[kCommandJsonBytes] {};
    Storage storage;
    Problem& problem;
    explicit Object (std::string_view json, Problem& error) noexcept : problem (error)
    {
        storage.text = text;
        if (json.size() > kCommandJsonBytes || ! detail::validUtf8 (json)) { problem.set ("invalid", "json"); return; }
        Reader r { json, storage, 0, true, {}, 0, 0, false };
        r.expect ('{');
        if (! r.take ('}'))
        {
            do
            {
                if (count == 32) { r.good = false; break; }
                const auto n = r.string (text + storage.chars, sizeof (text) - storage.chars);
                if (! r.good) break;
                const std::string_view key (text + storage.chars, n); storage.chars += n;
                for (std::size_t i = 0; i < count; ++i)
                    if (members[i].key == key) problem.set ("duplicate", key);
                r.expect (':'); r.space(); const auto begin = r.pos;
                if (! r.good || ! skip (r, 0)) { r.good = false; break; }
                members[count++] = { key, json.substr (begin, r.pos - begin), false };
            } while (r.take (','));
            r.expect ('}');
        }
        r.space();
        if (! r.good || r.pos != json.size()) problem.set ("invalid", "json");
    }
    std::string_view take (std::string_view key, bool required = true) noexcept
    {
        for (std::size_t i = 0; i < count; ++i)
            if (members[i].key == key) { members[i].used = true; return members[i].value; }
        if (required) problem.set ("missing", key);
        return {};
    }
    template<class T> void get (std::string_view key, T& value, bool required = true) noexcept
    {
        const auto input = take (key, required);
        if (input.empty()) return;
        Reader r { input, storage, 0, true, {}, 0, 0, false }; r.value (value); r.space();
        if (! r.good || r.pos != input.size()) problem.set ("invalid", key);
    }
    void finish() noexcept
    {
        for (std::size_t i = 0; i < count; ++i)
            if (! members[i].used) { problem.set ("unknown", members[i].key); break; }
    }
};

template <std::size_t I = 0, class V, class F> bool deviceFields (unsigned device, V& variant, F&& f) noexcept
{
    if (device == I)
    {
        variant.template emplace<I>(); f (*std::get_if<I> (&variant)); return true;
    }
    if constexpr (I + 1 < std::variant_size_v<V>) return deviceFields<I + 1> (device, variant, std::forward<F> (f));
    return false;
}
void begin (Writer& w) noexcept { w.put ('{'); w.first = true; }
void end (Writer& w) noexcept { w.put ('}'); }
// A rejection carries its fact last (text::Text::rejected): the field named, its refused number and the domain it left
// filled by the core, so a shell maps no index to a word. The request is the one the answer is for.
void answer (Writer& w, const Answer& a, const Request& request) noexcept
{
    begin (w);
    w.field ("kind", std::string_view (a.rejection == Rejection::None ? "accepted" : "rejected"));
    w.field ("commandId", a.command); w.field ("revision", a.revision);
    if (a.rejection == Rejection::None) w.field ("jobId", a.job);
    else
    {
        w.field ("code", unsigned (a.rejection)); w.field ("needBytes", a.needBytes);
        w.field ("field", unsigned (a.field));
        w.field ("device", a.device ? int (*a.device) : -1);
        w.field ("line", a.position.line); w.field ("column", a.position.column);
        w.field ("fact", text::Text::rejected (a, request));
    }
    end (w);
}
void problemAnswer (Writer& w, CommandId id, std::uint64_t revision, const Problem& p) noexcept
{
    begin (w); w.field ("kind", std::string_view ("rejected")); w.field ("commandId", id);
    w.field ("revision", revision); w.field ("code", std::string_view ("contract"));
    w.field ("reason", p.reason); w.field ("field", std::string_view (p.name, p.size));
    w.field ("fact", text::Fact::of (text::FactId::RejectedContract)); end (w);
}
void fact (Writer& w, const OwnedFact& owned) noexcept { w.value (owned.view()); }
void eventList (Writer& w, std::span<const Notification> events) noexcept
{
    w.put ('['); bool first = true;
    for (const auto& e : events)
    {
        if (! first) w.put (',');
        first = false;
        begin (w); w.field ("seq", e.seq); w.field ("jobId", e.jobId);
        constexpr std::string_view kinds[] = { "phase", "fact", "reading", "done", "rejected", "error", "measurement" };
        if (unsigned (e.kind) >= std::size (kinds)) { w.good = false; return; }
        w.field ("source", e.source); w.field ("revision", e.revision);
        w.field ("state", e.state); w.field ("phase", e.phase); w.field ("completedWork", e.completedWork); w.field ("totalWork", e.totalWork);
        w.field ("kind", kinds[unsigned (e.kind)]); w.text (",\"payload\":");
        switch (e.kind)
        {
            case EventKind::Measurement: w.value (e.payload.measurement); break;
            case EventKind::Phase: w.value (e.payload.phase); break;
            case EventKind::Fact: fact (w, e.payload.fact); break;
            case EventKind::Reading:
            {
                const auto& r = e.payload.reading;
                if (r.momentaryCount > 4 || r.shortTermCount > 4 || r.runCount > 4 || r.clipCount > 4) { w.good = false; return; }
                begin (w); w.field ("momentary", std::span<const ReadingPoint> (r.momentary, r.momentaryCount));
                w.field ("shortTerm", std::span<const ReadingPoint> (r.shortTerm, r.shortTermCount));
                w.field ("runs", std::span<const ReadingRun> (r.runs, r.runCount));
                w.field ("grid", r.grid); w.field ("momentaryReason", r.momentaryReason); w.field ("shortTermReason", r.shortTermReason);
                w.field ("clips", std::span<const double> (r.clips, 6u * r.clipCount));
                w.field ("totalRuns", r.totalRuns); w.field ("storedRuns", r.storedRuns); w.field ("tailFrames", r.tailFrames);
                w.field ("runsComplete", r.runsComplete); w.field ("finished", r.finished); end (w); break;
            }
            case EventKind::Done: begin (w); w.field ("masterId", e.payload.done.masterId); end (w); break;
            case EventKind::Rejected:
                begin (w); w.field ("commandId", e.payload.rejected.commandId);
                w.field ("code", unsigned (e.payload.rejected.code)); end (w); break;
            case EventKind::Error:
            {
                const auto& error = e.payload.error;
                if (! std::isfinite (error.needBytes) || error.needBytes < 0 || error.needBytes >= 9007199254740992.0
                    || std::bit_cast<std::uint64_t> (double (std::uint64_t (error.needBytes))) != std::bit_cast<std::uint64_t> (error.needBytes)) w.good = false;
                begin (w); w.field ("code", unsigned (error.code)); w.field ("recover", unsigned (error.recover));
                w.field ("needBytes", error.needBytes); w.text (",\"fact\":"); fact (w, error.fact); end (w); break;
            }
        }
        end (w);
    }
    w.put (']');
}
TransferNeed need (const Writer& w) noexcept
{
    if (! w.good || w.size > 4294967295ull || w.rowSize > 4294967295ull / sizeof (double))
        return { CodecStatus::Invalid, 0, 0 };
    return { CodecStatus::Ok, std::uint32_t (w.size), std::uint32_t (w.rowSize * sizeof (double)) };
}
CodecStatus buffers (const TransferNeed& n, std::span<char> json, std::span<double> rows) noexcept
{
    if (n.status != CodecStatus::Ok) return n.status;
    return json.size() < n.jsonBytes || rows.size_bytes() < n.rowBytes ? CodecStatus::TooSmall : CodecStatus::Ok;
}
}
TransferNeed Wire::snapshotBytes (const SnapshotView& v) noexcept
{
    const auto check = Codec::encodedBytes (v);
    if (check.status != CodecStatus::Ok) return { check.status, 0, 0 };
    Writer w; w.binaryRows = true; w.value (v); return need (w);
}
CodecStatus Wire::snapshot (const SnapshotView& v, std::span<char> json, std::span<double> rows) noexcept
{
    const auto st = buffers (snapshotBytes (v), json, rows); if (st != CodecStatus::Ok) return st;
    Writer w; w.output = json.data(); w.binaryRows = true; w.rows = rows.data(); w.value (v); return CodecStatus::Ok;
}
TransferNeed Wire::snapshotBytes (const Session& s) noexcept { return snapshotBytes (s.buildView()); }
CodecStatus Wire::snapshot (const Session& s, std::span<char> j, std::span<double> r) noexcept { return snapshot (s.buildView(), j, r); }
TransferNeed Wire::summaryBytes (const Session& s) noexcept
{
    MeasurementResult results[kAnalyzers]; return snapshotBytes (s.buildSummary (results));
}
CodecStatus Wire::summary (const Session& s, std::span<char> j, std::span<double> r) noexcept
{
    MeasurementResult results[kAnalyzers]; return snapshot (s.buildSummary (results), j, r);
}
CodecStatus Wire::queryRequest (std::string_view json, MeasurementQuery& out) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return CodecStatus::FloatingPointEnvironment;
    if (json.size() > kCommandJsonBytes || ! detail::validUtf8 (json)) return CodecStatus::Invalid;
    Storage storage; MeasurementQuery q;
    Reader reader { json, storage, 0, true, {}, 0, 0, false };
    reader.value (q); reader.space();
    if (! reader.good || reader.pos != json.size()) return CodecStatus::Invalid;
    out = q; return CodecStatus::Ok;
}
Checked Wire::queryStorage (const Session& s, std::string_view json) noexcept
{
    MeasurementQuery q;
    const auto parsed = queryRequest (json, q);
    if (parsed != CodecStatus::Ok) return { parsed == CodecStatus::FloatingPointEnvironment ? Rejection::FloatingPointEnvironment : Rejection::Contract };
    const auto demand = s.queryStorage (q);
    if (demand.status == QueryStatus::FloatingPointEnvironment) return { Rejection::FloatingPointEnvironment };
    if (demand.status == QueryStatus::Contract) return { Rejection::Contract };
    Checked out; out.bytes = demand.bytes; out.largestBlockBytes = demand.largestBlockBytes; return out;
}
TransferNeed Wire::queryBuffers (const Session& s, std::string_view json) noexcept
{
    MeasurementQuery q;
    if (const auto status = queryRequest (json, q); status != CodecStatus::Ok) return { status, 0, 0 };
    const auto demand = s.queryStorage (q);
    // A master's report is its whole record in JSON, not a row of columns: the bound is the answer's own scalars and
    // that record as the wire writes it.
    std::uint64_t record = 0;
    if (q.kind == QueryKind::MasterReport && demand.status == QueryStatus::Ready)
        for (const Kept& master : s.masters())
            if (master.id == q.masterId) { Writer w; w.binaryRows = true; w.value (master); record = w.good ? w.size : 0; }
    if (record > 4294967295ull - kQueryJsonBytes) return { CodecStatus::Invalid, 0, 0 };
    return { CodecStatus::Ok, std::uint32_t (kQueryJsonBytes + record), std::uint32_t (demand.rowBytes) };
}
TransferNeed Wire::queryBytes (const QueryView& response) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return { CodecStatus::FloatingPointEnvironment, 0, 0 };
    // A MasterReport answer is a record and no rows of its own; every other answer is rows and no record.
    if (response.request.kind == QueryKind::MasterReport)
    {
        if (! response.values.empty() || response.stride != 0 || response.stored != 0
            || response.master.has_value() != (response.status == QueryStatus::Ready)) return { CodecStatus::Invalid, 0, 0 };
    }
    else if (response.master || response.values.size() > kQueryValues || response.stride == 0 || response.values.size() % response.stride != 0
        || response.stored != response.values.size() / response.stride) return { CodecStatus::Invalid, 0, 0 };
    Writer w; w.binaryRows = true; w.value (response); return need (w);
}
CodecStatus Wire::query (const QueryView& response, std::span<char> json, std::span<double> rows) noexcept
{
    const auto status = buffers (queryBytes (response), json, rows);
    if (status != CodecStatus::Ok) return status;
    Writer w; w.output = json.data(); w.binaryRows = true; w.rows = rows.data(); w.value (response);
    return CodecStatus::Ok;
}
TransferNeed Wire::query (Session& s, std::string_view request, std::span<char> json, std::span<double> rows) noexcept
{
    const auto status = buffers (queryBuffers (s, request), json, rows);
    if (status != CodecStatus::Ok) return { status, 0, 0 };
    MeasurementQuery q;
    if (const auto parsed = queryRequest (request, q); parsed != CodecStatus::Ok) return { parsed, 0, 0 };
    const auto response = s.query (q);
    const auto n = queryBytes (response.view());
    if (n.status != CodecStatus::Ok) return n;
    if (! response.view().master && n.jsonBytes > kQueryJsonBytes) detail::storageOverflow();
    return { query (response.view(), json, rows), n.jsonBytes, n.rowBytes };
}
Checked Wire::masterWaveformChunkStorage (const Session& s, std::string_view json, const Pcm& shape) noexcept
{
    MeasurementQuery q;
    const auto parsed = queryRequest (json, q);
    if (parsed != CodecStatus::Ok) return { parsed == CodecStatus::FloatingPointEnvironment
        ? Rejection::FloatingPointEnvironment : Rejection::Contract };
    const auto demand = s.masterWaveformChunkStorage (q, shape);
    if (demand.status == QueryStatus::FloatingPointEnvironment) return { Rejection::FloatingPointEnvironment };
    if (demand.status == QueryStatus::Contract) return { Rejection::Contract };
    Checked out; out.bytes = demand.bytes; out.largestBlockBytes = demand.largestBlockBytes; return out;
}
TransferNeed Wire::masterWaveformChunkBuffers (const Session& s, std::string_view json, const Pcm& shape) noexcept
{
    MeasurementQuery q;
    if (const auto status = queryRequest (json, q); status != CodecStatus::Ok) return { status, 0, 0 };
    const auto demand = s.masterWaveformChunkStorage (q, shape);
    return { CodecStatus::Ok, kQueryJsonBytes, std::uint32_t (demand.rowBytes) };
}
TransferNeed Wire::masterWaveformChunk (Session& s, std::string_view request, const Pcm& chunk,
                                       std::span<char> json, std::span<double> rows) noexcept
{
    const auto status = buffers (masterWaveformChunkBuffers (s, request, chunk), json, rows);
    if (status != CodecStatus::Ok) return { status, 0, 0 };
    MeasurementQuery q;
    if (const auto parsed = queryRequest (request, q); parsed != CodecStatus::Ok) return { parsed, 0, 0 };
    const auto response = s.masterWaveformChunk (q, chunk);
    const auto n = queryBytes (response.view());
    if (n.status != CodecStatus::Ok) return n;
    if (n.jsonBytes > kQueryJsonBytes) detail::storageOverflow();
    return { query (response.view(), json, rows), n.jsonBytes, n.rowBytes };
}
TransferNeed Wire::eventsBytes (std::span<const Notification> events) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return { CodecStatus::FloatingPointEnvironment, 0, 0 };
    Writer w; w.binaryRows = true; eventList (w, events); return need (w);
}
CodecStatus Wire::events (std::span<const Notification> e, std::span<char> j, std::span<double> r) noexcept
{
    const auto st = buffers (eventsBytes (e), j, r); if (st != CodecStatus::Ok) return st;
    Writer w; w.output = j.data(); w.binaryRows = true; w.rows = r.data(); eventList (w, e); return CodecStatus::Ok;
}
namespace
{
template <class F> auto parseCommand (std::string_view json, F&& finish) noexcept
{
    Problem p; Object root (json, p); std::string_view kind; CommandId id = 0;
    root.get ("kind", kind); root.get ("commandId", id);
    Request request = command::Master { id };
    if (kind == "setTarget")
    {
        command::SetTarget r { id, {} };
        root.get ("target", r.target);
        request = r;
    }
    else if (kind == "setManual") { command::SetManual r { id }; root.get ("on", r.on); request = r; }
    else if (kind == "continueMeasurement") request = command::ContinueMeasurement { id };
    else if (kind == "adoptMachine") request = command::AdoptMachine { id };
    else if (kind == "cancel") { command::Cancel r { id }; root.get ("jobId", r.job); request = r; }
    else if (kind == "forget") { command::Forget r { id }; root.get ("masterId", r.master); request = r; }
    else if (kind == "editTarget" || kind == "editDevice" || kind == "revertEdits")
    {
        Object fields (root.take ("fields"), p);
        if (kind == "editTarget")
        {
            // A number sets the field; null clears it (the target row's number again); an absent key leaves it.
            command::EditTarget r { id };
            const auto field = [&] (std::string_view key, std::optional<double>& value, bool& clear)
            {
                const auto input = fields.take (key, false);
                if (input.empty()) return;
                Reader r { input, fields.storage, 0, true, {}, 0, 0, false };
                if (r.literal ("null")) clear = true;
                else r.value (value);
                r.space();
                if (! r.good || r.pos != input.size()) p.set ("invalid", key);
            };
            field ("lufs", r.fields.lufs, r.clear.lufs); field ("tp", r.fields.tp, r.clear.tp);
            request = r;
        }
        else
        {
            unsigned device = 0; root.get ("device", device);
            const auto read = [&] (auto& v)
            {
                using Of = detail::DeviceOf<std::remove_cvref_t<decltype (v)>>;
                Of::each (detail::rules(), [&] (std::uint8_t i, const detail::FieldRule&, auto& x)
                { fields.get (Of::fields[i], x, false); }, v);
            };
            if (kind == "editDevice")
            {
                command::EditDevice r { id }; if (! deviceFields (device, r.fields, read)) p.set ("invalid", "device"); request = r;
            }
            else
            {
                command::RevertEdits r { id }; if (! deviceFields (device, r.fields, read)) p.set ("invalid", "device"); request = r;
            }
        }
        fields.finish();
    }
    else if (kind != "master") p.set ("invalid", "kind");
    root.finish(); return finish (id, request, p);
}
template <class F> auto parseLoad (CommandId id, const Pcm& pcm, std::string_view meta, F&& finish) noexcept
{
    Problem p; Object root (meta, p); SourceMeta m;
    root.get ("name", m.name); root.get ("fileRate", m.fileRate); root.get ("bitDepth", m.bitDepth); root.get ("rateKnown", m.rateKnown);
    root.finish(); return finish (id, Request (command::Load { id, pcm, m }), p);
}
CodecStatus writeCommand (Session& session, CommandId id, const Request& request, const Problem& p,
                          std::span<char> output, std::uint32_t& written) noexcept
{
    Writer w; w.output = output.data();
    if (! p.reason.empty())
    {
        const auto refused = session.rejectProtocol (id);
        problemAnswer (w, refused.command, refused.revision, p);
    }
    else answer (w, session.apply (request), request);
    written = std::uint32_t (w.size); return CodecStatus::Ok;
}
Checked commandStorage (const Session& session, const Request& request, const Problem& p) noexcept
{
    return p.reason.empty() ? session.storageFor (request) : Checked { Rejection::Contract };
}
}
Checked Wire::commandStorage (const Session& session, std::string_view json) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return { Rejection::FloatingPointEnvironment };
    return parseCommand (json, [&] (CommandId, const Request& request, const Problem& p)
    { return felitronics::session::commandStorage (session, request, p); });
}
Checked Wire::loadStorage (const Session& session, std::uint32_t channels, std::uint64_t frames,
                           std::uint32_t rate, std::string_view meta) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return { Rejection::FloatingPointEnvironment };
    return parseLoad (0, { nullptr, channels, frames, rate }, meta, [&] (CommandId, const Request& request, const Problem& p)
    { return felitronics::session::commandStorage (session, request, p); });
}
namespace
{
template <class F> auto measured (std::string_view json, F&& finish) noexcept
{
    char text[kCommandJsonBytes] {};
    Storage storage; storage.text = text;
    MeasuredSource facts;
    bool valid = json.size() <= kCommandJsonBytes && detail::validUtf8 (json);
    if (valid)
    {
        Reader reader { json, storage, 0, true, {}, 0, 0, false };
        reader.value (facts); reader.space();
        valid = reader.good && reader.pos == json.size() && storage.chars <= sizeof (text);
    }
    return finish (facts, valid);
}
}
Checked Wire::loadMeasuredStorage (const Session& session, std::string_view facts) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return { Rejection::FloatingPointEnvironment };
    return measured (facts, [&] (const MeasuredSource& value, bool valid)
    { return valid ? session.loadMeasuredStorage (value) : Checked { Rejection::Contract }; });
}
Checked Wire::attachAudioStorage (const Session& session, const Pcm& pcm) noexcept
{ return session.attachAudioStorage (pcm); }
CodecStatus Wire::loadMeasured (Session& session, CommandId id, std::string_view facts,
                                std::span<char> output, std::uint32_t& written) noexcept
{
    if (output.size() < kAnswerBytes) return CodecStatus::TooSmall;
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return CodecStatus::FloatingPointEnvironment;
    return measured (facts, [&] (const MeasuredSource& value, bool valid)
    {
        Writer w; w.output = output.data();
        answer (w, valid ? session.loadMeasured (id, value) : session.rejectProtocol (id), command::Load { id });
        written = std::uint32_t (w.size); return CodecStatus::Ok;
    });
}
CodecStatus Wire::attachAudio (Session& session, CommandId id, const Pcm& pcm,
                               std::span<char> output, std::uint32_t& written) noexcept
{
    if (output.size() < kAnswerBytes) return CodecStatus::TooSmall;
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return CodecStatus::FloatingPointEnvironment;
    Writer w; w.output = output.data(); answer (w, session.attachAudio (id, pcm), command::Load { id, pcm });
    written = std::uint32_t (w.size); return CodecStatus::Ok;
}
Checked Wire::importStorage (const Session& session, std::string_view project) noexcept
{
    return session.storageFor (command::ImportProject { 0, project });
}
CodecStatus Wire::command (Session& session, std::string_view json, std::span<char> output, std::uint32_t& written) noexcept
{
    if (output.size() < kAnswerBytes) return CodecStatus::TooSmall;
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return CodecStatus::FloatingPointEnvironment;
    return parseCommand (json, [&] (CommandId id, const Request& request, const Problem& p)
    { return writeCommand (session, id, request, p, output, written); });
}
CodecStatus Wire::master (Session& session, const command::Master& request,
                          std::span<char> output, std::uint32_t& written) noexcept
{
    if (output.size() < kAnswerBytes) return CodecStatus::TooSmall;
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return CodecStatus::FloatingPointEnvironment;
    Writer w; w.output = output.data();
    answer (w, session.apply (request), request);
    written = std::uint32_t (w.size);
    return CodecStatus::Ok;
}
CodecStatus Wire::load (Session& session, CommandId id, const Pcm& pcm, std::string_view meta,
                        std::span<char> output, std::uint32_t& written) noexcept
{
    if (output.size() < kAnswerBytes) return CodecStatus::TooSmall;
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return CodecStatus::FloatingPointEnvironment;
    return parseLoad (id, pcm, meta, [&] (CommandId command, const Request& request, const Problem& p)
    { return writeCommand (session, command, request, p, output, written); });
}
CodecStatus Wire::importProject (Session& session, CommandId id, std::string_view project,
                                 std::span<char> output, std::uint32_t& written) noexcept
{
    if (output.size() < kAnswerBytes) return CodecStatus::TooSmall;
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return CodecStatus::FloatingPointEnvironment;
    Writer w; w.output = output.data();
    answer (w, session.importProject (id, project), command::ImportProject { id, project });
    written = std::uint32_t (w.size); return CodecStatus::Ok;
}
}
