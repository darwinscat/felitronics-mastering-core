// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "DeclaredBudget.h"
#include "Advance.h"
#include "Devices.h"
#include "FpEnvironmentControl.h"
#include "Grid.h"
#include "SnapshotV1Fixture.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/toml/Toml.h>
#include <felitronics_test.h>
#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace felitronics::session;
using felitronics::test::ok;
namespace budget = felitronics::session::testing;
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
std::string header (bool manual = true, std::string_view core = {})
{
    return "defaults = \"" + std::string (*detail::rules().engine.find ("defaults").string()) + "\"\ncore = \""
         + (core.empty() ? version (Session::version()) : std::string (core)) + "\"\nmanual = " + (manual ? "true\n" : "false\n");
}
std::string project (bool manual = true, std::string_view core = {})
{
    return header (manual, core) + "\n[target]\nname = \"allStreaming\"\n";
}
std::unique_ptr<Session> fresh (unsigned units = 10)
{
    auto made = Session::create();
    ok (made.status == Status::Ok, "create succeeds");
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
    auto spent = budget::spend ([&] { need = s.exportProjectBytes(); });
    ok (spent.bytes == 0 && need.rejection == Rejection::None, "export demand allocates nothing");
    ProjectText out;
    spent = budget::spend ([&] { out = s.exportProject(); });
    ok (out.rejection == Rejection::None && need.bytes == std::uint64_t (spent.bytes)
        && out.size == need.bytes, "export allocates exactly its declared bytes, without a terminator");
    return std::string (out.view());
}
Answer import (Session& s, const std::string& input, const char* evidence = nullptr)
{
    Checked need;
    const command::ImportProject request { 927, input };
    auto spent = budget::spend ([&] { need = s.check (request); });
    ok (spent.bytes == 0, "import preflight allocates nothing");
    Answer answer;
    spent = budget::spend ([&] { answer = s.importProject (request.id, request.bytes); });
    ok (answer.command == request.id && budget::covers (need.bytes, spent), budget::describe (need.bytes, spent));
    if (spent.bytes > 0) ok (! budget::covers (std::uint64_t (spent.bytes - 1), spent), "under-declared import demand turns the harness red");
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
    const auto machine = project (false, "0.0.1") + "\n[hpf]\nfq.machine = 36\n";
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
    const auto carried = detail::carriedDefaults();
    ok (! carried.previous && *carried.current.engine.find ("defaults").string() == "2026-09",
        "current defaults are compiled and the previous slot is empty until a second version exists");
    auto s = fresh();
    const auto current = project() + "\n[hpf]\nfq.hand = 36\n";
    ok (import (*s, current).rejection == Rejection::None && s->events().empty(),
        "the carried defaults version is accepted without conversion");
    auto old = withDefaults (project (true, "0.0.1")
        + "lufs.hand = -12.5\n\n[hpf]\nfq.machine = 37\nfq.hand = 40\n", "2025-12");
    const auto before = s->revision();
    const auto answer = import (*s, old);
    ok (answer.rejection == Rejection::None && s->revision() == before + 1,
        "older defaults convert and commit exactly once");
    auto expected = fresh();
    const auto today = withDefaults (old, *detail::rules().engine.find ("defaults").string());
    ok (import (*expected, today).rejection == Rejection::None, "current-version comparison imports");
    auto a = s->snapshot(), b = expected->snapshot();
    SnapshotView av = a.view(), bv = b.view(); av.revision = bv.revision;
    ok (json (av) == json (bv), "conversion retains written machine, hand and target numbers; all omissions use current defaults");
    ok (s->events().size() == 2 && s->events()[0].kind == EventKind::Fact
        && s->events()[0].payload.fact.view().id == text::FactId::DefaultsConverted,
        "conversion emits its warning before the unchanged machine comparison fact");
    if (answer.rejection == Rejection::None && s->events().size() == 2)
    {
        const auto warning = s->events()[0];
        old.assign (old.size(), 'x');
        const auto fact = warning.payload.fact.view();
        ok (fact.argCount == 1 && fact.args[0].kind == text::ArgKind::UserText
            && fact.args[0].userText == "2025-12", "warning owns the original defaults version after input destruction");
        for (const auto lang : { text::Lang::Ru, text::Lang::En })
        {
            const auto rendered = text::Text::text (fact, lang);
            ok (rendered.find ("2025-12") != std::string::npos && rendered.find ('{') == std::string::npos,
                "conversion warning names the old version in both languages");
        }
        ok (s->events()[1].payload.fact.view().id == text::FactId::MachineDifferences
            && s->events()[1].payload.fact.view().args[0].integer == 1,
            "converted foreign machine keeps the ordinary difference count");
    }
    const auto saved = exported (*s);
    ok (saved == exported (*expected) && saved.starts_with (header (true, "0.0.1")),
        "converted export uses current defaults and preserves core provenance");
    ok (import (*s, saved).rejection == Rejection::None && exported (*s) == saved
        && s->events().size() == 1, "converted project round trips without another conversion warning");
    ok (import (*s, withDefaults (current, "2026-08")).rejection == Rejection::None
        && s->events().size() == 1 && s->events()[0].payload.fact.view().id == text::FactId::DefaultsConverted,
        "an older month with the same core converts when the complete machine matches");
    ok (import (*s, withDefaults (project() + "\n[hpf]\nfq.machine = 37\n", "2026-08")).rejection == Rejection::None
        && s->events().size() == 2, "same-core differences survive older-default conversion");
    for (const auto newer : { "2026-10", "2027-01", "9999-12" })
        refused (*s, withDefaults (project(), newer), Rejection::NewerDefaults, std::string ("\"") + newer + '"');
    for (const auto malformed : { "2026-00", "2026-13", "2026-9", "026-09", "20260-09", "2026/09", "2026-09x", " 2026-09", "2026-0a", "" })
        refused (*s, withDefaults (project(), malformed), Rejection::UnknownDefaults,
            std::string ("\"") + malformed + '"');
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

    const auto inlineFile = "manual = true\ncore = \"" + version (Session::version()) + "\"\ndefaults = \""
        + std::string (*detail::rules().engine.find ("defaults").string()) + "\"\n"
        "saturation = { mix = { hand = 0.50 }, output = { hand = -0.0 } }\n"
        "hpf = { fq = { hand = 36 }, on = { hand = false } }\n"
        "target = { tp = { hand = -1 }, name = \"allStreaming\", lufs = { hand = -12.50 } }\n";
    ok (import (*restored, inlineFile).rejection == Rejection::None, "hand-written inline tables in another order import");
    auto expected = fresh();
    (void) expected->apply (command::SetManual { 1, true });
    (void) expected->apply (command::EditTarget { 2, { -12.5, -1.0 } });
    HpfFields<Touched> hpf; hpf.on = false; hpf.fq = 36;
    SaturationFields<Touched> sat; sat.mix = 0.5; sat.output = -0.0;
    (void) expected->apply (command::EditDevice { 3, hpf });
    (void) expected->apply (command::EditDevice { 4, sat });
    ok (exported (*restored) == exported (*expected), "inline spelling and commands materialize the same project");
    ok (exported (*restored).find ("output.hand = 0\n") != std::string::npos, "negative zero prints as zero");
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
        { "saturation", "mix", "0.123456789", "-0.01" }, { "saturation", "output", "-1.25", "0.01" },
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
    (void) import (*s, project (true, "0.0.1") + "\n[hpf]\nfq.machine = 36\nfq.hand = 40\n");
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
    refused (*s, project (true, "01.2.3"), Rejection::ProjectCore, "\"01.2.3\"");
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
void foreignMachine()
{
    auto s = fresh();
    const auto input = project (true, "0.0.1") + "\n[hpf]\nfq.machine = 36\nfq.hand = 36\n"
        "\n[limiter]\nneedles.machine = \"manual\"\n";
    ok (import (*s, input).rejection == Rejection::None, "another core's complete machine layer is taken");
    const auto saved = exported (*s);
    ok (saved.find ("core = \"0.0.1\"") != std::string::npos
        && saved.find ("fq.machine = 36\nfq.hand = 36\n") != std::string::npos, "origin and author ordering survive export");
    auto held = s->snapshot();
    ok (held.view().machineDifferences.size() == 2, "snapshot exposes every machine difference");
    const auto first = held.view().machineDifferences[0];
    ok (first.device == Device::Hpf && first.field == 1 && detail::same (first.fileValue, 36)
        && detail::same (first.coreValue, s->project().devices.hpf.machine.fq) == false, "difference names device, field, file and core values");
    ok (s->events().size() == 1 && s->events()[0].kind == EventKind::Fact, "foreign import publishes its fact immediately");
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
    const auto spent = budget::spend ([&] { status = Codec::decode (encoded, decoded); });
    ok (status == CodecStatus::Ok && need.bytes == std::uint64_t (spent.bytes) && json (decoded.view()) == encoded,
        "machine differences cross the same codec with exact owned storage");
    auto replay = fresh();
    ok (import (*replay, saved).rejection == Rejection::None && exported (*replay) == saved,
        "a foreign machine layer remains replayable after exporting it");
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
    ok (s->snapshot().view().machineDifferences.empty() && version (s->project().core) == version (Session::version()),
        "a new target places the current machine and clears the old comparison");
    s.reset();
    ok (json (held.view()) == encoded, "retained comparison survives commands and session destruction");
    auto same = project (false, "0.0.1");
    ok (import (*replay, same).rejection == Rejection::None && replay->snapshot().view().machineDifferences.empty()
        && replay->events()[0].payload.fact.view().args[0].integer == 0, "a foreign core with no differences still publishes count zero");
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
                const int scale = std::max ({ int (k.from.scale), int (k.to.scale), int (k.step.scale) });
                std::int64_t from = 0, to = 0, step = 0;
                (void) detail::scaleUp (k.from.mantissa, scale - k.from.scale, from);
                (void) detail::scaleUp (k.to.mantissa, scale - k.to.scale, to);
                (void) detail::scaleUp (k.step.mantissa, scale - k.step.scale, step);
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
void olderSnapshot()
{
    Snapshot out;
    const auto need = Codec::decodedBytes (snapshotV1);
    CodecStatus status {};
    const auto spent = budget::spend ([&] { status = Codec::decode (snapshotV1, out); });
    ok (need.status == CodecStatus::Ok && status == CodecStatus::Ok && budget::covers (need.bytes, spent),
        "a frozen v1 snapshot decodes within its declared storage");
    if (status != CodecStatus::Ok) return;
    const auto& v = out.view();
    ok (! v.mandatoryMeasurementsReady && ! v.devicesPlaced && ! v.canContinueMeasurement
        && ! v.needlesRunsTruncated && v.measurementResumeState == State::Empty,
        "absent appended flags use conservative false/Empty defaults");
    ok (v.measurements.empty() && v.measurementStorage.peakBytes == 0 && v.needlesJob == 0 && v.needlesSource == 0
        && ! v.needlesNeedDb && ! v.needlesCeilingDb && v.needlesBytes == 0 && v.needlesLargestBlockBytes == 0
        && v.needlesProgress.name == PhaseName::Stream && v.needlesProgress.totalUnits == 0,
        "all other fields appended since frozen v1 have zero, empty or null defaults");
    ok (v.eqCurve.size() == 2 && v.handFieldCount == 2, "historical rows and fields retain their values");
    auto partial = std::string (snapshotV1);
    partial.insert (1, "\"mandatoryMeasurementsReady\":true,");
    ok (Codec::decode (partial, out) == CodecStatus::Ok && out.view().mandatoryMeasurementsReady && ! out.view().devicesPlaced,
        "present appended flags survive default initialization of absent peers");
    auto invalid = std::string (snapshotV1);
    invalid.insert (1, "\"devicesPlaced\":null,");
    const auto rejected = budget::spend ([&] { status = Codec::decode (invalid, out); });
    ok (status == CodecStatus::Invalid && rejected.bytes == 0 && out.view().mandatoryMeasurementsReady,
        "an optional field with an invalid present value rejects before allocation and preserves output");
    ok (Codec::decode (snapshotV1, out) == CodecStatus::Ok && ! out.view().mandatoryMeasurementsReady,
        "decoding an older snapshot replaces previous values with documented defaults");
}
}
int main()
{
    sparseSections(); defaultsVersions(); roundTrip(); domainsAndExactNumbers(); refusals(); foreignMachine(); numbers(); slicing(); demandsAndOrder(); olderSnapshot();
    return felitronics::test::report();
}
