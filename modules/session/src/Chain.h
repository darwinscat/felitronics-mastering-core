// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#pragma once

// THE WHOLE PLAN AS THE CHAIN GETS IT (internal to modules/session) — the one derivation of a master's chain from a
// project: what a master the session decides (version 0) renders, and nothing a shell supplies.
//
// THE ORDER: the config's defaults → the machine's layer → a person's layer → what can physically apply (a mono source
// has no side to fold, a depth above [dither] onUpToBits takes no dither, a device the shell does not offer is out). The
// eight devices write their stages through their own writers — writeEq (the high-pass, tilt and low: one stage),
// writeDynamics (the glue's compressor, the saturation), writeLimiter (the limiter with its peak clipper, the dither) —
// and mono bass here; each reads the project by the tick rule (settingsOf), so a person's edit sounds whether the panel
// is shown or hidden.
//
// THE TOPOLOGY FOLLOWS THE DEVICES AS THEY SOUND, never [stages] (which is only the defaults layer a project file is
// written against): a stage is in the chain when a device that writes it is — the EQ stage when one of its three
// bands is on, mono bass when ticked on a stereo source, the compressor when the glue compresses, the clipper stage when
// the saturation shapes, the dither when it is in the delivery — and the limiter always. The chain's fixed geometry
// (the quantum, the lookaheads, the oversampling) is [chain]'s, [compressor]'s and [limiter]'s, stated.
//
// ONE GAIN, ONCE: neither gain of the chain is written here. The landing search adds the gain that brings the input to
// [input] referenceLufs to the chain's input gain, once, and moves the gain before the limiter; the ceiling is the
// target's (a person's edit of it included), with the search's margin under it.

#include "Planner.h"
#include "Rules.h"

#include <felitronics/session/Commands.h>
#include <felitronics/session/Project.h>

namespace felitronics::session::detail
{
// `ready` becomes the chain of a version-0 master for `devices` on the inputs: its topology and every parameter. The
// delivery's rate and depth are left 0 — the target's, which the master job resolves. Allocates nothing.
void writeChain (const PlanInputs& in, const Devices& devices, command::MasterReady& ready) noexcept;
} // namespace felitronics::session::detail
