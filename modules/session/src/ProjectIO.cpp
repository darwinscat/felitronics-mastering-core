// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "ProjectIO.h"
#include "Planner.h"
#include "Devices.h"
#include "Grid.h"
#include "JsonCodec.h"
#include "BuildContract.h"
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

using toml::Need;
using detail::Rules;

bool defaultsLabel (std::string_view text) noexcept
{
    if (text.size() != 7 || text[4] != '-') return false;
    for (std::size_t i = 0; i < text.size(); ++i)
        if (i != 4 && (text[i] < '0' || text[i] > '9')) return false;
    return text.substr (5) >= "01" && text.substr (5) <= "12";
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
    void value (SaturationType t) noexcept { string (detail::kSaturationTypeNames[std::size_t (t)]); }
    template <class T> void value (T n) noexcept
    {
        char buffer[64];
        if constexpr (std::is_same_v<T, double>)
        {
            // Keep ordinary TOML numbers where its Decimal subset represents them exactly.
            // Other finite binary64 values use a quoted shortest round-trip decimal.
            const bool plain = detail::decimalOf (n).has_value();
            const auto result = std::to_chars (buffer, buffer + sizeof (buffer), detail::kept (n),
                                               plain ? std::chars_format::fixed : std::chars_format::general);
            if (result.ec != std::errc {}) detail::storageOverflow();
            const std::string_view digits { buffer, std::size_t (result.ptr - buffer) };
            if (plain) text (digits); else string (digits);
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
        text ("\nmanual = "); value (p.manual);
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

// Import and commands use the same binary64 domain predicate, including the actual source rate.
template <class T> bool field (toml::Reader& in, std::string_view key, const Rules& rules, const detail::FieldRule& rule, T& out, std::uint32_t sourceRate)
{
    if constexpr (std::is_same_v<T, double>)
    {
        const auto* value = in.data().find (key);
        if (value && std::holds_alternative<std::string> (value->data))
        {
            std::string digits;
            if (! in.optional (key, digits)) return false;
            detail::Storage storage;
            detail::Reader reader { digits, storage, 0, true, {} };
            const auto number = reader.number();
            if (! reader.good || reader.pos != digits.size() || ! detail::JsonNumber::read (number, out))
            { in.refuse (key, unsigned (Rejection::ProjectType)); return false; }
        }
        else if (! in.optional (key, out)) return false;
        if (! std::isfinite (out)) in.refuse (key, unsigned (Rejection::NotFinite));
        else if (! rule.knob.accepts (out, sourceRate)) in.refuse (key, unsigned (Rejection::OutOfDomain));
        out = detail::kept (out);
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
    else if constexpr (std::is_same_v<T, SaturationType>)
    {
        // The machine's layer may hold any of the config's eight; a person's only the types a hand edit takes.
        std::string name;
        if (! in.optional (key, name)) return false;
        const auto type = detail::saturationTypeNamed (name);
        if (type && (key != "hand" || detail::handSaturationType (*type))) out = *type;
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
        case toml::Fault::OutOfRange: return Rejection::OutOfDomain;
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
    // The machine's layer is written once the first measurement ends — exportable while what the devices read still ends.
    if (! devicesPlaced_) return { state_ == State::Empty ? Rejection::NoSource : Rejection::NotPlaced, kNoField, 0 };
    // ...and once no field of it is a placeholder for a measurement still to end (owner, 02.10).
    if (detail::anyPending (plan_.devices)) return { Rejection::PlanPending, kNoField, 0 };
    Writer w; w.project (project_, detail::rules(), source_.channels);
    return { Rejection::None, kNoField, w.size };
}
Rejection Session::exportProject (std::span<char> output) const noexcept
{
    const auto need = exportProjectBytes();
    if (need.rejection != Rejection::None) return need.rejection;
    if (output.size() < need.bytes) return Rejection::TooLong;
    Writer w { output.data() }; w.project (project_, detail::rules(), source_.channels);
    return Rejection::None;
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
    constexpr std::string_view required[] { "defaults", "manual", "target", "target.name" };
    const auto library = toml::storageFor (bytes, toml::ReadStorage { required });
    constexpr auto limit = std::numeric_limits<std::size_t>::max();
    const auto owned = ImportedProject::storageBytes();
    if (library.parse == limit || library.read == limit || library.read > limit - library.parse
        || owned > limit - library.parse - library.read)
        return { Rejection::ProjectTooLarge, kNoField, 0 };
    const auto total = std::uint64_t (library.parse) + library.read + owned;
    return { Rejection::None, kNoField, total, 0.0, total };
}
ImportedProject readProject (std::string_view bytes, const PlanInputs& inputs) noexcept
{
    const auto channels = inputs.channels, offeredDevices = inputs.offered, sourceRate = inputs.sampleRate;
    ImportedProject out;
    auto parsed = toml::parse (bytes);
    if (const auto* error = std::get_if<toml::Error> (&parsed))
    {
        out.answer.rejection = Rejection::ProjectSyntax;
        out.answer.position = { error->line, error->column };
        return out;
    }
    const auto& root = *std::get_if<toml::Table> (&parsed);
    const Rules rules = detail::rules();
    const auto currentLabel = *rules.engine.find ("defaults").string();
    std::string defaults, target;
    const auto report = toml::read (root, [&] (toml::Reader& in)
    {
        (void) in.required ("defaults", defaults);
        (void) in.required ("manual", out.project.manual);
        (void) in.table ("target", Need::Required, [&] (toml::Reader& t)
        {
            (void) t.required ("name", target);
            const auto readTarget = [&] (std::string_view key, const Knob& knob, std::optional<double>& value)
            {
                (void) t.table (key, Need::Optional, [&] (toml::Reader& layer)
                {
                    double n = 0;
                    if (field (layer, "hand", rules, knobRule (knob), n, sourceRate)) value = n;
                });
            };
            readTarget ("lufs", rules.lufs, out.project.targetEdit.lufs);
            readTarget ("tp", rules.tp, out.project.targetEdit.tp);
        });
        // A missing/unknown target is reported after schema checks; use a known row only for filling defaults.
        out.project.target = rules.find (target).value_or (rules.defaultRow);
        placeDefaults (rules, out.project.target, channels, out.project.devices);
        eachDevice (out.project.devices, [&] (Device, auto& layers)
        {
            using Of = DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
            (void) in.table (Of::name, Need::Optional, [&] (toml::Reader& device)
            {
                Of::each (rules, [&] (std::uint8_t i, const FieldRule& rule, auto& machine, auto& hand)
                {
                    (void) device.table (Of::fields[i], Need::Optional, [&] (toml::Reader& authors)
                    {
                        (void) field (authors, "machine", rules, rule, machine, sourceRate);
                        using T = std::remove_cvref_t<decltype (machine)>;
                        T n {};
                        if (field (authors, "hand", rules, rule, n, sourceRate)) hand = n;
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
    else if (defaults != currentLabel)
        fail (Rejection::UnknownDefaults, root.find ("defaults")->position);
    if (out.answer.rejection != Rejection::None) return out;
    if (! rules.find (target))
    {
        const auto& t = *std::get_if<toml::Table> (&root.find ("target")->data);
        fail (Rejection::UnknownTarget, t.find ("name")->position);
    }
    if (out.answer.rejection != Rejection::None) return out;
    // The planner's machine layer for the file's target on this source: what the machine would decide now — whole, or
    // the file waits: a field it has not measured yet for that target is no opinion to compare the file's against.
    PlanInputs now = inputs;
    now.row = out.project.target;
    now.targetEdit = out.project.targetEdit;
    Devices decided;
    DevicePlans plans;
    PlanFindings found;
    propose (now, decided, plans, found);
    if (anyPending (plans))
    {
        const auto& t = *std::get_if<toml::Table> (&root.find ("target")->data);
        fail (Rejection::PlanPending, t.find ("name")->position);
        return out;
    }
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
                // Dither's saved hand may be dormant above its delivery bit depth. It can be reverted,
                // and is used again on an eligible target; editDevice still requires eligibility.
                if ((offeredDevices & (1u << unsigned (device))) == 0
                    || (device != Device::Dither && ! offered (rules, out.project.target, channels, device)))
                    fail (Rejection::NotOffered, at ("hand"));
            }
            if (! detail::same (double (machine), double (current)))
            {
                if ((offeredDevices & (1u << unsigned (device))) == 0) fail (Rejection::NotOffered, at ("machine"));
                out.differences[out.differenceCount++] = { device, i, double (machine), double (current) };
            }
        }, layers.machine, layers.hand, Of::layers (decided).machine);
    });
    return out;
}
}
}
