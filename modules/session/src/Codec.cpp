// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "BuildGuards.h"
#include "JsonCodec.h"
#include "JsonNumber.h"
#include "BuildContract.h"
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
using detail::Writer;
using detail::Storage;
using detail::Reader;
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
    if (sizes.differences) out.differences_.reset (new MachineDifference[sizes.differences]);
    if (sizes.eqPoints) out.eqCurve_.reset (new EqPoint[sizes.eqPoints]);
    Storage storage { 0, 0, 0, 0, 0, out.text_.get(), out.masters_.get(), out.points_.get(), out.runs_.get(), out.differences_.get(), 0, out.eqCurve_.get() };
    const bool filled = read (json, storage, out.view_);
    detail::debugBound (filled);
    output = std::move (out);
    return CodecStatus::Ok;
}
} // namespace felitronics::session
