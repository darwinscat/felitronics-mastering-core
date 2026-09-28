// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#include "../../modules/session/tests/Advance.h"
#include <alloc_counter.h>
#include <felitronics_test.h>
#include <felitronics/session/Snapshot.h>
#include "fc_session_abi.h"
#include <felitronics/session/Config.h>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <new>
#include <string>

using namespace felitronics::session;
using felitronics::test::ok;
Session* sessionForReplayTest (fc_session handle) noexcept;
namespace
{
fc_session_status createSession (fc_session* out)
{
    const fc_session_capabilities caps { sizeof (fc_session_capabilities), 9007199254740991.0, 4294967295u, FC_SESSION_DEVICES_ALL, 9007199254740991.0 };
    const auto version = felitronics::session::config::Config::versions().all;
    return fc_session_create (&caps, std::uint32_t (version), std::uint32_t (version >> 32), out);
}

Session* original = nullptr;
const Snapshot* savedSnapshot = nullptr;
const ProjectText* savedProject = nullptr;
command::Load replayLoad;
fc_session originalHandle = 0;
fc_session extra = 777;
[[maybe_unused]] fc_session_status inner = FC_SESSION_OK;
[[maybe_unused]] void reenter() noexcept { inner = fc_session_destroy (0); }
std::string encoded (SnapshotView view)
{
    // Process identities and retained renders are not in the project.
    view.revision = 0; view.job = view.measurementJob = 0; view.jobRecipe = {}; view.masters = {};
    const auto need = Codec::encodedBytes (view);
    std::string out (std::size_t (need.bytes), '\0');
    ok (Codec::encode (view, out) == CodecStatus::Ok, "replay snapshot encodes");
    return out;
}
int replayAfterPoison()
{
    fc_session refused = 123;
    std::uint32_t halves[] = { 7, 7 };
    ok (createSession (&refused) == FC_SESSION_ERR_POISONED && refused == 123, "poison refuses a new handle for good");
    ok (fc_session_destroy (originalHandle) == FC_SESSION_ERR_POISONED && fc_session_destroy (0) == FC_SESSION_ERR_POISONED,
        "poison precedes valid and invalid handles");
    ok (fc_session_config_version (halves) == FC_SESSION_ERR_POISONED && halves[0] == 7 && halves[1] == 7,
        "poison refuses queries without writing");
    ok (createSession (nullptr) == FC_SESSION_ERR_POISONED && fc_session_config_version (nullptr) == FC_SESSION_ERR_POISONED,
        "poison precedes pointer checks");
    ok (fc_session_abi_version() == FC_SESSION_ABI_VERSION, "build identity stays readable");
    // Outside the abandoned module: desktop's new C++ owner, wasm's fresh module instance.
    auto restored = Session::create();
    ok (restored.session->apply (replayLoad).rejection == Rejection::None, "replay loads the same source");
    testing::measure (*restored.session);
    const auto answer = restored.session->importProject (8, savedProject->view());
    ok (answer.rejection == Rejection::None, "replay imports after the same measurement");
    auto replayed = restored.session->snapshot();
    auto expected = savedSnapshot->view();
    expected.masterProgress = {}; // work progress is not project state
    ok (encoded (expected) == encoded (replayed.view()), "replay restores state, target, manual mode and every field of both layers");
    ok (restored.session->exportProject().view() == savedProject->view(), "replay preserves the exact project text");
    ok (restored.session->revision() != original->revision() && restored.session->masters().empty(), "revisions and retained masters are not replayed");
    return felitronics::test::report();
}
[[noreturn]] void abandoned()
{
    std::puts ("native allocation failure abandoned the shipped facade; checking poison and replay from the terminate handler");
    ok (extra == 777, "failure published no handle");
    const int result = replayAfterPoison();
    std::fflush (nullptr);
    std::_Exit (result);
}
}
int main()
{
    fc_session handle = 0;
    ok (createSession (&handle) == FC_SESSION_OK, "facade creates the original session");
    // Test owns cleanup because a poisoned facade deliberately refuses even destroy.
    std::unique_ptr<Session> old (sessionForReplayTest (handle));
    float samples[] = { 0.0f, 0.25f, -0.25f, 0.0f };
    const float* planes[] = { samples, samples };
    const command::Load load { 1, { planes, 2, 4, 48000 }, { "replay.wav", 48000, true, 24 } };
    for (unsigned i = 0; i < 3; ++i)
    {
        ok (old->apply (load).rejection == Rejection::None, "successive loads");
        testing::measure (*old);
        (void) old->apply (command::SetTarget { 2, "cd" });
        (void) old->apply (command::SetManual { 3, true });
        HpfFields<Touched> hpf; hpf.fq = 36; hpf.on = false;
        ok (old->apply (command::EditDevice { 4, hpf }).rejection == Rejection::None, "device edits");
        (void) old->apply (command::SetTarget { 5, "lp" });
        ok (! old->project().devices.hpf.hand.fq && ! old->project().devices.hpf.hand.on, "target change resets prior edits");
        ok (old->apply (command::EditDevice { 5, hpf }).rejection == Rejection::None, "new target edits enter the replay recipe");
        (void) old->apply (command::EditTarget { 6, { -12.5, -0.5 } });
        (void) old->apply (command::Master { 7 }); (void) old->step (4);
    }
    const auto snapshot = old->snapshot();
    const auto saved = old->exportProject();
    ok (saved.rejection == Rejection::None && ! old->masters().empty(), "last project is outside the facade before failure");
    original = old.get(); savedSnapshot = &snapshot; savedProject = &saved; replayLoad = load; originalHandle = handle;
#if defined(__cpp_exceptions)
    // The failure injection is fc_master's. Session::create is noexcept, so on native hosts the
    // termination callback observes the still-active production guard. It never resumes the failed call.
    std::set_terminate (&abandoned);
    alloc::failNext = true;
    try { (void) createSession (&extra); }
    catch (const std::bad_alloc&) { abandoned(); }
    alloc::failNext = false;
    ok (false, "the armed allocation must abandon the call");
#else
    alloc::onNext = &reenter;
    (void) createSession (&extra);
    std::unique_ptr<Session> extraOwner (sessionForReplayTest (extra));
    ok (inner == FC_SESSION_ERR_POISONED, "reentry exercises the permanent poison on the exceptions-free tier");
#endif
    return replayAfterPoison();
}
