// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "../../../tests/DeclaredBudget.h"
#include "Advance.h"
#include "Devices.h"
#include "FpEnvironmentControl.h"
#include "Grid.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/toml/Toml.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <charconv>
#include <initializer_list>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace budget = felitronics::session::testing;
namespace declared = felitronics::declared;
namespace
{
struct Audio
{
    float samples[4] { 0.0f, 0.25f, -0.25f, 0.0f };
    const float* planes[2] { samples, samples };
    command::Load load() const { return { 1, { planes, 2, 4, 48000 }, { "project.wav", 48000, true, 24 } }; }
};
std::string version (Version v)
{
    return std::to_string (v.major) + '.' + std::to_string (v.minor) + '.' + std::to_string (v.patch);
}
std::string header (bool manual = true)
{
    return "defaults = \"" + std::string (*detail::rules().engine.find ("defaults").string()) + "\"\nmanual = "
         + (manual ? "true\n" : "false\n");
}
std::string project (bool manual = true)
{
    return header (manual) + "\n[target]\nname = \"allStreaming\"\n";
}
std::unique_ptr<Session> fresh (unsigned units = 10)
{
    auto made = Session::create();
    ok (made.status == Status::Ok, "create succeeds");
    // On allStreaming, which every file here names: a session starts on the config's default, Clean (`maxClean`).
    (void) made.session->apply (command::SetTarget { 1, "allStreaming" });
    Audio audio;
    ok (made.session->apply (audio.load()).rejection == Rejection::None, "load succeeds");
    if (units >= 5) budget::measure (*made.session, units < 10); else (void) made.session->step (units);
    return std::move (made.session);
}
std::string json (const SnapshotView& v)
{
    const auto need = Codec::encodedBytes (v);
    std::string out (std::size_t (need.bytes), '\0');
    ok (Codec::encode (v, out) == CodecStatus::Ok, "snapshot encodes");
    return out;
}
std::string exported (Session& s)
{
    Checked need;
    auto spent = declared::spend ([&] { need = s.exportProjectBytes(); });
    ok (spent.bytes == 0 && need.rejection == Rejection::None, "export demand allocates nothing");
    ProjectText out;
    spent = declared::spend ([&] { out = s.exportProject(); });
    ok (out.rejection == Rejection::None && need.bytes == std::uint64_t (spent.bytes)
        && out.size == need.bytes, "export allocates exactly its declared bytes, without a terminator");
    return std::string (out.view());
}
Answer import (Session& s, const std::string& input, const char* evidence = nullptr)
{
    Checked need;
    const command::ImportProject request { 927, input };
    auto spent = declared::spend ([&] { need = s.check (request); });
    ok (spent.bytes == 0, "import preflight allocates nothing");
    Answer answer;
    spent = declared::spend ([&] { answer = s.importProject (request.id, request.bytes); });
    ok (answer.command == request.id && declared::covers (need.bytes, spent), declared::describe (need.bytes, spent));
    if (spent.bytes > 0) ok (! declared::covers (std::uint64_t (spent.bytes - 1), spent), "under-declared import demand turns the harness red");
    if (evidence)
        std::printf ("import budget %s: text=%zu declared=%llu actual=%lld ratio=%.4f\n", evidence, input.size(),
                     static_cast<unsigned long long> (need.bytes), spent.bytes, double (need.bytes) / double (spent.bytes));
    return answer;
}
void refused (Session& s, const std::string& input, Rejection code, std::string_view marker = {})
{
    const auto before = json (s.snapshot().view());
    const auto answer = import (s, input);
    ok (answer.rejection == code, "import refuses with code " + std::to_string (unsigned (code))
        + ", received " + std::to_string (unsigned (answer.rejection)));
    ok (before == json (s.snapshot().view()), "refusal preserves every snapshot field and the revision");
    ok (s.events().size() == 1 && s.events()[0].kind == EventKind::Rejected
        && s.events()[0].payload.rejected.commandId == 927 && s.events()[0].payload.rejected.code == code,
        "refusal publishes the command identity and code");
    if (! marker.empty())
    {
        const auto at = input.find (marker);
        const auto line = 1u + unsigned (std::count (input.begin(), input.begin() + std::ptrdiff_t (at), '\n'));
        const auto start = input.rfind ('\n', at);
        const auto column = unsigned (at - (start == std::string::npos ? 0 : start + 1)) + 1;
        ok (answer.position.line == line && answer.position.column == column, "refusal points at the exact value or key");
    }
    else if (code != Rejection::ProjectTooLarge && code != Rejection::NoSource && code != Rejection::NotPlaced
             && code != Rejection::FloatingPointEnvironment)
        ok (answer.position.line != 0 && answer.position.column != 0, "document refusal names a position");
    const auto fact = text::Text::rejected (answer, command::ImportProject { 927, input });
    for (const auto lang : { text::Lang::Ru, text::Lang::En })
    {
        const auto rendered = fact ? text::Text::text (*fact, lang) : std::string {};
        ok (! rendered.empty() && rendered.find ('{') == std::string::npos, "import refusal renders in both catalog languages");
    }
}
void sparseSections()
{
    auto s = fresh();
    const auto minimal = project (false);
    ok (exported (*s) == minimal, "untouched defaults export only metadata and target");
    std::string empty = minimal;
    detail::eachDevice (s->project().devices, [&] (Device, const auto& layers)
    {
        using Of = detail::DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
        empty += "\n[" + std::string (Of::name) + "]\n";
    });
    ok (import (*s, empty).rejection == Rejection::None && exported (*s) == minimal,
        "empty device sections remain accepted and canonicalize away");
    ok (import (*s, minimal).rejection == Rejection::None && exported (*s) == minimal,
        "absent sections round trip byte-identically");
    const auto hand = project() + "\n[hpf]\nfq.hand = 32\n";
    ok (import (*s, hand).rejection == Rejection::None && exported (*s) == hand,
        "an equal touched hand alone retains exactly its device section");
    const auto machine = project (false) + "\n[hpf]\nfq.machine = 36\n";
    ok (import (*s, machine).rejection == Rejection::None && exported (*s) == machine,
        "a machine difference alone retains exactly its device section");
}
std::string withDefaults (std::string input, std::string_view label)
{
    input.replace (0, input.find ('\n'), "defaults = \"" + std::string (label) + "\"");
    return input;
}
void defaultsVersions()
{
    ok (*detail::rules().engine.find ("defaults").string() == "2026-10", "the current defaults are compiled");
    auto s = fresh();
    const auto current = project() + "\n[hpf]\nfq.hand = 36\n";
    ok (import (*s, current).rejection == Rejection::None && s->events().empty(),
        "the carried defaults version is accepted as it is");
    // THE CURRENT LABEL IS THE ONLY ONE THAT OPENS (owner, 30.09), with the file's machine layer as the file's and its
    // differences shown beside it.
    ok (import (*s, withDefaults (project (true) + "\n[hpf]\nfq.machine = 37\n", "2026-10")).rejection == Rejection::None
        && s->snapshot().view().plan.fromFile && detail::same (s->project().devices.hpf.machine.fq, 37.0)
        && s->events().size() == 1 && s->events()[0].payload.fact.view().id == text::FactId::MachineDifferences,
        "2026-10: the file's machine cutoff of 37 Hz is kept, as the file's, with its comparison");
    // Any other label is refused whole, with the existing reasons, and the open project stays as it was: nothing converts,
    // nothing re-places a file's machine layer on import. 2026-09 (saved before the core had its planner) is one of them.
    const auto open = exported (*s);
    const auto revision = s->revision();
    for (const auto older : { "2026-09", "2020-01", "2025-12", "2026-01" })
        for (const auto& body : { project (true) + "lufs.hand = -12.5\n\n[hpf]\nfq.machine = 37\nfq.hand = 40\n",
                                  project() + "\n[hpf]\nfq.hand = 36\n", project (false) })
        {
            const auto file = withDefaults (body, older);
            refused (*s, file, Rejection::UnknownDefaults, std::string ("\"") + older + '"');
            ok (s->revision() == revision && exported (*s) == open,
                std::string (older) + ": the revision and the open project are unchanged");
            const auto answer = import (*s, file);
            const auto fact = text::Text::rejected (answer, command::ImportProject { 927, file });
            ok (fact && fact->id == text::FactId::RejectedUnknownDefaults, std::string (older) + ": the refusal's fact is RejectedUnknownDefaults");
        }
    for (const auto newer : { "2026-11", "2027-01", "9999-12" })
        refused (*s, withDefaults (project(), newer), Rejection::NewerDefaults, std::string ("\"") + newer + '"');
    for (const auto malformed : { "2026-00", "2026-13", "2026-9", "026-09", "20260-09", "2026/09", "2026-09x", " 2026-09", "2026-0a", "" })
        refused (*s, withDefaults (project(), malformed), Rejection::UnknownDefaults,
            std::string ("\"") + malformed + '"');
    ok (s->revision() == revision && exported (*s) == open, "no refusal moved the revision or the open project");
}
void roundTrip()
{
    auto s = fresh();
    (void) s->apply (command::SetTarget { 2, "lp" });
    (void) s->apply (command::SetManual { 3, true });
    (void) s->apply (command::EditTarget { 4, { -12.5, -0.5 } });
    // Every typed field and optional alternative participates, including a hand equal to its machine.
    detail::eachDevice (s->project().devices, [&] (Device device, const auto& layers)
    {
        using Of = detail::DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
        if (! detail::offered (detail::rules(), s->project().target, 2, device)) return;
        auto hand = layers.hand;
        Of::each (detail::rules(), [] (std::uint8_t, const detail::FieldRule&, auto& h, const auto& m) { h = m; }, hand, layers.machine);
        ok (s->apply (command::EditDevice { 5, hand }).rejection == Rejection::None, "every device's hand can equal the machine");
    });
    const auto saved = exported (*s);
    ok (saved.starts_with (header()), "defaults and stamped core version are first, with manual mode");
    ok (saved.find (".machine") == std::string::npos && saved.find ("fq.hand = 32\n") != std::string::npos,
        "defaults are omitted; an equal hand remains owned");
    ok (saved.find ("[hpf]\n") != std::string::npos && saved.find ("[low]\n") != std::string::npos,
        "section names are the config's names");
    auto restored = fresh();
    const auto before = restored->revision();
    ok (import (*restored, saved, "realistic").rejection == Rejection::None && restored->revision() == before + 1, "import commits once");
    ok (exported (*restored) == saved, "export import export is byte-identical");
    auto dither = fresh();
    (void) dither->apply (command::SetTarget { 2, "cd" });
    (void) dither->apply (command::SetManual { 3, true });
    DitherFields<Touched> ditherHand; ditherHand.on = true;
    ok (dither->apply (command::EditDevice { 4, ditherHand }).rejection == Rejection::None, "dither hand on a 16-bit target");
    auto ditherCopy = fresh();
    const auto ditherText = exported (*dither);
    ok (import (*ditherCopy, ditherText).rejection == Rejection::None && exported (*ditherCopy) == ditherText,
        "dither's hand round trips too");
    (void) dither->apply (command::SetTarget { 6, "allStreaming" });
    const auto reset = exported (*dither);
    ok (! dither->project().devices.dither.machine.on && ! dither->project().devices.dither.hand.on
        && import (*ditherCopy, reset).rejection == Rejection::None && exported (*ditherCopy) == reset,
        "target change clears dither hand and the reset project round trips");
    ok (ditherCopy->apply (command::EditDevice { 7, ditherHand }).rejection == Rejection::NotOffered,
        "dither edits still require delivery at 16 bits or below");
    (void) ditherCopy->apply (command::SetTarget { 8, "cd" });
    ok (! ditherCopy->project().devices.dither.hand.on && ditherCopy->project().devices.dither.machine.on,
        "returning to CD uses the machine's decision with no device edit");

    auto a = s->snapshot(), b = restored->snapshot();
    SnapshotView av = a.view(), bv = b.view(); av.revision = bv.revision;
    ok (! av.plan.fromFile && bv.plan.fromFile && av.plan.key == bv.plan.key, "the restored machine layer is the file's, on the same plan");
    av.plan.fromFile = true;
    ok (json (av) == json (bv), "both complete project layers and measured state survive round trip");

    const auto inlineFile = "manual = true\ndefaults = \""
        + std::string (*detail::rules().engine.find ("defaults").string()) + "\"\n"
        "saturation = { mix = { hand = 0.50 } }\n"
        "tilt = { db = { hand = -0.0 } }\n"
        "hpf = { fq = { hand = 36 }, on = { hand = false } }\n"
        "target = { tp = { hand = -1 }, name = \"allStreaming\", lufs = { hand = -12.50 } }\n";
    ok (import (*restored, inlineFile).rejection == Rejection::None, "hand-written inline tables in another order import");
    auto expected = fresh();
    (void) expected->apply (command::SetManual { 1, true });
    (void) expected->apply (command::EditTarget { 2, { -12.5, -1.0 } });
    HpfFields<Touched> hpf; hpf.on = false; hpf.fq = 36;
    SaturationFields<Touched> sat; sat.mix = 0.5;
    (void) expected->apply (command::EditDevice { 3, hpf });
    (void) expected->apply (command::EditDevice { 4, sat });
    TiltFields<Touched> tilt; tilt.db = -0.0;
    (void) expected->apply (command::EditDevice { 5, tilt });
    ok (exported (*restored) == exported (*expected), "inline spelling and commands materialize the same project");
    ok (exported (*restored).find ("db.hand = 0\n") != std::string::npos, "negative zero prints as zero");
    ok (import (*restored, project (false)).rejection == Rejection::None && ! restored->project().manual,
        "manual false imports without depending on the current manual mode");
}
void domainsAndExactNumbers()
{
    auto s = fresh();
    struct Row { const char* section; const char* field; const char* accepted; const char* refused; };
    const Row rows[] {
        { "target", "lufs", "99", nullptr }, { "target", "tp", "-5.5", "-6.01" },
        { "hpf", "fq", "20000.25", "24000" }, { "hpf", "slope", "96", "102" },
        { "monoBass", "fq", "120.25", "300.1" }, { "monoBass", "width", "0.333333333", "1.01" },
        { "glue", "upToDb", "5.25", "6.01" }, { "saturation", "drive", "1.25", "12.01" },
        { "saturation", "mix", "0.123456789", "-0.01" },
        { "tilt", "db", "5.25", "6.01" }, { "low", "db", "-5.25", "-6.01" },
        { "limiter", "needlesDb", "5.25", "6.01" }
    };
    for (const auto& row : rows)
    {
        const std::string base = header() + "\n[target]\nname = \"lp\"\n";
        const auto input = [&] (const char* value) { return base + "\n[" + row.section + '.' + row.field + "]\nhand = " + value + '\n'; };
        ok (import (*s, input (row.accepted)).rejection == Rejection::None,
            std::string (row.section) + '.' + row.field + " imports within domain, independent of slider");
        const auto saved = exported (*s);
        ok (import (*s, saved).rejection == Rejection::None && exported (*s) == saved, "domain project round-trips");
        if (row.refused)
            refused (*s, input (row.refused), std::string_view (row.field) == "slope" ? Rejection::NotOneOf : Rejection::OutOfDomain, row.refused);
    }
    for (double value : { 1.0 / 3.0, -1.2345678901234567, std::numeric_limits<double>::max(),
                          std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min() })
    {
        TargetFields<Touched> fields; fields.lufs = value;
        ok (s->apply (command::EditTarget { 1, fields }).rejection == Rejection::None, "finite LUFS accepted");
        const auto saved = exported (*s);
        ok (import (*s, saved).rejection == Rejection::None
            && std::bit_cast<std::uint64_t> (*s->project().targetEdit.lufs) == std::bit_cast<std::uint64_t> (value),
            "full binary64 project value round-trips without quantization");
        ok (exported (*s) == saved, "extended numeric spelling is canonical");
    }
    for (const auto text : { "\"1e\"", "\"NaN\"", "\"1.2.3\"", "\"1e9999\"" })
        refused (*s, project() + "\n[target.lufs]\nhand = " + text + '\n', Rejection::ProjectType, text);
    Audio audio; auto load = audio.load(); load.pcm.sampleRate = 8000;
    ok (s->apply (load).rejection == Rejection::None, "different source rate loaded"); budget::measure (*s);
    refused (*s, project() + "\n[hpf]\nfq.hand = 4000\n", Rejection::OutOfDomain, "4000");
    ok (import (*s, project() + "\n[hpf]\nfq.hand = 3999.5\n").rejection == Rejection::None, "import uses current source Nyquist");
}

void refusals()
{
    auto s = fresh();
    (void) import (*s, project (true) + "\n[hpf]\nfq.machine = 36\nfq.hand = 40\n");
    (void) s->apply (command::Master { 19 });
    (void) s->step (1);
    refused (*s, project() + "\n[unknown]\n", Rejection::ProjectUnknownKey, "unknown");
    refused (*s, project() + "\n[hpf]\nunknown.hand = 30\n", Rejection::ProjectUnknownKey, "unknown");
    refused (*s, project() + "\n[hpf]\nfq.robot = 30\n", Rejection::ProjectUnknownKey, "robot");
    refused (*s, project() + "\n[hpf]\nfq.hand = \"bad\"\n", Rejection::ProjectType, "\"bad\"");
    refused (*s, project() + "\n[hpf]\nfq = 30\n", Rejection::ProjectType, "30");
    refused (*s, project() + "\n[hpf]\nfq.hand = 24000\n", Rejection::OutOfDomain, "24000");
    ok (s->importProject (1, project() + "\n[hpf]\nfq.hand = 32.5\n").rejection == Rejection::None, "import accepts a value between slider steps");
    refused (*s, project() + "\n[hpf]\nslope.hand = 11\n", Rejection::NotOneOf, "11");
    refused (*s, project() + "\n[limiter]\nneedles.hand = \"other\"\n", Rejection::NotOneOf, "\"other\"");
    refused (*s, header() + "target = { name = \"unknown\" }\n", Rejection::UnknownTarget, "\"unknown\"");
    auto old = project(); old.replace (old.find ("defaults = "), old.find ('\n'), "defaults = \"missing\"");
    refused (*s, old, Rejection::UnknownDefaults, "\"missing\"");
    refused (*s, "core = \"0.6.0\"\n" + project(), Rejection::ProjectUnknownKey, "core");
    refused (*s, project() + "\n[saturation]\noutput.hand = -1\n", Rejection::ProjectUnknownKey, "output");
    ok (import (*s, project() + "\n[hpf]\nfq.machine = 36\n").rejection == Rejection::None, "saved machine and hidden hand are accepted");
    refused (*s, header() + "[target]\n", Rejection::ProjectMissing);
    refused (*s, project() + "\n[hpf\n", Rejection::ProjectSyntax);
    refused (*s, project() + "\n[hpf]\nfq.hand = 36\nfq.hand = 38\n", Rejection::ProjectSyntax);
    refused (*s, std::string (felitronics::toml::kMaxDocument + 1, 'x'), Rejection::ProjectSyntax);
    ok (import (*s, project (false) + "\n[hpf]\nfq.hand = 36\n").rejection == Rejection::None, "saved machine and hidden hand are accepted");
    ok (import (*s, project() + "\n[low]\ndb.hand = 1\n").rejection == Rejection::None, "saved machine and hidden hand are accepted");
    refused (*s, project() + "\n[target.lufs]\nmachine = -12\n", Rejection::ProjectUnknownKey, "machine");
    ok (import (*s, project (false) + "\n[hpf]\nfq.machine = 36\n\n[tilt]\ndb.hand = 1\n").rejection == Rejection::None, "saved machine and hidden hand are accepted");
    auto limit = project(); limit += '#' + std::string (felitronics::toml::kMaxDocument - limit.size() - 1, 'x');
    ok (import (*s, limit).rejection == Rejection::None, "a project at the library text limit is accepted beyond the former 16 KiB cap");
    // Positions count Unicode characters; the prefix comment has a multibyte value.
    refused (*s, project() + "\n[hpf]\n\"fq\" = { hand = \"\xC3\xA9\", nope = 1 }\n", Rejection::ProjectType, "\"\xC3\xA9\"");
}
void filesMachine()
{
    auto s = fresh();
    const auto input = project (true) + "\n[hpf]\nfq.machine = 36\nfq.hand = 36\n"
        "\n[limiter]\nneedles.machine = \"manual\"\n";
    ok (import (*s, input).rejection == Rejection::None, "the file's complete machine layer is taken");
    const auto saved = exported (*s);
    ok (saved.find ("core") == std::string::npos && saved.find ("fq.machine = 36\nfq.hand = 36\n") != std::string::npos,
        "author ordering survives export, and the file carries no core stamp");
    auto held = s->snapshot();
    ok (held.view().machineDifferences.size() == 2, "snapshot exposes every machine difference");
    const auto first = held.view().machineDifferences[0];
    ok (first.device == Device::Hpf && first.field == 1 && detail::same (first.fileValue, 36)
        && detail::same (first.coreValue, s->project().devices.hpf.machine.fq) == false, "difference names device, field, file and core values");
    ok (s->events().size() == 1 && s->events()[0].kind == EventKind::Fact, "an import with differences publishes its fact immediately");
    const auto fact = s->events()[0].payload.fact.view();
    ok (fact.id == text::FactId::MachineDifferences && fact.args[0].integer == 2, "fact carries the difference count");
    for (const auto lang : { text::Lang::Ru, text::Lang::En })
    {
        const auto rendered = text::Text::text (fact, lang);
        ok (rendered.find ('2') != std::string::npos && rendered.find ('{') == std::string::npos, "difference count renders in both languages");
    }
    const auto single = text::Fact::of (text::FactId::MachineDifferences, text::Arg::count (1));
    ok (text::Text::text (single, text::Lang::En).find ("1 place.") != std::string::npos
        && text::Text::text (single, text::Lang::Ru).find ("1 месте.") != std::string::npos,
        "the new count fact agrees with one in both languages");
    const auto encoded = json (held.view());
    Snapshot decoded;
    const auto need = Codec::decodedBytes (encoded);
    CodecStatus status;
    const auto spent = declared::spend ([&] { status = Codec::decode (encoded, decoded); });
    ok (status == CodecStatus::Ok && need.bytes == std::uint64_t (spent.bytes) && json (decoded.view()) == encoded,
        "machine differences cross the same codec with exact owned storage");
    auto replay = fresh();
    ok (import (*replay, saved).rejection == Rejection::None && exported (*replay) == saved,
        "the file's machine layer remains replayable after exporting it");
    const auto job = s->apply (command::Master { 8 }).job;
    const auto recipe = s->jobRecipe();
    ok (import (*s, project()).rejection == Rejection::None && s->job() == job
        && detail::same (s->jobRecipe().project.devices.hpf.machine.fq, recipe.project.devices.hpf.machine.fq),
        "import during mastering leaves the captured recipe and job alone");
    ok (s->snapshot().view().machineDifferences.empty(), "a new import replaces the comparison");
    (void) import (*s, input);
    (void) s->apply (command::SetManual { 9, false });
    ok (s->snapshot().view().machineDifferences.size() == 2, "switching off manual leaves the saved machine layer");
    (void) s->apply (command::SetTarget { 10, "lp" });
    ok (s->snapshot().view().machineDifferences.empty(), "a new target places the current machine and clears the old comparison");
    s.reset();
    ok (json (held.view()) == encoded, "retained comparison survives commands and session destruction");
    ok (import (*replay, project (false)).rejection == Rejection::None && replay->snapshot().view().machineDifferences.empty()
        && std::none_of (replay->events().begin(), replay->events().end(), [] (const Notification& e) { return e.kind == EventKind::Fact; }),
        "a file with no differences publishes no fact");
}
void numbers()
{
    const auto rules = detail::rules();
    auto s = fresh();
    (void) s->apply (command::SetTarget { 2, "lp" });
    (void) s->apply (command::SetManual { 3, true });
    auto replay = fresh();
    Devices devices;
    std::size_t fields = 0;
    detail::eachDevice (devices, [&] (Device, const auto& layers)
    {
        using Of = detail::DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
        auto edits = layers.hand;
        Of::each (rules, [&] (std::uint8_t index, const detail::FieldRule& rule, auto& hand)
        {
            ++fields;
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype (hand)>, std::optional<double>>)
            {
                const auto& k = rule.knob;
                // A stepless knob (owner, 07.10: step 0) takes any decimal: its travel is walked in hundredths of its
                // places, off every grid it had.
                const bool stepless = k.step.mantissa == 0;
                const int scale = std::max ({ int (k.from.scale), int (k.to.scale), int (k.step.scale) }) + (stepless ? 2 : 0);
                std::int64_t from = 0, to = 0, step = 1;
                (void) detail::scaleUp (k.from.mantissa, scale - k.from.scale, from);
                (void) detail::scaleUp (k.to.mantissa, scale - k.to.scale, to);
                if (! stepless) (void) detail::scaleUp (k.step.mantissa, scale - k.step.scale, step);
                const auto count = (to - from) / step;
                for (const auto tick : { std::int64_t (0), std::int64_t (1), count / 3, count / 2, count })
                {
                    const double n = felitronics::toml::Decimal { from + tick * step, std::uint8_t (scale), false }.toDouble();
                    hand = n;
                    ok (s->apply (command::EditDevice { 4, edits }).rejection == Rejection::None, "knob grid value accepted");
                    const auto file = exported (*s);
                    char shortest[64]; const auto printed = std::to_chars (shortest, shortest + 64, n, std::chars_format::fixed);
                    const auto line = std::string (Of::fields[index]) + ".hand = " + std::string (shortest, printed.ptr) + '\n';
                    ok (file.find (line) != std::string::npos, "one shortest decimal rule covers every knob, including both travel ends");
                    ok (import (*replay, file).rejection == Rejection::None && exported (*replay) == file,
                        "every knob's printed decimals round trip exactly");
                }
                hand.reset();
            }
        }, edits);
    });
    ok (fields == kDeviceFields, "the fixed comparison capacity equals the shared device field walk");
}
void slicing()
{
    std::string reference;
    std::vector<unsigned> referenceFacts;
    for (const auto units : { 1u, 7u, 1000000u })
    {
        auto s = fresh (0);
        std::vector<unsigned> facts;
        for (;;)
        {
            const auto result = s->step (units);
            for (const auto& e : s->events()) if (e.kind == EventKind::Fact) facts.push_back (unsigned (e.payload.fact.view().id));
            if (result.state == StepState::Done) break;
        }
        const auto final = json (s->snapshot().view());
        if (reference.empty()) { reference = final; referenceFacts = facts; }
        ok (final == reference && facts == referenceFacts && facts.size() >= 8,
            "budgets 1, 7 and large yield identical ordered absence/completion facts and every final snapshot field");
    }
}
void demandsAndOrder()
{
    auto empty = Session::create();
    refused (*empty.session, std::string (felitronics::toml::kMaxDocument + 1, 'x'), Rejection::NoSource);
    auto loaded = fresh (0);
    refused (*loaded, std::string (felitronics::toml::kMaxDocument + 1, 'x'), Rejection::NotPlaced);
    ok (empty.session->exportProject().rejection == Rejection::NoSource
        && loaded->exportProject().rejection == Rejection::NotPlaced, "unplaced projects cannot masquerade as decisions");
    auto s = fresh();
    namespace fpenv = felitronics::session::testing;
    const auto env = fpenv::saveFpEnvironment();
    const auto input = std::string (felitronics::toml::kMaxDocument + 1, 'x');
    if (fpenv::setRounding (fpenv::kRoundUpward))
    {
        const auto checked = s->check (command::ImportProject { 1, input });
        const auto answer = s->importProject (1, input);
        const auto exported = s->exportProject();
        fpenv::restoreFpEnvironment (env);
        ok (checked.rejection == Rejection::FloatingPointEnvironment && checked.bytes == 0
            && answer.rejection == checked.rejection && exported.rejection == checked.rejection, "FP entry precedes size and serialization");
    }
    else fpenv::restoreFpEnvironment (env);
    // The session law measures the whole import, including schema refusals and owned results.
    // Parser/container allocation laws are tested by felitronics-toml.
    std::vector<std::string> hostile {
        "", header() + "[target]\n", project() + "[hpf]\nfq.hand = 24000\n",
        project() + "[limiter]\nneedles.hand = \"" + std::string (8000, 'a') + "\"\n",
        project() + "[hpf]\nfq.hand = \"bad\"\n", project() + "[hpf\n"
    };
    std::string largest = project();
    for (unsigned i = 0; i < 8192; ++i)
        largest += "[unknown" + std::to_string (i) + ".author]\nvalue = 1\n";
    largest += '#' + std::string (felitronics::toml::kMaxDocument - largest.size() - 1, 'x');
    hostile.push_back (largest);
    for (const auto& inputText : hostile)
    {
        const auto before = json (s->snapshot().view());
        ok (import (*s, inputText, inputText.size() == largest.size() ? "largest-adversarial" : nullptr).rejection
                != Rejection::None && json (s->snapshot().view()) == before,
            "hostile project import is covered by its declared demand and changes nothing");
    }
}
// Every object member of a JSON document, once per path (an array's elements share the first one's path): the bytes from
// its key to its value's end. The encoder writes no whitespace, so neither does this read any.
struct Member { std::string path; std::size_t from, to; };
std::size_t valueEnd (std::string_view d, std::size_t i, const std::string& path, std::vector<Member>& out, std::set<std::string>& seen)
{
    if (d[i] == '"')
    {
        for (++i; d[i] != '"'; ++i) if (d[i] == '\\') ++i;
        return i + 1;
    }
    if (d[i] == '[')
    {
        for (++i; d[i] != ']';) { i = valueEnd (d, i, path + "[]", out, seen); if (d[i] == ',') ++i; }
        return i + 1;
    }
    if (d[i] == '{')
    {
        for (++i; d[i] != '}';)
        {
            const auto from = i, colon = d.find ("\":", i + 1);
            const auto member = path + "." + std::string (d.substr (from + 1, colon - from - 1));
            i = valueEnd (d, colon + 2, member, out, seen);
            if (seen.insert (member).second) out.push_back ({ member, from, i });
            if (d[i] == ',') ++i;
        }
        return i + 1;
    }
    return d.find_first_of (",]}", i);
}
// Snapshots never persist (the page and the core ship together): a snapshot without any one of its keys is refused,
// a semantically optional one (null when absent) included.
void everySnapshotKeyRequired()
{
    auto s = fresh();
    (void) s->apply (command::SetTarget { 2, "lp" });
    const auto whole = json (s->snapshot().view());
    Snapshot out;
    ok (Codec::decode (whole, out) == CodecStatus::Ok, "PRECONDITION: the whole snapshot decodes");
    std::vector<Member> members;
    std::set<std::string> seen;
    (void) valueEnd (whole, 0, "", members, seen);
    std::string decoded;
    for (const auto& m : members)
    {
        auto cut = whole;
        if (cut[m.to] == ',') cut.erase (m.from, m.to + 1 - m.from); else cut.erase (m.from - 1, m.to - m.from + 1);
        if (Codec::decode (cut, out) == CodecStatus::Ok) decoded += " " + m.path;
    }
    ok (members.size() > 100 && decoded.empty(),
        "a snapshot without any one of its " + std::to_string (members.size()) + " keys is refused; decoded without:" + decoded);
}
}

// A PROJECT SAVED BEFORE (owner, 07.10: old projects open with machine values): v0.20.0 wrote no glue threshold, ratio,
// knee, attack or release and no limiter release, lookahead or oversampling; a test build of the site wrote the glue's five
// with its own input's numbers. Either opens with the machine's values for this source and no difference.
void savedBefore()
{
    auto s = fresh();
    const auto& own = s->project().devices;
    const std::string v020 = project (false) + "\n[glue]\nmix.machine = 0.4\n\n[limiter]\nneedles.machine = \"auto\"\n";
    const std::string ownFive = project (false) + "\n[glue]\nthresholdDb.machine = -20.5\nratio.machine = 1.9\nkneeDb.machine = 6\n"
        "attackMs.machine = 20\nreleaseMs.machine = 120\n";
    for (const auto& [file, name] : { std::pair { v020, "a v0.20.0 project" }, std::pair { ownFive, "a project with the glue's five of its own input" } })
    {
        auto replay = fresh();
        const auto answer = import (*replay, file);
        const auto v = replay->snapshot();
        const auto& g = replay->project().devices.glue.machine;
        const auto& l = replay->project().devices.limiter.machine;
        const auto& shown = v.view().project.devices.glue.machine;
        const auto& plan = v.view().plan.glue;
        ok (answer.rejection == Rejection::None && v.view().machineDifferences.empty()
                && detail::same (g.thresholdDb, own.glue.machine.thresholdDb) && detail::same (g.ratio, own.glue.machine.ratio)
                && detail::same (g.kneeDb, own.glue.machine.kneeDb) && detail::same (g.attackMs, own.glue.machine.attackMs)
                && detail::same (g.releaseMs, own.glue.machine.releaseMs)
                && detail::same (l.releaseMs, own.limiter.machine.releaseMs) && detail::same (l.lookaheadMs, own.limiter.machine.lookaheadMs)
                && l.oversampling == own.limiter.machine.oversampling
                && plan.ratio && detail::same (shown.ratio, *plan.ratio) && plan.kneeDb && detail::same (shown.kneeDb, *plan.kneeDb),
            std::string (name) + " opens with the machine's values and no difference ("
                + std::to_string (v.view().machineDifferences.size()) + " found)");
    }
}
int main()
{
    sparseSections(); defaultsVersions(); roundTrip(); domainsAndExactNumbers(); refusals(); filesMachine(); numbers(); slicing(); demandsAndOrder(); everySnapshotKeyRequired(); savedBefore();
    return felitronics::test::report();
}
