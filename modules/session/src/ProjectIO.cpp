// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "ProjectIO.h"
#include "Devices.h"
#include "Grid.h"
#include <felitronics/toml/Schema.h>
#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <type_traits>

namespace felitronics::session
{
namespace
{
using toml::Reader;
using toml::Need;
using detail::Rules;

bool sameVersion (Version a, Version b) noexcept
{
    return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
}
bool defaultsLabel (std::string_view text) noexcept
{
    if (text.size() != 7 || text[4] != '-') return false;
    for (std::size_t i = 0; i < text.size(); ++i)
        if (i != 4 && (text[i] < '0' || text[i] > '9')) return false;
    return text.substr (5) >= "01" && text.substr (5) <= "12";
}
bool readVersion (std::string_view text, Version& out) noexcept
{
    std::size_t pos = 0;
    for (auto* part : { &out.major, &out.minor, &out.patch })
    {
        const std::size_t start = pos;
        *part = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
        {
            const auto digit = std::uint32_t (text[pos++] - '0');
            if (*part > (std::numeric_limits<std::uint32_t>::max() - digit) / 10) return false;
            *part = *part * 10 + digit;
        }
        if (pos == start || (pos > start + 1 && text[start] == '0')) return false;
        if (part != &out.patch && (pos == text.size() || text[pos++] != '.')) return false;
    }
    return pos == text.size();
}

// One walk sizes and writes the canonical document. No temporary TOML tree or string is allocated.
struct Writer
{
    char* output = nullptr;
    std::size_t size = 0;
    void put (char c) noexcept { if (output) output[size] = c; ++size; }
    void text (std::string_view s) noexcept { for (char c : s) put (c); }
    void string (std::string_view s) noexcept
    {
        constexpr char hex[] = "0123456789abcdef";
        put ('"');
        for (char c : s)
        {
            const auto byte = static_cast<unsigned char> (c);
            if (c == '"' || c == '\\') { put ('\\'); put (c); }
            else if (byte < 32 || byte == 127) { text ("\\u00"); put (hex[byte >> 4]); put (hex[byte & 15]); }
            else put (c);
        }
        put ('"');
    }
    void value (bool b) noexcept { text (b ? "true" : "false"); }
    void value (Needles n) noexcept
    {
        switch (n)
        {
            case Needles::Auto: string ("auto"); break;
            case Needles::Manual: string ("manual"); break;
            case Needles::Off: string ("off"); break;
        }
    }
    template <class T> void value (T n) noexcept
    {
        char buffer[64];
        if constexpr (std::is_same_v<T, double>)
        {
            // Fixed notation is the TOML subset's decimal notation. This overload writes the shortest
            // decimal that rounds back to n. Every project number is on a validated, at most nine-place grid.
            const auto result = std::to_chars (buffer, buffer + sizeof (buffer), detail::kept (n), std::chars_format::fixed);
            text ({ buffer, std::size_t (result.ptr - buffer) });
        }
        else
        {
            const auto result = std::to_chars (buffer, buffer + sizeof (buffer), n);
            text ({ buffer, std::size_t (result.ptr - buffer) });
        }
    }
    template <class T> void line (std::string_view field, std::string_view author, const T& n) noexcept
    {
        text (field); put ('.'); text (author); text (" = "); value (n); put ('\n');
    }
    void project (const Project& p, const Rules& rules, std::uint32_t channels) noexcept
    {
        text ("defaults = "); string (*rules.engine.find ("defaults").string());
        text ("\ncore = \""); value (p.core.major); put ('.'); value (p.core.minor); put ('.'); value (p.core.patch);
        text ("\"\nmanual = "); value (p.manual);
        text ("\n\n[target]\nname = "); string (rules.row (p.target).key); put ('\n');
        if (p.targetEdit.lufs) line ("lufs", "hand", *p.targetEdit.lufs);
        if (p.targetEdit.tp) line ("tp", "hand", *p.targetEdit.tp);
        Devices defaults;
        detail::placeDefaults (rules, p.target, channels, defaults);
        detail::eachDevice (p.devices, [&] (Device, const auto& layers)
        {
            using Of = detail::DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
            bool started = false;
            Of::each (rules, [&] (std::uint8_t field, const detail::FieldRule&, const auto& machine, const auto& hand, const auto& def)
            {
                const bool different = ! detail::same (double (machine), double (def));
                if (! different && ! hand) return;
                if (! started) { text ("\n["); text (Of::name); text ("]\n"); started = true; }
                if (different) line (Of::fields[field], "machine", machine);
                if (hand) line (Of::fields[field], "hand", *hand);
            }, layers.machine, layers.hand, Of::layers (defaults).machine);
        });
    }
};

// Reading a numeric field keeps the written decimal for its travel and grid checks.
// A decimal that rounds onto a step from just beside it is still refused.
template <class T> bool field (Reader& in, std::string_view key, const Rules& rules, const detail::FieldRule& rule, T& out)
{
    if constexpr (std::is_same_v<T, double>)
    {
        toml::Decimal d;
        if (! in.optional (key, d)) return false;
        if (detail::compare (d, rule.knob.from) < 0 || detail::compare (d, rule.knob.to) > 0)
            in.refuse (key, unsigned (Rejection::OutOfTravel));
        else if (! detail::onGrid (d, rule.knob.from, rule.knob.step)) in.refuse (key, unsigned (Rejection::OffStep));
        out = detail::kept (d.toDouble());
    }
    else if constexpr (std::is_same_v<T, Needles>)
    {
        std::string mode;
        if (! in.optional (key, mode)) return false;
        if (mode == "auto") out = Needles::Auto;
        else if (mode == "manual") out = Needles::Manual;
        else if (mode == "off") out = Needles::Off;
        else in.refuse (key, unsigned (Rejection::NotOneOf));
    }
    else
    {
        if (! in.optional (key, out)) return false;
        if constexpr (std::is_same_v<T, std::int32_t>)
            if (! rules.slope (out)) in.refuse (key, unsigned (Rejection::NotOneOf));
    }
    return true;
}
Rejection rejection (const toml::Problem& p) noexcept
{
    switch (p.fault)
    {
        case toml::Fault::Missing: return Rejection::ProjectMissing;
        case toml::Fault::WrongType: return Rejection::ProjectType;
        case toml::Fault::OutOfRange: return Rejection::OutOfTravel;
        case toml::Fault::UnknownKey: return Rejection::ProjectUnknownKey;
        case toml::Fault::Refused: return Rejection (p.detail);
    }
    return Rejection::ProjectSyntax;
}
ProjectPosition position (toml::Position p) noexcept { return { p.line, p.column }; }
void identifyField (Answer& answer, std::string_view path, const Rules& rules) noexcept
{
    if (path == "target.lufs.hand") answer.field = 0;
    if (path == "target.tp.hand") answer.field = 1;
    Devices devices;
    detail::eachDevice (devices, [&] (Device device, const auto& layers)
    {
        using Of = detail::DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
        Of::each (rules, [&] (std::uint8_t i, const detail::FieldRule&, const auto&)
        {
            const auto prefix = Of::name;
            const auto key = Of::fields[i];
            if (path.size() <= prefix.size() + key.size() + 1 || path.compare (0, prefix.size(), prefix) != 0
                || path[prefix.size()] != '.' || path.compare (prefix.size() + 1, key.size(), key) != 0
                || path[prefix.size() + key.size() + 1] != '.') return;
            answer.device = device; answer.field = i;
        }, layers.machine);
    });
}
}

std::string_view ProjectText::view() const noexcept { return { data.get(), size }; }
Checked Session::exportProjectBytes() const noexcept
{
    if (checkFloatingPointEnvironment() != Status::Ok) return { Rejection::FloatingPointEnvironment, kNoField, 0 };
    if (! placed()) return { state_ == State::Empty ? Rejection::NoSource : Rejection::NotPlaced, kNoField, 0 };
    Writer w; w.project (project_, detail::rules(), source_.channels);
    return { Rejection::None, kNoField, w.size };
}
ProjectText Session::exportProject() const noexcept
{
    ProjectText out;
    const auto need = exportProjectBytes();
    out.rejection = need.rejection;
    if (need.rejection != Rejection::None) return out;
    out.size = std::size_t (need.bytes);
    out.data.reset (new char[out.size]);
    Writer w { out.data.get() }; w.project (project_, detail::rules(), source_.channels);
    return out;
}
Answer Session::importProject (CommandId id, std::string_view bytes) noexcept
{
    return apply (command::ImportProject { id, bytes });
}

namespace detail
{
Checked importBytes (std::string_view bytes) noexcept
{
    // readProject visits each table/field once. A custom refusal follows a successful conversion,
    // so it is the only problem at that key. Only missing required paths need extra report slots.
    constexpr std::string_view required[] { "defaults", "core", "manual", "target", "target.name" };
    const auto library = toml::storageFor (bytes, toml::ReadStorage { required });
    constexpr auto limit = std::numeric_limits<std::size_t>::max();
    const auto owned = ImportedProject::storageBytes();
    if (library.parse == limit || library.read == limit || library.read > limit - library.parse
        || owned > limit - library.parse - library.read)
        return { Rejection::ProjectTooLarge, kNoField, 0 };
    return { Rejection::None, kNoField, std::uint64_t (library.parse) + library.read + owned };
}
ImportedProject readProject (std::string_view bytes, std::uint32_t channels) noexcept
{
    ImportedProject out;
    auto parsed = toml::parse (bytes);
    if (const auto* error = std::get_if<toml::Error> (&parsed))
    {
        out.answer.rejection = Rejection::ProjectSyntax;
        out.answer.position = { error->line, error->column };
        return out;
    }
    const auto& root = *std::get_if<toml::Table> (&parsed);
    const auto carried = carriedDefaults();
    const Rules& rules = carried.current;
    const auto currentLabel = *rules.engine.find ("defaults").string();
    const auto previousLabel = carried.previous ? carried.previous->engine.find ("defaults").string() : std::nullopt;
    std::string defaults, core, target;
    const auto report = toml::read (root, [&] (Reader& in)
    {
        (void) in.required ("defaults", defaults);
        (void) in.required ("core", core);
        (void) in.required ("manual", out.project.manual);
        (void) in.table ("target", Need::Required, [&] (Reader& t)
        {
            (void) t.required ("name", target);
            const auto readTarget = [&] (std::string_view key, const Knob& knob, std::optional<double>& value)
            {
                (void) t.table (key, Need::Optional, [&] (Reader& layer)
                {
                    double n = 0;
                    if (field (layer, "hand", rules, knobRule (knob), n)) value = n;
                });
            };
            readTarget ("lufs", rules.lufs, out.project.targetEdit.lufs);
            readTarget ("tp", rules.tp, out.project.targetEdit.tp);
        });
        // A missing/unknown target is reported after schema checks; use a known row only for filling defaults.
        out.project.target = rules.find (target).value_or (rules.defaultRow);
        const Rules& defaultsRules = previousLabel && defaults == *previousLabel ? *carried.previous : rules;
        placeDefaults (defaultsRules, defaultsRules.find (target).value_or (defaultsRules.defaultRow), channels, out.project.devices);
        eachDevice (out.project.devices, [&] (Device, auto& layers)
        {
            using Of = DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
            (void) in.table (Of::name, Need::Optional, [&] (Reader& device)
            {
                Of::each (rules, [&] (std::uint8_t i, const FieldRule& rule, auto& machine, auto& hand)
                {
                    (void) device.table (Of::fields[i], Need::Optional, [&] (Reader& authors)
                    {
                        (void) field (authors, "machine", rules, rule, machine);
                        using T = std::remove_cvref_t<decltype (machine)>;
                        T n {};
                        if (field (authors, "hand", rules, rule, n)) hand = n;
                    });
                }, layers.machine, layers.hand);
            });
        });
    });
    if (! report.ok())
    {
        const auto& p = report.problems.front();
        out.answer.rejection = rejection (p);
        out.answer.position = position (p.position);
        identifyField (out.answer, p.path, rules);
        return out;
    }
    const auto fail = [&] (Rejection r, toml::Position p)
    {
        out.answer.rejection = r; out.answer.position = position (p);
    };
    if (! defaultsLabel (defaults))
        fail (Rejection::UnknownDefaults, root.find ("defaults")->position);
    else if (defaults > currentLabel)
        fail (Rejection::NewerDefaults, root.find ("defaults")->position);
    else if (defaults < previousLabel.value_or (currentLabel))
    {
        out.convertedDefaults = true;
        std::copy_n (defaults.data(), sizeof (out.originalDefaults), out.originalDefaults);
    }
    else if (defaults != currentLabel && (! previousLabel || defaults != *previousLabel))
        fail (Rejection::UnknownDefaults, root.find ("defaults")->position);
    if (out.answer.rejection != Rejection::None) return out;
    if (! readVersion (core, out.project.core)) fail (Rejection::ProjectCore, root.find ("core")->position);
    else if (! rules.find (target))
    {
        const auto& t = *std::get_if<toml::Table> (&root.find ("target")->data);
        fail (Rejection::UnknownTarget, t.find ("name")->position);
    }
    if (out.answer.rejection != Rejection::None) return out;
    out.foreignCore = ! sameVersion (out.project.core, Session::version());
    Devices decided;
    Answer mismatch;
    placeMachine (rules, out.project.target, channels, decided);
    eachDevice (out.project.devices, [&] (Device device, const auto& layers)
    {
        using Of = DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
        const auto* section = root.find (Of::name);
        const auto* table = section ? std::get_if<toml::Table> (&section->data) : nullptr;
        Of::each (rules, [&] (std::uint8_t i, const FieldRule&, const auto& machine, const auto& hand, const auto& current)
        {
            const auto at = [&] (std::string_view author)
            {
                const auto* v = table ? table->find (Of::fields[i]) : nullptr;
                const auto* t = v ? std::get_if<toml::Table> (&v->data) : nullptr;
                const auto* a = t ? t->find (author) : nullptr;
                return a ? a->position : v ? v->position : table ? table->position : root.position;
            };
            if (out.answer.rejection == Rejection::None && hand)
            {
                if (! out.project.manual) fail (Rejection::ManualOff, at ("hand"));
                else if (! offered (rules, out.project.target, channels, device)) fail (Rejection::NotOffered, at ("hand"));
            }
            if (! detail::same (double (machine), double (current)))
            {
                if (mismatch.rejection == Rejection::None && ! out.foreignCore)
                {
                    mismatch.rejection = Rejection::MachineMismatch;
                    mismatch.position = position (at ("machine"));
                    mismatch.device = device; mismatch.field = i;
                }
                out.differences[out.differenceCount++] = { device, i, double (machine), double (current) };
            }
        }, layers.machine, layers.hand, Of::layers (decided).machine);
    });
    if (out.answer.rejection == Rejection::None) out.answer = mismatch;
    return out;
}
}
}
