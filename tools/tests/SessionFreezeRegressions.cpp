// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// This probe also compiles against the pre-freeze public surface to demonstrate the regressions.
#include "fc_session_abi.h"
#include <felitronics/session/Config.h>
#include <felitronics/session/Wire.h>
#include <cstring>
#include <string>
using namespace felitronics::session;
int main (int argc, char** argv)
{
    if (argc != 2) return 2;
    const std::string item = argv[1];
    auto made = Session::create(); auto& s = *made.session;
    float samples[4] {}; const float* pcm[] { samples, samples };
    const command::Load load { 1, { pcm, 2, 4, 48000 }, {} };
    if (item == "F12")
    {
        TargetFields<Touched> fields; fields.lufs = -14.0 - 1.0 / 3.0;
        return s.apply (command::EditTarget { 1, fields }).rejection == Rejection::None ? 0 : 1;
    }
    if (item == "A")
    {
        const auto revision = s.revision(); const auto a = s.apply (command::EditTarget { 1, {} });
        return a.rejection == Rejection::None && a.revision == revision ? 0 : 1;
    }
    if (item == "B" || item == "C")
    {
        (void) s.apply (load); (void) s.step (10);
        HpfFields<Touched> fields; fields.fq = 100.25; fields.slope = 18;
        if (item == "C" && s.apply (command::EditDevice { 2, fields }).rejection != Rejection::ManualOff) return 1;
        (void) s.apply (command::SetManual { 3, true });
        if (s.apply (command::EditDevice { 4, fields }).rejection != Rejection::None) return 1;
        if (item == "B") return 0;
        LowShelfFields<Touched> shelf; shelf.db = 5.25;
        if (s.apply (command::EditDevice { 5, shelf }).rejection != Rejection::NotOffered) return 1;
        (void) s.apply (command::SetTarget { 6, "lp" });
        if (s.apply (command::EditDevice { 7, shelf }).rejection != Rejection::None) return 1;
        auto file = s.exportProject(); std::string project (file.view());
        const auto at = project.find ("defaults = "); project.replace (at, project.find ('\n', at) - at, "defaults = \"2020-01\"");
        if (s.importProject (8, project).rejection != Rejection::None || s.events().empty() || s.events()[0].kind != EventKind::Fact) return 1;
        const auto current = s.exportProject(); project = current.view(); project += "\n[hpf]\nfq.machine = 100.25\n";
        return s.importProject (9, project).rejection == Rejection::MachineMismatch ? 0 : 1;
    }
    if (item == "F5")
    {
        (void) s.apply (load); (void) s.step (1); const auto seq = s.events().back().seq;
        char answer[kAnswerBytes]; std::uint32_t written = 0;
        if (Wire::command (s, "{", answer, written) != CodecStatus::Ok) return 1;
        return s.events().size() == 1 && s.events()[0].kind == EventKind::Rejected && s.events()[0].seq == seq + 1 ? 0 : 1;
    }
    if (item == "F1" || item == "F2")
    {
#if defined (FC_SESSION_CAPABILITIES_V1_BYTES)
        const fc_session_capabilities caps { sizeof (caps), 9007199254740991.0, 48000, 255, 9007199254740991.0 };
        if (caps.size != FC_SESSION_CAPABILITIES_V1_BYTES) return 1;
        return item == "F1" || s.storageFor (load).bytes == 32 ? 0 : 1;
#else
        return 1; // No extensible record prefix or per-call demand entry points.
#endif
    }
    fc_session_capabilities caps {};
#if defined (FC_SESSION_CAPABILITIES_V1_BYTES)
    caps.size = sizeof (caps); caps.largestFreeBlockBytes = 9007199254740991.0;
#endif
    caps.heapCeilingBytes = 9007199254740991.0; caps.maxRateHz = 48000; caps.offeredDevices = 255;
    fc_session h = 0; const auto version = config::Config::versions().all;
    if (fc_session_create (&caps, std::uint32_t (version), std::uint32_t (version >> 32), &h) != FC_SESSION_OK) return 1;
    char answer[FC_SESSION_ANSWER_BYTES]; std::uint32_t written = 0;
    int result = 1;
    if (item == "F7")
    {
        alignas (8) double rows[1] {};
        result = fc_session_snapshot_copy (0, answer, 1, rows, 7) == FC_SESSION_ERR_ALIGNMENT ? 0 : 1;
    }
    if (item == "F8")
    {
        const auto empty = fc_session_export_project_size (h, &written);
        constexpr char meta[] = R"({"name":"","fileRate":48000,"bitDepth":24,"rateKnown":true})";
        (void) fc_session_load (h, 1, 0, pcm, 2, 4, 48000, meta, sizeof (meta) - 1, answer, sizeof (answer), &written);
        result = empty != fc_session_export_project_size (h, &written) ? 0 : 1;
    }
    (void) fc_session_destroy (h); return result;
}
