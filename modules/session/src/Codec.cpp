// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "CodecSchema.h"
#include "JsonNumber.h"
#include "Utf8.h"
#include <felitronics/session/Snapshot.h>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace felitronics::session
{
namespace
{
// The same walk sizes and writes. It never allocates, including in a debug standard library.
struct Writer
{
    char* output = nullptr;
    std::size_t size = 0;
    bool good = true, first = true;
    void put (char c) noexcept { if (output) output[size] = c; ++size; }
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
    template <class T> void value (std::span<const T> rows) noexcept
    {
        put ('[');
        bool start = true;
        for (const T& row : rows) { if (! start) put (','); start = false; value (row); }
        put (']');
    }
    template <class T> void field (std::string_view name, const T& x) noexcept
    {
        if (! first) put (',');
        first = false;
        string (name); put (':'); value (x);
    }
};

struct Storage
{
    std::size_t chars = 0, masters = 0, points = 0, runs = 0;
    char* text = nullptr;
    Kept* kept = nullptr;
    ReadingPoint* point = nullptr;
    ReadingRun* run = nullptr;
    std::uint64_t bytes() const noexcept
    {
        return chars + masters * sizeof (Kept) + points * sizeof (ReadingPoint) + runs * sizeof (ReadingRun);
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
            seen = 0;
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
            if (seen != (std::uint64_t (1) << ordinal) - 1) good = false;
            seen = parentSeen; key = parentKey; ordinal = parentOrdinal; matched = parentMatched;
        }
    }
    template <class T> void value (std::optional<T>& x) noexcept
    {
        if (literal ("null")) x.reset();
        else { T v {}; value (v); x = v; }
    }
    template <class T> void value (std::span<const T>& rows) noexcept
    {
        std::size_t* count = nullptr; T* out = nullptr;
        if constexpr (std::is_same_v<T, Kept>) { count = &storage.masters; out = storage.kept; }
        else if constexpr (std::is_same_v<T, ReadingPoint>) { count = &storage.points; out = storage.point; }
        else { count = &storage.runs; out = storage.run; }
        const auto start = *count;
        expect ('[');
        if (! take (']'))
        {
            do { T row {}; value (row); if (good) { if (out) out[*count] = row; ++*count; } }
            while (good && take (','));
            expect (']');
        }
        if (out) rows = { out + start, *count - start };
    }
    template <class T> void field (std::string_view name, T& x) noexcept
    {
        const auto bit = std::uint64_t (1) << ordinal++;
        if (key != name || ! good) return;
        if ((seen & bit) != 0) { good = false; return; }
        seen |= bit; matched = true; value (x);
    }
};
bool valid (const SnapshotView& v) noexcept
{
    return std::isfinite (v.sourceBytes) && v.sourceBytes >= 0.0 && v.sourceBytes < 9007199254740992.0
        && std::bit_cast<std::uint64_t> (double (std::uint64_t (v.sourceBytes))) == std::bit_cast<std::uint64_t> (v.sourceBytes);
}
bool read (std::string_view json, Storage& storage, SnapshotView& view) noexcept
{
    if (! detail::validUtf8 (json)) return false;
    Reader reader { json, storage, 0, true, {}, 0, 0, false };
    reader.value (view); reader.space();
    return reader.good && reader.pos == json.size() && valid (view);
}
} // namespace

CodecNeed Codec::encodedBytes (const SnapshotView& view) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return { CodecStatus::FloatingPointEnvironment, 0 };
    if (! valid (view)) return { CodecStatus::Invalid, 0 };
    Writer writer; writer.value (view);
    return writer.good ? CodecNeed { CodecStatus::Ok, writer.size } : CodecNeed { CodecStatus::Invalid, 0 };
}
CodecStatus Codec::encode (const SnapshotView& view, std::span<char> output) noexcept
{
    const auto need = encodedBytes (view);
    if (need.status != CodecStatus::Ok) return need.status;
    if (output.size() < need.bytes) return CodecStatus::TooSmall;
    Writer writer { output.data() }; writer.value (view);
    return CodecStatus::Ok;
}
CodecNeed Codec::decodedBytes (std::string_view json) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return { CodecStatus::FloatingPointEnvironment, 0 };
    Storage storage; SnapshotView view;
    if (! read (json, storage, view)) return { CodecStatus::Invalid, 0 };
    return { CodecStatus::Ok, storage.bytes() };
}
CodecStatus Codec::decode (std::string_view json, Snapshot& output) noexcept
{
    if (Session::checkFloatingPointEnvironment() != Status::Ok) return CodecStatus::FloatingPointEnvironment;
    Storage sizes; SnapshotView view;
    if (! read (json, sizes, view)) return CodecStatus::Invalid;
    Snapshot out;
    if (sizes.chars) out.text_.reset (new char[sizes.chars]);
    if (sizes.masters) out.masters_.reset (new Kept[sizes.masters]);
    if (sizes.points) out.points_.reset (new ReadingPoint[sizes.points]);
    if (sizes.runs) out.runs_.reset (new ReadingRun[sizes.runs]);
    Storage storage { 0, 0, 0, 0, out.text_.get(), out.masters_.get(), out.points_.get(), out.runs_.get() };
    if (! read (json, storage, out.view_)) return CodecStatus::Invalid;
    output = std::move (out);
    return CodecStatus::Ok;
}
} // namespace felitronics::session
