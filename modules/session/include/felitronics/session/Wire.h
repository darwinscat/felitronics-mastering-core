// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once
#include <felitronics/session/Snapshot.h>

namespace felitronics::session
{
inline constexpr std::uint32_t kCommandJsonBytes = 4096;
inline constexpr std::uint32_t kAnswerBytes = 32768;
inline constexpr std::uint32_t kQueryJsonBytes = 8192;
struct TransferNeed
{
    CodecStatus status = CodecStatus::Ok;
    std::uint32_t jsonBytes = 0;
    std::uint32_t rowBytes = 0;
};
// Allocation-free transfer. The same codec writes into caller-owned JSON and f64 storage.
// Point rows: [index, value]; run rows: [first, count, value]. Descriptors address the
// binary buffer in bytes, aligned to 8. Differences: [device,field,fileValue,coreValue].
// Non-finite row values retain IEEE-754 bits.
class Wire final
{
public:
    [[nodiscard]] static TransferNeed snapshotBytes (const SnapshotView& view) noexcept;
    [[nodiscard]] static CodecStatus snapshot (const SnapshotView& view, std::span<char> json, std::span<double> rows) noexcept;
    [[nodiscard]] static TransferNeed snapshotBytes (const Session& session) noexcept;
    [[nodiscard]] static CodecStatus snapshot (const Session& session, std::span<char> json, std::span<double> rows) noexcept;
    [[nodiscard]] static TransferNeed summaryBytes (const Session& session) noexcept;
    [[nodiscard]] static CodecStatus summary (const Session& session, std::span<char> json, std::span<double> rows) noexcept;
    [[nodiscard]] static CodecStatus queryRequest (std::string_view json, MeasurementQuery& out) noexcept;
    [[nodiscard]] static Checked queryStorage (const Session& session, std::string_view json) noexcept;
    // Bounds before execution, then actual written sizes. Refusals precede query/cache mutation.
    [[nodiscard]] static TransferNeed queryBuffers (const Session& session, std::string_view json) noexcept;
    [[nodiscard]] static TransferNeed query (Session& session, std::string_view request, std::span<char> json, std::span<double> rows) noexcept;
    [[nodiscard]] static TransferNeed queryBytes (const QueryView& response) noexcept;
    [[nodiscard]] static CodecStatus query (const QueryView& response, std::span<char> json, std::span<double> rows) noexcept;
    [[nodiscard]] static TransferNeed eventsBytes (std::span<const Notification> events) noexcept;
    [[nodiscard]] static CodecStatus events (std::span<const Notification> events, std::span<char> json, std::span<double> rows) noexcept;
    [[nodiscard]] static Checked commandStorage (const Session& session, std::string_view json) noexcept;
    [[nodiscard]] static Checked loadStorage (const Session& session, std::uint32_t channels, std::uint64_t frames,
                                              std::uint32_t rate, std::string_view meta) noexcept;
    [[nodiscard]] static Checked loadMeasuredStorage (const Session& session, std::string_view facts) noexcept;
    [[nodiscard]] static Checked attachAudioStorage (const Session& session, const Pcm& pcm) noexcept;
    [[nodiscard]] static Checked importStorage (const Session& session, std::string_view project) noexcept;
    // Contract or domain rejection is an answer, CodecStatus::Ok. A non-Ok status writes nothing.
    // The answer buffer must hold kAnswerBytes BEFORE parsing or applying a command.
    [[nodiscard]] static CodecStatus command (Session& session, std::string_view json, std::span<char> answer, std::uint32_t& written) noexcept;
    [[nodiscard]] static CodecStatus master (Session& session, const command::Master& request,
                                             std::span<char> answer, std::uint32_t& written) noexcept;
    [[nodiscard]] static CodecStatus load (Session& session, CommandId id, const Pcm& pcm, std::string_view meta,
                                         std::span<char> answer, std::uint32_t& written) noexcept;
    [[nodiscard]] static CodecStatus loadMeasured (Session& session, CommandId id, std::string_view facts,
                                                  std::span<char> answer, std::uint32_t& written) noexcept;
    [[nodiscard]] static CodecStatus attachAudio (Session& session, CommandId id, const Pcm& pcm,
                                                 std::span<char> answer, std::uint32_t& written) noexcept;
    [[nodiscard]] static CodecStatus importProject (Session& session, CommandId id, std::string_view project,
                                                  std::span<char> answer, std::uint32_t& written) noexcept;
};
}
