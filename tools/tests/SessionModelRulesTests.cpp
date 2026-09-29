// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
// Uses the pre-freeze public surface too: each scenario runs red on 31a9b1c.
#include "../../modules/session/tests/Advance.h"
#include <felitronics/session/Wire.h>
#include <felitronics/session/Config.h>
#include <cstdio>
#include <string>
using namespace felitronics::session;
namespace {
int failures = 0;
void require (bool yes, const char* message) { if (!yes) { std::printf ("FAIL: %s\n", message); ++failures; } }
std::string snapshot (Session& s) {
    const auto v = s.snapshot(); const auto n = Codec::encodedBytes (v.view());
    std::string out (std::size_t (n.bytes), '\0');
    require (Codec::encode (v.view(), out) == CodecStatus::Ok, "snapshot encodes"); return out;
}
}
int main (int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string item = argv[1];
    auto made = Session::create(); auto& s = *made.session;
    float data[4] {}; const float* pcm[] { data, data };
    require (s.apply (command::Load { 1, { pcm, 2, 4, 48000 }, {} }).rejection == Rejection::None, "load");
    testing::measure (s);
    if (item == "M1") {
        HpfFields<Touched> f; f.fq = 36; f.on = false;
        require (s.apply (command::EditDevice { 2, f }).rejection == Rejection::None, "hidden panel accepts edits");
        (void) s.apply (command::SetManual { 3, true });
        require (s.apply (command::EditDevice { 4, f }).rejection == Rejection::None, "shown panel accepts edits");
        (void) s.apply (command::SetManual { 5, false });
        require (s.project().devices.hpf.hand.fq == 36 && s.project().devices.hpf.hand.on == false, "hiding preserves touched values including false");
        require (snapshot (s).find ("\"handFieldCount\":2") != std::string::npos, "snapshot counts touched device fields");
        const auto file = s.exportProject();
        require (file.view().find ("manual = false") != std::string_view::npos && file.view().find ("fq.hand = 36") != std::string_view::npos, "hidden hand exports");
        require (s.importProject (6, file.view()).rejection == Rejection::None, "hidden hand imports");
        HpfFields<Mark> mask; mask.fq = true;
        require (s.apply (command::RevertEdits { 7, mask }).rejection == Rejection::None, "hidden panel accepts revert");
        require (snapshot (s).find ("\"handFieldCount\":1") != std::string::npos, "revert counts remaining false field");
        (void) s.apply (command::SetTarget { 8, "lp" });
        require (! s.project().devices.hpf.hand.on && snapshot (s).find ("\"handFieldCount\":0") != std::string::npos,
                 "target change resets hidden false edit and count");
        (void) s.apply (command::SetTarget { 9, "allStreaming" });
        require (snapshot (s).find ("\"handFieldCount\":0") != std::string::npos, "target reset clears count");
    } else if (item == "M2") {
        const auto base = s.exportProject();
        const std::string edited = std::string (base.view()) + "\n[hpf]\nfq.machine = 36\n";
        require (s.importProject (2, edited).rejection == Rejection::None, "same-core machine edit imports");
        require (s.project().devices.hpf.machine.fq == 36, "file machine wins");
        require (s.snapshot().view().machineDifferences.size() == 1, "same-core difference row");
        require (s.events().size() == 1 && s.events()[0].kind == EventKind::Fact && s.events()[0].payload.fact.view().args[0].integer == 1, "same-core count fact");
        require (s.exportProject().view() == edited, "same-core layer round trips");
        if (! s.events().empty()) {
            const auto fact = s.events()[0].payload.fact.view();
            const auto en = text::Text::text (fact, text::Lang::En);
            const auto ru = text::Text::text (fact, text::Lang::Ru);
            require (en.find ("same core") != std::string::npos && en.find ("edited by hand") != std::string::npos
                && !ru.empty() && ru.find ('{') == std::string::npos, "same-core fact explains hand-edited files in both languages");
        }
        std::string foreign = edited; const auto at = foreign.find ("core = ");
        foreign.replace (at, foreign.find ('\n', at) - at, "core = \"0.0.1\"");
        require (s.importProject (3, foreign).rejection == Rejection::None && s.exportProject().view() == foreign, "foreign machine and core stamp round trip");
    } else if (item == "M3") {
        const auto config = config::Config::load();
        for (const auto& target : config.config.targets.targets) {
            require (s.apply (command::SetTarget { 2, target.key }).rejection == Rejection::None, "every target is selectable");
            const auto state = snapshot (s);
            require (state.find (target.key == "lp" ? "\"low\":{\"hand\":{\"db\":null,\"on\":null},\"machine\":{\"db\":0.5,\"on\":true}"
                                             : "\"low\":{\"hand\":{\"db\":null,\"on\":null},\"machine\":{\"db\":0,\"on\":false}") != std::string::npos,
                     "machine low correction belongs to the medium; absent means off at zero");
            DeviceEdit low; low.emplace<7>(); std::get<7> (low).on = true; std::get<7> (low).db = -6;
            require (s.apply (command::EditDevice { 3, low }).rejection == Rejection::None, "low is offered on every target while hidden");
            std::get<7> (low).db = 6;
            require (s.apply (command::EditDevice { 4, low }).rejection == Rejection::None, "low positive domain endpoint");
            std::get<7> (low).db = 6.01;
            require (s.apply (command::EditDevice { 5, low }).rejection == Rejection::OutOfDomain, "low domain remains bounded");
        }
        (void) s.apply (command::SetTarget { 6, "allStreaming" });
        (void) s.apply (command::SetManual { 2, true });
        // Variant 7 remains the low device across the rename.
        DeviceEdit edit; edit.emplace<7>(); std::get<7> (edit).on = true; std::get<7> (edit).db = 5.25;
        require (s.apply (command::EditDevice { 3, edit }).rejection == Rejection::None, "low is offered on streaming");
        (void) s.apply (command::SetTarget { 4, "lp" });
        require (s.apply (command::EditDevice { 5, edit }).rejection == Rejection::None, "low is offered on lp");
        const auto file = s.exportProject(); require (file.view().find ("[low]\n") != std::string_view::npos, "project uses low section");
        (void) s.apply (command::SetTarget { 6, "allStreaming" });
        require (s.exportProject().view().find ("db.hand") == std::string_view::npos, "target change removes low hand from export");
        require (snapshot (s).find ("\"eqCurve\":") != std::string::npos, "summed EQ curve is in snapshot");
    } else return 2;
    std::printf ("%s: %d failures\n", item.c_str(), failures); return failures ? 1 : 0;
}
