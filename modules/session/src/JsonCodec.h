// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once
#include "CodecSchema.h"
#include "JsonNumber.h"
#include "TextFacts.h"
#include "Utf8.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace felitronics::session::detail
{
inline constexpr bool maskValue (double value) noexcept
{
    const auto bits = std::bit_cast<std::uint64_t> (value);
    return (bits & 0x7fffffffffffffffull) == 0u || bits == std::bit_cast<std::uint64_t> (1.0);
}
// WHAT THE TEXT WALK REFUSES OF A SNAPSHOT that the walk with binary rows does not: the thread's floating-point
// environment, the view's own invariants, and a machine difference's device out of range (text writes it as an enum,
// a binary row as a number). Every other check is the same in both walks. So a binary-row transfer is sized by this and
// one walk — never by printing every row as text first (Wire::snapshotBytes).
[[nodiscard]] CodecStatus snapshotEncodable (const SnapshotView& view) noexcept;
// The same walk sizes and writes. It never allocates, including in a debug standard library.
struct Writer
{
    char* output = nullptr;
    std::uint64_t size = 0;
    bool good = true, first = true;
    bool binaryRows = false;
    double* rows = nullptr;
    std::uint64_t rowSize = 0;
    void put (char c) noexcept { if (output) output[std::size_t (size)] = c; ++size; }
    void text (std::string_view s) noexcept { for (char c : s) put (c); }
    void string (std::string_view s) noexcept
    {
        if (! detail::validUtf8 (s)) good = false;
        constexpr char hex[] = "0123456789abcdef";
        put ('"');
        for (char c : s)
        {
            const auto byte = static_cast<unsigned char> (c);
            if (c == '"' || c == '\\') { put ('\\'); put (c); }
            else if (byte < 32) { text ("\\u00"); put (hex[byte >> 4]); put (hex[byte & 15]); }
            else put (c);
        }
        put ('"');
    }
    void value (std::string_view s) noexcept { string (s); }
    void value (bool b) noexcept { text (b ? "true" : "false"); }
    void value (double d) noexcept
    {
        if (! std::isfinite (d)) { string (std::isnan (d) ? "NaN" : d < 0 ? "-Infinity" : "Infinity"); return; }
        char buffer[detail::JsonNumber::capacity];
        text ({ buffer, detail::JsonNumber::write (d, buffer) });
    }
    template <class T> void value (const T& x) noexcept
    {
        if constexpr (std::is_enum_v<T>)
        {
            const auto n = static_cast<unsigned> (x);
            if (n > detail::enumLast<T>()) good = false;
            value (n);
        }
        else if constexpr (std::is_integral_v<T>)
        {
            if constexpr (std::is_same_v<T, std::uint64_t>) put ('"');
            std::uint64_t n = 0;
            if constexpr (std::is_signed_v<T>)
            {
                if (x < 0) { put ('-'); n = std::uint64_t (-std::int64_t (x)); }
                else n = std::uint64_t (x);
            }
            else n = x;
            char buffer[24]; std::size_t count = 0;
            do { buffer[count++] = char ('0' + n % 10); n /= 10; } while (n);
            while (count) put (buffer[--count]);
            if constexpr (std::is_same_v<T, std::uint64_t>) put ('"');
        }
        else
        {
            const bool parent = first;
            first = true;
            put ('{'); detail::describe (*this, x); put ('}');
            first = parent;
        }
    }
    template <class T> void value (const std::optional<T>& x) noexcept { if (x) value (*x); else text ("null"); }
    // A fact as every fact crosses the wire — an event's and a plan's alike: its id, and each argument whole.
    void value (const session::text::Fact& f) noexcept
    {
        if (f.argCount > session::text::Fact::kMaxArgs) { good = false; return; }
        const bool parent = first;
        put ('{'); first = true; field ("FactId", unsigned (f.id));
        text (",\"args\":[");
        for (unsigned i = 0; i < f.argCount; ++i)
        {
            if (i) put (',');
            const auto& a = f.args[i];
            put ('{'); first = true; field ("kind", unsigned (a.kind));
            field ("unit", unsigned (a.unit)); field ("precision", unsigned (a.precision));
            field ("sign", unsigned (a.sign)); field ("bound", unsigned (a.bound));
            field ("termId", unsigned (a.termId)); field ("number", a.number);
            // Signed count identities also cross JSON as decimal strings, without loss.
            text (",\"integer\":\"");
            if (a.integer < 0) put ('-');
            const auto magnitude = a.integer < 0 ? std::uint64_t (-(a.integer + 1)) + 1 : std::uint64_t (a.integer);
            Writer digits; char buf[24]; digits.output = buf; digits.value (magnitude);
            text ({ buf + 1, std::size_t (digits.size - 2) }); put ('"');
            field ("userText", a.userText); put ('}');
        }
        text ("]}");
        first = parent;
    }
    template <class T, std::size_t N> void value (const BoundedList<T, N>& list) noexcept
    {
        if (list.count > N) { good = false; return; }
        value (std::span<const T> (list.items.data(), list.count));
    }
    template <class T> void value (std::span<const T> values) noexcept
    {
        if constexpr (std::is_same_v<T, double> || std::is_same_v<T, EqPoint> || std::is_same_v<T, ReadingPoint> || std::is_same_v<T, ReadingRun> || std::is_same_v<T, MachineDifference>)
        {
            if (binaryRows)
            {
                const unsigned stride = std::is_same_v<T, double> ? 1u : (std::is_same_v<T, EqPoint> || std::is_same_v<T, ReadingPoint>) ? 2u : std::is_same_v<T, ReadingRun> ? 3u : 4u;
                const bool parent = first; first = true; put ('{');
                field ("byteOffset", double (rowSize * sizeof (double)));
                field ("length", double (values.size())); field ("stride", stride); put ('}'); first = parent;
                const auto append = [&] (double v) { if (this->rows) this->rows[std::size_t (rowSize)] = v; ++rowSize; };
                const auto index = [&] (std::uint64_t v)
                {
                    if (v >= 9007199254740992ull) good = false;
                    append (double (v));
                };
                for (const T& row : values)
                {
                    if constexpr (std::is_same_v<T, double>) append (row);
                    else if constexpr (std::is_same_v<T, MachineDifference>)
                    {
                        append (double (row.device)); append (double (row.field)); append (row.fileValue); append (row.coreValue);
                    }
                    else if constexpr (std::is_same_v<T, EqPoint>) { append (row.hz); append (row.db); }
                    else
                    {
                        if constexpr (std::is_same_v<T, ReadingPoint>) index (row.index);
                        else { index (row.first); index (row.count); }
                        append (row.value);
                    }
                }
                return;
            }
        }
        put ('[');
        bool start = true;
        for (const T& row : values) { if (! start) put (','); start = false; value (row); }
        put (']');
    }
    template <class T, class D> void optionalField (std::string_view name, const T& x, const D&) noexcept { field (name, x); }
    template <class T> void field (std::string_view name, const T& x) noexcept
    {
        if (! first) put (',');
        first = false;
        string (name); put (':'); value (x);
    }
};

struct Storage
{
    std::size_t chars = 0, masters = 0, points = 0, runs = 0, differences = 0;
    char* text = nullptr;
    Kept* kept = nullptr;
    ReadingPoint* point = nullptr;
    ReadingRun* run = nullptr;
    MachineDifference* difference = nullptr;
    std::size_t eqPoints = 0;
    EqPoint* eqPoint = nullptr;
    std::size_t measurementResults = 0, measurementNumbers = 0, measurementArrays = 0, measurementRows = 0;
    MeasurementResult* measurementResult = nullptr;
    MeasurementValue* measurementNumber = nullptr;
    MeasurementArray* measurementArray = nullptr;
    double* measurementRow = nullptr;
    std::size_t landingPasses = 0;
    LandingPass* landingPass = nullptr;
    std::size_t landingTraceRows = 0;
    LandingTraceBucket* landingTraceRow = nullptr;
    std::size_t masterCrestRows = 0;
    double* masterCrestRow = nullptr;
    std::size_t masterSections = 0, masterWaveformRows = 0;
    MasterSection* masterSection = nullptr;
    MasterWaveformBucket* masterWaveformRow = nullptr;
    std::uint64_t bytes() const noexcept
    {
        return std::uint64_t (measurementResults) * sizeof (MeasurementResult)
             + std::uint64_t (measurementNumbers) * sizeof (MeasurementValue)
             + std::uint64_t (measurementArrays) * sizeof (MeasurementArray)
             + std::uint64_t (measurementRows) * sizeof (double) + chars + masters * sizeof (Kept) + points * sizeof (ReadingPoint) + runs * sizeof (ReadingRun) + differences * sizeof (MachineDifference) + eqPoints * sizeof (EqPoint)
             + std::uint64_t (landingPasses) * sizeof (LandingPass)
             + std::uint64_t (landingTraceRows) * sizeof (LandingTraceBucket)
             + std::uint64_t (masterCrestRows) * sizeof (double)
             + std::uint64_t (masterSections) * sizeof (MasterSection)
             + std::uint64_t (masterWaveformRows) * sizeof (MasterWaveformBucket);
    }
};

struct Reader
{
    std::string_view input;
    Storage& storage;
    std::size_t pos = 0;
    bool good = true;
    std::string_view key;
    std::uint64_t seen = 0;
    unsigned ordinal = 0;
    bool matched = false;
    std::uint64_t optionalMask = 0;
    bool masterCrestContext = false;
    std::size_t crestBandCount = 0, crestMaskCount = 0;
    std::uint64_t traceSamples = 0, traceNonFinite = 0;
    std::uint64_t costSectionEnd = 0, costWaveEnd = 0, costWaveFrom = 0, costWaveTo = 0;
    std::size_t costWaveRows = 0;
    // A master read without its heavy rows, and one read with some: a snapshot says which it carries (masterRowsIncluded),
    // and is held to it when its last field is in.
    bool leanMasterRead = false, wholeMasterRead = false;
    unsigned costWaveChannels = 0;

    void space() noexcept { while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\n' || input[pos] == '\r' || input[pos] == '\t')) ++pos; }
    bool take (char c) noexcept { space(); if (pos < input.size() && input[pos] == c) { ++pos; return true; } return false; }
    void expect (char c) noexcept { if (! take (c)) good = false; }
    bool literal (std::string_view s) noexcept
    {
        space();
        if (input.substr (pos, s.size()) != s) return false;
        pos += s.size(); return true;
    }
    unsigned hex4() noexcept
    {
        unsigned n = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            if (pos == input.size()) { good = false; return 0; }
            const char c = input[pos++];
            const unsigned digit = c >= '0' && c <= '9' ? unsigned (c - '0')
                : c >= 'a' && c <= 'f' ? unsigned (c - 'a' + 10) : c >= 'A' && c <= 'F' ? unsigned (c - 'A' + 10) : 16;
            if (digit == 16) good = false;
            n = n * 16 + digit;
        }
        return n;
    }
    // Reads a JSON string into an optional exact buffer. Keys use a bounded local buffer.
    std::size_t string (char* out, std::size_t capacity) noexcept
    {
        expect ('"');
        std::size_t n = 0;
        const auto byte = [&] (char c)
        {
            if (out) { if (n < capacity) out[n] = c; else good = false; }
            ++n;
        };
        while (good && pos < input.size())
        {
            char c = input[pos++];
            if (c == '"') return n;
            if (static_cast<unsigned char> (c) < 32) { good = false; break; }
            if (c != '\\') { byte (c); continue; }
            if (pos == input.size()) break;
            c = input[pos++];
            switch (c)
            {
                case '"': case '\\': case '/': byte (c); break;
                case 'b': byte ('\b'); break;
                case 'f': byte ('\f'); break;
                case 'n': byte ('\n'); break;
                case 'r': byte ('\r'); break;
                case 't': byte ('\t'); break;
                case 'u':
                {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF)
                    {
                        if (input.substr (pos, 2) != "\\u") { good = false; break; }
                        pos += 2;
                        const unsigned lo = hex4();
                        if (lo < 0xDC00 || lo > 0xDFFF) { good = false; break; }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + lo - 0xDC00;
                    }
                    else if (cp >= 0xDC00 && cp <= 0xDFFF) { good = false; break; }
                    if (cp < 0x80) byte (char (cp));
                    else
                    {
                        if (cp >= 0x10000) { byte (char (0xF0 | (cp >> 18))); byte (char (0x80 | ((cp >> 12) & 63))); }
                        else if (cp >= 0x800) byte (char (0xE0 | (cp >> 12)));
                        else byte (char (0xC0 | (cp >> 6)));
                        if (cp >= 0x800) byte (char (0x80 | ((cp >> 6) & 63)));
                        byte (char (0x80 | (cp & 63)));
                    }
                    break;
                }
                default: good = false; break;
            }
        }
        good = false; return n;
    }
    std::string_view number() noexcept
    {
        space(); const auto start = pos;
        if (pos < input.size() && input[pos] == '-') ++pos;
        if (pos == input.size()) { good = false; return {}; }
        if (input[pos] == '0') ++pos;
        else
        {
            if (input[pos] < '1' || input[pos] > '9') { good = false; return {}; }
            while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9') ++pos;
        }
        if (pos < input.size() && input[pos] == '.')
        {
            const auto begin = ++pos;
            while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9') ++pos;
            if (begin == pos) good = false;
        }
        if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E'))
        {
            ++pos;
            if (pos < input.size() && (input[pos] == '+' || input[pos] == '-')) ++pos;
            const auto begin = pos;
            while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9') ++pos;
            if (begin == pos) good = false;
        }
        if (pos - start > detail::JsonNumber::capacity) good = false;
        return input.substr (start, pos - start);
    }
    void value (std::string_view& x) noexcept
    {
        char* out = storage.text ? storage.text + storage.chars : nullptr;
        const auto n = string (out, input.size());
        storage.chars += n;
        if (out) x = { out, n };
    }
    void value (bool& x) noexcept { if (literal ("true")) x = true; else if (literal ("false")) x = false; else good = false; }
    void value (double& x) noexcept
    {
        space();
        if (pos < input.size() && input[pos] == '"')
        {
            char word[10]; const auto n = string (word, sizeof (word));
            if (! good) return;
            const std::string_view s (word, n);
            if (s == "NaN") x = std::numeric_limits<double>::quiet_NaN();
            else if (s == "-Infinity") x = -std::numeric_limits<double>::infinity();
            else if (s == "Infinity") x = std::numeric_limits<double>::infinity();
            else good = false;
            return;
        }
        const auto n = number();
        if (! good) return;
        if (! detail::JsonNumber::read (n, x)) good = false;
    }
    template <class T> void value (T& x) noexcept
    {
        if constexpr (std::is_enum_v<T>)
        {
            unsigned n = 0; value (n);
            if (n > detail::enumLast<T>()) good = false;
            else x = static_cast<T> (n);
        }
        else if constexpr (std::is_integral_v<T>)
        {
            if constexpr (std::is_same_v<T, std::uint64_t>) expect ('"');
            const auto n = number();
            if (! good) return;
            const bool negative = ! n.empty() && n[0] == '-';
            std::uint64_t limit = std::uint64_t (std::numeric_limits<T>::max());
            if constexpr (std::is_signed_v<T>) { if (negative) ++limit; }
            else if (negative) good = false;
            std::uint64_t integer = 0;
            for (std::size_t i = negative ? 1u : 0u; good && i < n.size(); ++i)
            {
                const auto digit = unsigned (n[i] - '0');
                if (digit > 9 || integer > (limit - digit) / 10) good = false;
                else integer = integer * 10 + digit;
            }
            if (good)
            {
                if constexpr (std::is_signed_v<T>) x = static_cast<T> (negative ? -std::int64_t (integer) : std::int64_t (integer));
                else x = static_cast<T> (integer);
            }
            if constexpr (std::is_same_v<T, std::uint64_t>) expect ('"');
        }
        else
        {
            const auto parentSeen = seen; const auto parentKey = key;
            const auto parentOrdinal = ordinal; const auto parentMatched = matched;
            const auto parentOptional = optionalMask;
            const auto parentMasterCrest = masterCrestContext;
            const auto parentCrestBands = crestBandCount, parentCrestMask = crestMaskCount;
            [[maybe_unused]] const auto parentTraceSamples = traceSamples, parentTraceNonFinite = traceNonFinite;
            const auto beforeRows = storage.measurementRows, beforeChars = storage.chars;
            const auto beforeNumbers = storage.measurementNumbers, beforeArrays = storage.measurementArrays;
            const auto beforePasses = storage.landingPasses, beforeTraceRows = storage.landingTraceRows;
            const auto beforeCrestRows = storage.masterCrestRows;
            const auto beforeSections = storage.masterSections, beforeWaveform = storage.masterWaveformRows;
            seen = 0; optionalMask = 0;
            if constexpr (std::is_same_v<T, MasterCrest>)
            { masterCrestContext = true; crestBandCount = crestMaskCount = 0; }
            if constexpr (std::is_same_v<T, LandingTrace>) { traceSamples = 0; traceNonFinite = 0; }
            if constexpr (std::is_same_v<T, MasterCost>)
            {
                costSectionEnd = costWaveEnd = costWaveFrom = costWaveTo = 0;
                costWaveRows = 0; costWaveChannels = 0;
            }
            expect ('{');
            do
            {
                char name[64]; const auto n = string (name, sizeof (name));
                if (! good) break;
                key = { name, n }; expect (':'); matched = false; ordinal = 0;
                detail::describe (*this, x);
                if (! matched) good = false;
            } while (good && take (','));
            expect ('}');
            if ((seen | optionalMask) != (std::uint64_t (1) << ordinal) - 1) good = false;
            if constexpr (std::is_same_v<T, MeasurementValue>)
            {
                if (storage.chars == beforeChars || storage.chars - beforeChars > kMeasurementNameBytes) good = false;
                if (x.value ? x.reason != MeasurementReason::None : x.reason == MeasurementReason::None) good = false;
            }
            else if constexpr (std::is_same_v<T, MeasurementArray>)
            {
                if (storage.chars == beforeChars || storage.chars - beforeChars > kMeasurementNameBytes) good = false;
                const auto elements = storage.measurementRows - beforeRows;
                if (x.columns == 0 || elements % x.columns != 0 || x.stored != elements / x.columns
                    || x.stored > x.total || (x.complete && x.stored != x.total)) good = false;
            }
            else if constexpr (std::is_same_v<T, MeasurementResult>)
            {
                if (storage.measurementNumbers - beforeNumbers > kMeasurementNumbers
                    || storage.measurementArrays - beforeArrays > kMeasurementArrays
                    || x.stored > x.total || (x.complete && x.stored != x.total)
                    || (x.status == MeasurementStatus::Ready ? x.reason != MeasurementReason::None : x.reason == MeasurementReason::None)) good = false;
            }
            else if constexpr (std::is_same_v<T, LandingSummary>)
            {
                if (storage.landingPasses - beforePasses > 12
                    || x.passes != storage.landingPasses - beforePasses) good = false;
                // A named limit belongs to an unreachable landing; the two levels to a between one, both, in order.
                if (x.binding != LandingConstraint::None && x.status != LandingStatus::TargetUnreachable) good = false;
                // Peaks above the ceiling mark a delivered landing that no render under the ceiling held.
                if (x.peaksAboveCeiling && (! x.deliverable || x.status != LandingStatus::TargetUnreachable
                    || x.binding != LandingConstraint::TruePeakCeiling)) good = false;
                if ((x.belowLufs || x.aboveLufs)
                    && (x.status != LandingStatus::TargetBetweenAchievable || ! x.belowLufs || ! x.aboveLufs
                        || ! std::isfinite (*x.belowLufs) || ! std::isfinite (*x.aboveLufs) || *x.belowLufs > *x.aboveLufs)) good = false;
            }
            else if constexpr (std::is_same_v<T, LandingTrace>)
            {
                wholeMasterRead = true;
                if (x.columns != storage.landingTraceRows - beforeTraceRows
                    || x.columns > 65536 || x.fromFrame > x.toFrame
                    || (x.columns != 0 && x.sampleRateHz == 0)
                    || (x.valid && (! x.complete || x.nonFinite != 0 || x.samples == 0))
                    || x.nonFinite > x.samples
                    || (x.complete && (traceSamples != x.samples || traceNonFinite != x.nonFinite))) good = false;
            }
            else if constexpr (std::is_same_v<T, LandingTraceBucket>)
            {
                if (x.nonFinite > x.samples || ! std::isfinite (x.minDb) || ! std::isfinite (x.maxDb)
                    || ! std::isfinite (x.meanDb) || x.minDb < 0.0 || x.maxDb < x.minDb
                    || x.meanDb < 0.0) good = false;
                if (x.samples > std::numeric_limits<std::uint64_t>::max() - traceSamples
                    || x.nonFinite > std::numeric_limits<std::uint64_t>::max() - traceNonFinite) good = false;
                else { traceSamples += x.samples; traceNonFinite += x.nonFinite; }
            }
            else if constexpr (std::is_same_v<T, MasterCrest>)
            {
                const auto count = storage.masterCrestRows - beforeCrestRows;
                if (x.version != 1 || (x.blocks != 0 && (x.sampleRateHz == 0 || x.hopFrames == 0 || x.blockHops == 0))
                    || ! std::isfinite (x.edgeLowHz) || ! std::isfinite (x.edgeMidHz)
                    || ! std::isfinite (x.edgeHighHz) || x.edgeLowHz < 0
                    || x.edgeMidHz < x.edgeLowHz || x.edgeHighHz < x.edgeMidHz
                    // Rows and mask both absent is a lean summary's crest; the snapshot's own check holds it to its flag.
                    || (count != 0 && (x.blocks > crestBandCount / 10u || crestBandCount != x.blocks * 10u
                        || crestMaskCount != (x.status == MeasurementStatus::Ready ? x.blocks * 5u : 0u)))
                    || count != crestBandCount + crestMaskCount
                    || (x.status == MeasurementStatus::Ready ? x.reason != MeasurementReason::None || ! x.complete
                        : x.reason == MeasurementReason::None)) good = false;
                if (count != 0) wholeMasterRead = true; else if (x.blocks != 0) leanMasterRead = true;
            }
            else if constexpr (std::is_same_v<T, MasterReport>)
            {
                if (! std::isfinite (x.targetLufs) || ! std::isfinite (x.ceilingDbTp)
                    || x.checkPasses > 1
                    || (x.status == MeasurementStatus::Ready
                        && (! x.achievedLufs || ! x.truePeakDbTp || ! x.missLu || ! x.gainFromSourceDb))
                    // Delivered: under the ceiling, or above it and marked (no render stayed under it).
                    || (x.deliverable && (x.status != MeasurementStatus::Ready || ! (x.peakSafe || x.peaksAboveCeiling)))
                    || (x.peakSafe && (! x.truePeakDbTp || *x.truePeakDbTp > x.ceilingDbTp))
                    || (x.peaksAboveCeiling && (x.peakSafe || ! x.deliverable || x.targetMet || ! x.truePeakDbTp
                        || *x.truePeakDbTp <= x.ceilingDbTp))
                    || (x.crest.status == MeasurementStatus::Ready
                        && x.checkPasses != (x.crest.sourceRateCheck ? 1u : 0u))
                    || (x.status == MeasurementStatus::Ready ? x.reason != MeasurementReason::None
                        : x.reason == MeasurementReason::None)
                    || (x.lraLu ? x.lraReason != MeasurementReason::None
                        : x.lraReason == MeasurementReason::None)
                    || (x.plrDb ? x.plrReason != MeasurementReason::None
                        : x.plrReason == MeasurementReason::None)) good = false;
                for (const auto& value : { x.achievedLufs, x.truePeakDbTp, x.lraLu,
                                           x.plrDb, x.gainFromSourceDb, x.missLu })
                    if (value && ! std::isfinite (*value)) good = false;
            }
            else if constexpr (std::is_same_v<T, MasterCostValue>)
            {
                if (x.value ? x.reason != MeasurementReason::None || ! std::isfinite (*x.value)
                    : x.reason == MeasurementReason::None) good = false;
            }
            else if constexpr (std::is_same_v<T, MasterCost>)
            {
                if (storage.masterSections - beforeSections > 1000000u
                    || storage.masterWaveformRows - beforeWaveform > 4096u
                    || x.k2Reason != MeasurementReason::NotImplemented
                    || x.sourceRateHz == 0 || x.masterRateHz == 0
                    || x.sourceFrames == 0 || x.masterFrames == 0
                    // No bucket at all is a lean summary's cost; the snapshot is held to its flag.
                    || costSectionEnd > x.sourceFrames
                    || (costWaveRows != 0 && (costWaveEnd != x.masterFrames
                        || (costWaveChannels == 2 && costWaveRows % 2u != 0)))
                    || (x.worstSectionIndex && *x.worstSectionIndex >= storage.masterSections - beforeSections)) good = false;
                (costWaveRows != 0 ? wholeMasterRead : leanMasterRead) = true;
            }
            else if constexpr (std::is_same_v<T, MasterSection>)
            {
                if (x.toFrame <= x.fromFrame || ! std::isfinite (x.sourceLufs)
                    || ! std::isfinite (x.masterLufs) || ! std::isfinite (x.shiftLu)
                    || x.fromFrame < costSectionEnd) good = false;
                costSectionEnd = x.toFrame;
            }
            else if constexpr (std::is_same_v<T, MasterWaveformBucket>)
            {
                if (x.toFrame <= x.fromFrame || x.channel > 1 || x.finite > x.toFrame - x.fromFrame
                    || ! std::isfinite (x.minimum) || ! std::isfinite (x.maximum)
                    || ! std::isfinite (x.rms) || x.minimum > x.maximum || x.rms < 0) good = false;
                if (costWaveRows == 0)
                {
                    if (x.channel != 0 || x.fromFrame != 0) good = false;
                }
                else if (costWaveRows == 1)
                {
                    costWaveChannels = x.channel == 1 ? 2u : 1u;
                    if (x.channel == 1 ? x.fromFrame != costWaveFrom || x.toFrame != costWaveTo
                        : x.fromFrame != costWaveEnd) good = false;
                }
                else if (costWaveChannels == 1)
                {
                    if (x.channel != 0 || x.fromFrame != costWaveEnd) good = false;
                }
                else if (costWaveRows % 2u == 0)
                {
                    if (x.channel != 0 || x.fromFrame != costWaveEnd) good = false;
                }
                else if (x.channel != 1 || x.fromFrame != costWaveFrom || x.toFrame != costWaveTo)
                    good = false;
                if (x.channel == 0)
                { costWaveFrom = x.fromFrame; costWaveTo = x.toFrame; costWaveEnd = x.toFrame; }
                ++costWaveRows;
            }
            else if constexpr (std::is_same_v<T, MasterHint>)
            {
                if (x.reason == LandingReason::None || ! std::isfinite (x.evidence)
                    || x.percent != (x.reason == LandingReason::ExcessSubBass
                                  || x.reason == LandingReason::DarkMix)) good = false;
            }
            else if constexpr (std::is_same_v<T, Kept>)
            {
                if (x.report && (! x.landing || x.report->deliverable != x.landing->deliverable
                    || x.report->peaksAboveCeiling != x.landing->peaksAboveCeiling
                    || x.report->targetMet != (x.landing->status == LandingStatus::Solved))) good = false;
            }
            else if constexpr (std::is_same_v<T, SnapshotView>)
            {
                // A whole snapshot has every master's rows; a lean summary none of the heavy ones.
                if (x.masterRowsIncluded ? leanMasterRead : wholeMasterRead) good = false;
            }
            optionalMask = parentOptional;
            masterCrestContext = parentMasterCrest;
            crestBandCount = parentCrestBands; crestMaskCount = parentCrestMask;
            if constexpr (std::is_same_v<T, LandingTrace>)
                { traceSamples = parentTraceSamples; traceNonFinite = parentTraceNonFinite; }
            seen = parentSeen; key = parentKey; ordinal = parentOrdinal; matched = parentMatched;
        }
    }
    template <class T> void value (std::optional<T>& x) noexcept
    {
        if (literal ("null")) x.reset();
        else { T v {}; value (v); x = v; }
    }
    // A fact, read as the writer writes it: a known id, at most kMaxArgs arguments, every field once. A plan's facts carry
    // no person's text, and a snapshot's storage holds none for them: a text is refused.
    void value (session::text::Fact& f) noexcept
    {
        f = {};
        bool id = false, args = false;
        expect ('{');
        do
        {
            char name[16]; const auto n = string (name, sizeof (name));
            if (! good) return;
            const std::string_view k (name, n);
            expect (':');
            if (k == "FactId" && ! id)
            {
                std::uint16_t raw = 0; value (raw); id = true;
                f.id = session::text::FactId (raw);
                if (session::text::detail::shapeOf (f.id) == nullptr) good = false;
            }
            else if (k == "args" && ! args)
            {
                args = true;
                expect ('[');
                if (! take (']'))
                {
                    do
                    {
                        if (f.argCount == session::text::Fact::kMaxArgs) { good = false; return; }
                        value (f.args[f.argCount++]);
                    }
                    while (good && take (','));
                    expect (']');
                }
            }
            else good = false;
        }
        while (good && take (','));
        expect ('}');
        if (! id || ! args) good = false;
    }
    void value (session::text::Arg& a) noexcept
    {
        namespace t = session::text;
        a = {};
        constexpr std::string_view keys[] { "kind", "unit", "precision", "sign", "bound", "termId", "number", "integer", "userText" };
        unsigned fields = 0;
        expect ('{');
        do
        {
            char name[16]; const auto n = string (name, sizeof (name));
            if (! good) return;
            unsigned k = 0;
            while (k < std::size (keys) && keys[k] != std::string_view (name, n)) ++k;
            if (k == std::size (keys) || (fields & (1u << k)) != 0) { good = false; return; }
            fields |= 1u << k;
            expect (':');
            std::uint16_t small = 0;
            const auto below = [&] (unsigned limit) { value (small); if (small >= limit) good = false; };
            switch (k)
            {
                case 0: below (unsigned (t::ArgKind::UserText) + 1u); a.kind = t::ArgKind (small); break;
                case 1: below (unsigned (t::kUnitCount)); a.unit = t::Unit (small); break;
                case 2: below (10u); a.precision = std::uint8_t (small); break;
                case 3: below (unsigned (t::Sign::Always) + 1u); a.sign = t::Sign (small); break;
                case 4: below (unsigned (t::Bound::AtMost) + 1u); a.bound = t::Bound (small); break;
                case 5: value (small); a.termId = t::Term (small); break;
                case 6: value (a.number); break;
                case 7: expect ('"'); value (a.integer); expect ('"'); break;
                default: { char none[1]; if (string (none, 0) != 0) good = false; break; }
            }
        }
        while (good && take (','));
        expect ('}');
        if (fields != (1u << std::size (keys)) - 1u
            || (a.kind == t::ArgKind::Term && t::detail::shapeOf (a.termId) == nullptr)) good = false;
    }
    template <class T, std::size_t N> void value (BoundedList<T, N>& list) noexcept
    {
        list = {};
        expect ('[');
        if (take (']')) return;
        do
        {
            if (list.count == N) { good = false; return; }
            value (list.items[list.count++]);
        }
        while (good && take (','));
        expect (']');
    }
    template <class T> void value (std::span<const T>& rows) noexcept
    {
        std::size_t* count = nullptr; T* out = nullptr;
        if constexpr (std::is_same_v<T, Kept>) { count = &storage.masters; out = storage.kept; }
        else if constexpr (std::is_same_v<T, LandingPass>) { count = &storage.landingPasses; out = storage.landingPass; }
        else if constexpr (std::is_same_v<T, LandingTraceBucket>) { count = &storage.landingTraceRows; out = storage.landingTraceRow; }
        else if constexpr (std::is_same_v<T, MasterSection>) { count = &storage.masterSections; out = storage.masterSection; }
        else if constexpr (std::is_same_v<T, MasterWaveformBucket>) { count = &storage.masterWaveformRows; out = storage.masterWaveformRow; }
        else if constexpr (std::is_same_v<T, ReadingPoint>) { count = &storage.points; out = storage.point; }
        else if constexpr (std::is_same_v<T, ReadingRun>) { count = &storage.runs; out = storage.run; }
        else if constexpr (std::is_same_v<T, EqPoint>) { count = &storage.eqPoints; out = storage.eqPoint; }
        else if constexpr (std::is_same_v<T, MeasurementResult>) { count = &storage.measurementResults; out = storage.measurementResult; }
        else if constexpr (std::is_same_v<T, MeasurementValue>) { count = &storage.measurementNumbers; out = storage.measurementNumber; }
        else if constexpr (std::is_same_v<T, MeasurementArray>) { count = &storage.measurementArrays; out = storage.measurementArray; }
        else if constexpr (std::is_same_v<T, double>)
        {
            count = masterCrestContext ? &storage.masterCrestRows : &storage.measurementRows;
            out = masterCrestContext ? storage.masterCrestRow : storage.measurementRow;
        }
        else { count = &storage.differences; out = storage.difference; }
        const auto start = *count;
        std::uint32_t analyzers = 0;
        expect ('[');
        if (! take (']'))
        {
            do
            {
                T row {}; value (row);
                if constexpr (std::is_same_v<T, double>)
                    if (masterCrestContext && (! std::isfinite (row)
                        || (key == "rows" && row < 0.0)
                        || (key == "sourceMask" && ! maskValue (row)))) good = false;
                if constexpr (std::is_same_v<T, MeasurementResult>)
                {
                    const auto bit = 1u << unsigned (row.analyzer);
                    if ((analyzers & bit) != 0 || *count - start == kAnalyzers) good = false;
                    analyzers |= bit;
                }
                if (good) { if (out) out[*count] = row; ++*count; }
            }
            while (good && take (','));
            expect (']');
        }
        if (out) rows = { out + start, *count - start };
        if constexpr (std::is_same_v<T, double>) if (masterCrestContext)
        {
            if (key == "rows") crestBandCount = *count - start;
            else if (key == "sourceMask") crestMaskCount = *count - start;
        }
    }
    template <class T, class D> void optionalField (std::string_view name, T& x, const D& defaultValue) noexcept
    {
        const auto bit = std::uint64_t (1) << ordinal;
        optionalMask |= bit;
        if ((seen & bit) == 0) x = defaultValue;
        field (name, x);
    }
    template <class T> void field (std::string_view name, T& x) noexcept
    {
        const auto bit = std::uint64_t (1) << ordinal++;
        if (key != name || ! good) return;
        if ((seen & bit) != 0) { good = false; return; }
        seen |= bit; matched = true; value (x);
    }
};
}
