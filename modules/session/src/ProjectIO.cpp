// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "ProjectIO.h"
#include "Planner.h"
#include "Devices.h"
#include "Grid.h"
#include "JsonCodec.h"
#include "BuildContract.h"
#include "MasterJob.h"
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

// A device field's truth exactly as `bool (x)` gives it — a number's is "not zero": NaN true, −0 false — spelled for a
// number without the float `!=` gcc's -Wfloat-equal refuses.
template <class T> bool truthOf (const T& x) noexcept
{
    if constexpr (std::is_floating_point_v<T>) return ! core::exactlyEqual (x, T (0));
    else return bool (x);
}

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
    void value (std::string_view s) noexcept { string (s); }
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
    template <class T> void workedLine (std::string_view field, const T& n, std::string_view origin) noexcept
    {
        text (field); text (" = "); value (n); text (" # "); text (origin); put ('\n');
    }
    void project (const Project& p, const Rules& rules, std::uint32_t channels) noexcept
    {
        text ("defaults = "); string (*rules.engine.find ("defaults").string());
        text ("\nmanual = "); value (p.manual);
        text ("\n\n[target]\nname = "); string (rules.row (p.target).key); put ('\n');
        if (p.targetEdit.lufs) line ("lufs", "hand", *p.targetEdit.lufs);
        if (p.targetEdit.tp) line ("tp", "hand", *p.targetEdit.tp);
        if (p.targetEdit.loudnessMode)
        {
            text ("loudnessMode.hand = ");
            string (*p.targetEdit.loudnessMode == LoudnessMode::MaxClean ? std::string_view ("maxClean")
                    : *p.targetEdit.loudnessMode == LoudnessMode::MaxDense ? std::string_view ("maxDense")
                    : *p.targetEdit.loudnessMode == LoudnessMode::MaxExtreme ? std::string_view ("maxExtreme")
                                                                           : std::string_view ("manual"));
            put ('\n');
        }
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
    void stringInteger (std::uint64_t n) noexcept
    {
        char buffer[32];
        const auto end = std::to_chars (buffer, buffer + sizeof (buffer), n);
        string ({ buffer, std::size_t (end.ptr - buffer) });
    }
    void renderState (const command::MasterReady& ready) noexcept
    {
        // Physical parameters are derived by the machine, or supplied explicitly by a ready caller.
        // The device tables above retain the individual default/machine/hand provenance of its controls.
        const std::string_view origin = ready.version == 1 ? "hand" : "machine";
        const auto& t = ready.topology;
        const auto& p = ready.params;
        text ("\n[renderState]\n");
        workedLine ("topology.internalBlock", t.internalBlock, origin);
        workedLine ("topology.eq", t.eq, origin);
        workedLine ("topology.monoBass", t.monoBass, origin);
        workedLine ("topology.stereoAir", t.stereoAir, origin);
        workedLine ("topology.compressor", t.compressor, origin);
        workedLine ("topology.clipper", t.clipper, origin);
        workedLine ("topology.limiter", t.limiter, origin);
        workedLine ("topology.dither", t.dither, origin);
        workedLine ("topology.compressorLookaheadMs", t.compressorLookaheadMs, origin);
        workedLine ("topology.limiterLookaheadMs", t.limiterLookaheadMs, origin);
        workedLine ("topology.oversampleFactor", t.oversampleFactor, origin);
        workedLine ("topology.tapsPerPhase", t.tapsPerPhase, origin);
        workedLine ("topology.sidechainHpfHz", t.sidechainHpfHz, origin);
        workedLine ("params.inputGainDb", p.inputGainDb, "machine");
        workedLine ("params.preLimiterGainDb", p.preLimiterGainDb, "machine");
        workedLine ("params.monoBass.enabled", p.monoBass.enabled, origin);
        workedLine ("params.monoBass.frequencyHz", double (p.monoBass.frequencyHz), origin);
        workedLine ("params.monoBass.lowWidth", double (p.monoBass.lowWidth), origin);
        workedLine ("params.stereoAir.enabled", p.stereoAir.enabled, origin);
        workedLine ("params.stereoAir.frequencyHz", double (p.stereoAir.frequencyHz), origin);
        workedLine ("params.stereoAir.gainDb", double (p.stereoAir.gainDb), origin);
        workedLine ("params.compressor.detector", int (p.compressor.detector), origin);
        workedLine ("params.compressor.link", int (p.compressor.link), origin);
        workedLine ("params.compressor.rmsWindowMs", p.compressor.rmsWindowMs, origin);
        workedLine ("params.compressor.mode", int (p.compressor.mode), origin);
        workedLine ("params.compressor.thresholdDb", p.compressor.thresholdDb, origin);
        workedLine ("params.compressor.ratio", p.compressor.ratio, origin);
        workedLine ("params.compressor.kneeDb", p.compressor.kneeDb, origin);
        workedLine ("params.compressor.rangeDb", p.compressor.rangeDb, origin);
        workedLine ("params.compressor.attackMs", p.compressor.attackMs, origin);
        workedLine ("params.compressor.releaseMs", p.compressor.releaseMs, origin);
        workedLine ("params.compressor.makeupDb", p.compressor.makeupDb, origin);
        workedLine ("params.compressor.autoMakeup", p.compressor.autoMakeup, origin);
        workedLine ("params.compressor.lookaheadMs", p.compressor.lookaheadMs, origin);
        workedLine ("params.clipper.shape", int (p.clipper.shape), origin);
        workedLine ("params.clipper.driveDb", double (p.clipper.driveDb), origin);
        workedLine ("params.clipper.bias", double (p.clipper.bias), origin);
        workedLine ("params.clipper.mix", double (p.clipper.mix), origin);
        workedLine ("params.clipper.outputDb", double (p.clipper.outputDb), origin);
        workedLine ("params.clipper.autoComp", double (p.clipper.autoComp), origin);
        workedLine ("params.clipper.dcBlockHz", double (p.clipper.dcBlockHz), origin);
        workedLine ("params.limiter.ceilingDbTp", p.limiter.ceilingDbTp, "target");
        workedLine ("params.limiter.releaseMs", p.limiter.releaseMs, origin);
        workedLine ("params.limiter.dualRelease", p.limiter.dualRelease, origin);
        workedLine ("params.limiter.slowReleaseMs", p.limiter.slowReleaseMs, origin);
        workedLine ("params.limiter.peakClip", p.limiter.peakClip, origin);
        workedLine ("params.limiter.overCeilingDb", p.limiter.overCeilingDb, origin);
        workedLine ("params.limiter.kneeDb", p.limiter.kneeDb, origin);
        workedLine ("params.peakClipCutDb", p.peakClipCutDb, origin);
        workedLine ("params.peakClipPeakDb", p.peakClipPeakDb, "machine");
        workedLine ("params.dither.bits", p.dither.bits, "target");
        workedLine ("params.dither.shaping", int (p.dither.shaping), origin);
        text ("params.dither.seed = "); stringInteger (p.dither.seed); text (" # "); text (origin); put ('\n');
        workedLine ("params.dither.autoBlank", p.dither.autoBlank, origin);
        workedLine ("params.dither.autoBlankSamples", p.dither.autoBlankSamples, origin);
        workedLine ("params.bypassEq", p.bypassEq, origin);
        workedLine ("params.bypassMonoBass", p.bypassMonoBass, origin);
        workedLine ("params.bypassCompressor", p.bypassCompressor, origin);
        workedLine ("params.bypassClipper", p.bypassClipper, origin);
        workedLine ("params.bypassLimiter", p.bypassLimiter, origin);
        workedLine ("params.bypassDither", p.bypassDither, origin);
        workedLine ("params.compressorMix", p.compressorMix, origin);
        for (const auto& band : p.eqBands)
        {
            text ("\n[[renderState.eqBands]]\n");
            workedLine ("on", band.on, origin);
            workedLine ("type", int (band.type), origin);
            workedLine ("swept", band.swept, origin);
            workedLine ("bypass", band.bypass, origin);
            workedLine ("dyn.on", band.dyn.on, origin);
            workedLine ("dyn.rangeDb", band.dyn.rangeDb, origin);
            workedLine ("dyn.thrDb", band.dyn.thrDb, origin);
            workedLine ("dyn.thrAuto", band.dyn.thrAuto, origin);
            workedLine ("dyn.atk", band.dyn.atk, origin);
            workedLine ("dyn.rel", band.dyn.rel, origin);
            for (const auto& lane : band.lanes)
            {
                text ("\n[[renderState.eqBands.lanes]]\n");
                workedLine ("on", lane.on, origin);
                workedLine ("freq", lane.freq, origin);
                workedLine ("Q", lane.Q, origin);
                workedLine ("gainDb", lane.gainDb, origin);
                workedLine ("slope", lane.slope, origin);
                workedLine ("bypass", lane.bypass, origin);
            }
        }
    }
    void worked (MasterId id, const Kept& master, const command::MasterReady& rendered, const Rules& rules, std::uint32_t channels) noexcept
    {
        const auto& p = master.recipe.project;
        const auto row = rules.row (p.target);
        const auto mode = master.report ? master.report->deliveryMode : DeliveryMode::Mastered;
        const bool chain = mode == DeliveryMode::Mastered;
        const auto deliveryMode = mode == DeliveryMode::AsIs ? std::string_view ("asIs")
            : mode == DeliveryMode::PeaksOnly ? std::string_view ("peaksOnly") : std::string_view ("mastered");
        workedLine ("masterId", id, "target");
        workedLine ("mode", deliveryMode, "target");
        workedLine ("chain", chain, "target");
        workedLine ("gainDb", chain ? master.report->gainFromSourceDb.value_or (0.0) : master.report->deliveryGainDb, "target");

        text ("\n[target]\n");
        workedLine ("name", row.key, "target");
        workedLine ("lufs", p.targetEdit.lufs.value_or (row.lufs.toDouble()), p.targetEdit.lufs ? "hand" : "target");
        workedLine ("ceilingDbTp", p.targetEdit.tp.value_or (row.tp.toDouble()), p.targetEdit.tp ? "hand" : "target");
        const auto loudnessMode = detail::loudnessModeOf (rules, p);
        workedLine ("loudnessMode", loudnessMode == LoudnessMode::MaxClean ? std::string_view ("maxClean")
            : loudnessMode == LoudnessMode::MaxDense ? std::string_view ("maxDense")
            : loudnessMode == LoudnessMode::MaxExtreme ? std::string_view ("maxExtreme") : std::string_view ("manual"),
            p.targetEdit.loudnessMode ? "hand" : "target");

        text ("\n[delivery]\n");
        workedLine ("sampleRate", master.recipe.deliveryRateHz, "target");
        workedLine ("bitDepth", master.recipe.deliveryBits, "target");
        workedLine ("dither", master.report && master.report->deliveryDithered, "target");
        workedLine ("fileFormat", mode == DeliveryMode::AsIs ? std::string_view ("original") : std::string_view ("wav"), "target");
        workedLine ("ceilingDbTp", master.report->ceilingDbTp, "target");
        text ("\n[render]\n");
        workedLine ("inputGainDb", chain ? rendered.params.inputGainDb : master.report->deliveryGainDb, chain ? "machine" : "target");
        workedLine ("preLimiterGainDb", chain ? rendered.params.preLimiterGainDb : 0.0, chain ? "machine" : "target");

        Devices defaults;
        detail::placeDefaults (rules, p.target, channels, defaults);
        detail::eachDevice (p.devices, [&] (Device device, const auto& layers)
        {
            using Of = detail::DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
            const auto settings = detail::settingsOf (rules, layers);
            const auto& def = Of::layers (defaults).machine;
            text ("\n["); text (Of::name); text ("]\n");
            if (device == Device::Limiter)
                workedLine ("on", chain && rendered.topology.limiter && ! rendered.params.bypassLimiter,
                    ! chain ? "target" : master.recipe.readyVersion == 1 ? "hand" : "default");
            Of::each (rules, [&] (std::uint8_t field, const detail::FieldRule&, const auto& sounded,
                                  const auto& machine, const auto& hand, const auto& defaultValue)
            {
                const bool isOn = Of::fields[field] == std::string_view ("on");
                if (isOn && ! chain)
                {
                    const bool on = mode == DeliveryMode::PeaksOnly && device == Device::Dither
                        && master.report && master.report->deliveryDithered;
                    workedLine (Of::fields[field], on, "target");
                    return;
                }
                std::string_view origin = "default";
                if (hand) origin = "hand";
                else if (isOn && detail::tickFrom (rules, layers) == TickFrom::Touched) origin = "hand";
                else if (! detail::same (double (machine), double (defaultValue))) origin = "machine";
                // The glue's five as they applied: the numbers the render gave its compressor — a field by hand at its
                // value, every other at the law at the amount as it sounded — not the ones placed with the machine's own
                // amount. A glue out of the chain applied none, and its five stay as the project holds them.
                if (device == Device::Glue && chain && rendered.topology.compressor && ! rendered.params.bypassCompressor)
                {
                    const auto& c = rendered.params.compressor;
                    const auto name = Of::fields[field];
                    const double* applied = name == "thresholdDb" ? &c.thresholdDb : name == "ratio" ? &c.ratio
                        : name == "kneeDb" ? &c.kneeDb : name == "attackMs" ? &c.attackMs : name == "releaseMs" ? &c.releaseMs : nullptr;
                    if (applied)
                    {
                        workedLine (name, *applied, hand || master.recipe.readyVersion == 1 ? std::string_view ("hand")
                            : ! detail::same (*applied, double (defaultValue)) ? std::string_view ("machine") : std::string_view ("default"));
                        return;
                    }
                }
                // The waterfall (MVP): the mixes and the cut as the landing steered them, the machine's.
                if (chain && master.report && master.report->waterfall)
                {
                    const auto name = Of::fields[field];
                    const auto& v = rendered.params;
                    if (device == Device::Glue && name == "mix") { workedLine (name, v.compressorMix, "machine"); return; }
                    if (device == Device::Saturation && name == "mix") { workedLine (name, double (v.clipper.mix), "machine"); return; }
                    if (device == Device::Saturation && name == "drive" && master.report->waterfall->saturation.drive)
                    { workedLine (name, *master.report->waterfall->saturation.drive, "machine"); return; }
                    if (device == Device::Limiter && name == "needlesDb" && std::isfinite (v.peakClipCutDb))
                    { workedLine (name, v.peakClipCutDb, "machine"); return; }
                }
                if (isOn && chain)
                {
                    bool active = truthOf (sounded);
                    const auto& t = rendered.topology;
                    const auto& v = rendered.params;
                    switch (device)
                    {
                        case Device::Hpf: active = t.eq && ! v.bypassEq && v.eqBands[0].on; break;
                        case Device::MonoBass: active = t.monoBass && ! v.bypassMonoBass; break;
                        case Device::Glue: active = t.compressor && ! v.bypassCompressor; break;
                        case Device::Saturation: active = t.clipper && ! v.bypassClipper; break;
                        case Device::Dither: active = t.dither && ! v.bypassDither; break;
                        case Device::Tilt: active = t.eq && ! v.bypassEq && v.eqBands[1].on; break;
                        case Device::Low: active = t.eq && ! v.bypassEq && v.eqBands[2].on; break;
                        case Device::Bands: active = active && t.eq && ! v.bypassEq; break;
                        case Device::Limiter: break;
                    }
                    if (active != truthOf (sounded)) origin = master.recipe.readyVersion == 1 ? "hand" : "machine";
                    workedLine (Of::fields[field], active, origin);
                }
                else workedLine (Of::fields[field], sounded, origin);
            }, settings, layers.machine, layers.hand, def);
        });
        // THE WATERFALL (MVP): per zone the share asked, the share reached and the dB taken; the limiter's asked is the rest.
        if (chain && master.report && master.report->waterfall)
        {
            const auto& w = *master.report->waterfall;
            text ("\n[waterfall]\n");
            const auto zone = [&] (std::string_view name, const MasterWaterfallZone& z)
            {
                const auto out = [&] (std::string_view what, const auto& n, std::string_view origin)
                { text (name); text (what); text (" = "); value (n); text (" # "); text (origin); put ('\n'); };
                if (z.asked) out ("Asked", *z.asked, "hand");
                if (z.reached) out ("Reached", *z.reached, "machine");
                if (z.db) out ("Db", *z.db, "machine");
                if (z.setting) out ("Setting", *z.setting, "machine");
                constexpr std::string_view stops[] = { "reached", "mixAtOne", "mixAtZero", "cutAtEnd", "cutAtZero", "comfortRed",
                                                       "notSounding", "passes", "rest", "noWish" };
                out ("Stop", stops[std::size_t (z.stop)], "machine");
            };
            zone ("glue", w.glue);
            zone ("saturation", w.saturation);
            zone ("cut", w.cut);
            zone ("limiter", w.limiter);
            if (w.totalDb) workedLine ("totalDb", *w.totalDb, "machine");
            workedLine ("extraPasses", w.extraPasses, "machine");
        }
        if (chain) renderState (rendered);
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
            if (! (rule.kind == detail::FieldRule::Kind::Oversampling ? rules.oversampling (out) : rules.slope (out)))
                in.refuse (key, unsigned (Rejection::NotOneOf));
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
    if (path == "target.loudnessMode.hand") answer.field = 2;
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
std::string_view WorkedText::view() const noexcept { return { data.get(), size }; }
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
Checked Session::exportWorkedBytes (MasterId id) const noexcept
{
    if (checkFloatingPointEnvironment() != Status::Ok) return { Rejection::FloatingPointEnvironment, kNoField, 0 };
    if (masterCount_ == 0) return { Rejection::UnknownMaster, kNoField, 0 };
    const Kept* const first = masters_.get();
    const Kept* const last = first + masterCount_;
    const Kept* const master = std::find_if (first, last, [id] (const Kept& k) { return k.id == id; });
    if (master == last || ! master->report) return { Rejection::UnknownMaster, kNoField, 0 };
    Writer w; w.worked (id, *master, masterRows_[std::size_t (master - masters_.get())].workedReady, detail::rules(), source_.channels);
    return { Rejection::None, kNoField, w.size };
}
Rejection Session::exportWorked (MasterId id, std::span<char> output) const noexcept
{
    const auto need = exportWorkedBytes (id);
    if (need.rejection != Rejection::None) return need.rejection;
    if (output.size() < need.bytes) return Rejection::TooLong;
    const Kept* const master = std::find_if (masters_.get(), masters_.get() + masterCount_,
        [id] (const Kept& k) { return k.id == id; });
    Writer w { output.data() }; w.worked (id, *master, masterRows_[std::size_t (master - masters_.get())].workedReady, detail::rules(), source_.channels);
    return Rejection::None;
}
WorkedText Session::exportWorked (MasterId id) const noexcept
{
    WorkedText out;
    const auto need = exportWorkedBytes (id);
    out.rejection = need.rejection;
    if (need.rejection != Rejection::None) return out;
    out.size = std::size_t (need.bytes);
    out.data.reset (new char[out.size]);
    (void) exportWorked (id, { out.data.get(), out.size });
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
            (void) t.table ("loudnessMode", Need::Optional, [&] (toml::Reader& layer)
            {
                std::string name;
                if (! layer.required ("hand", name)) return;
                if (name == "manual") out.project.targetEdit.loudnessMode = LoudnessMode::Manual;
                else if (name == "maxClean") out.project.targetEdit.loudnessMode = LoudnessMode::MaxClean;
                else if (name == "maxDense") out.project.targetEdit.loudnessMode = LoudnessMode::MaxDense;
                else if (name == "maxExtreme") out.project.targetEdit.loudnessMode = LoudnessMode::MaxExtreme;
                else layer.refuse ("hand", unsigned (Rejection::NotOneOf));
            });
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
    eachDevice (out.project.devices, [&] (Device device, auto& layers)
    {
        using Of = DeviceOf<std::remove_cvref_t<decltype (layers.machine)>>;
        const auto* section = root.find (Of::name);
        const auto* table = section ? std::get_if<toml::Table> (&section->data) : nullptr;
        Of::each (rules, [&] (std::uint8_t i, const FieldRule&, auto& machine, const auto& hand, const auto& current)
        {
            // The glue's five follow the amount by the law and are no decision: a machine value a file wrote for them (an
            // earlier build's numbers of its own input) gives way to the machine's for this source and is no difference.
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype (layers.machine)>, GlueFields<Value>>)
                if (i >= 3) machine = current;
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
