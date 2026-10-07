// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE LOW END OF THE TWO DEMO SONGS, as the first phase measured them (v0.19.0's run: the table from 10 Hz, the occupancy
// reference over the note range) — the regression guard of the sure lowest note on real music (owner, 07.10): Cold Gaze
// of Eternity and Cat in Space, the unmastered FLACs the site's demo plays. Per mix: its rate, frames, hop, the note
// range's share of the programme, the background density, the note v0.18.0 named (−1 unsure), and the bands from
// 20.60 Hz (MIDI 16) to the one above the lowest band on from 25 Hz: MIDI, frames on, duty, margin over the duty line
// (dB), density, resolved. A band not listed was never on. Written by measuring each song through the session at v0.18.0
// (775c7dc) and v0.19.0 (763abec), not by hand.

#include <cstdint>

struct RealMixBand { std::int32_t midi, count; double duty, marginDb, density; std::int32_t resolved; };
struct RealMix
{
    const char* name;
    std::uint32_t rate;
    std::uint64_t frames;
    double hop, rangeShare, background;
    std::int32_t v018Note;
    std::int32_t bandCount;
    RealMixBand bands[19];
};
inline constexpr RealMix kRealMixes[] = {
    { "cat-in-space", 48000, 6310957, 65536, 0.76958837321233875, 5.2114064046194078e-06, -1, 6, { { 16, 0, 0, 0, 5.8626207465645929e-07, 0 }, { 17, 1, 0.010526315789473684, 0.67508818784111924, 5.2777187878081361e-07, 0 }, { 18, 3, 0.031578947368421054, 1.9264647998520985, 3.9616828975966651e-06, 0 }, { 19, 0, 0, 0, 8.6034907608132361e-07, 0 }, { 20, 2, 0.021052631578947368, 2.588263927462112, 7.2697657847930581e-07, 1 }, { 21, 1, 0.010526315789473684, 1.8621047668462722, 1.435309686718404e-06, 1 } } },
    { "cold-gaze-of-eternity", 48000, 15438858, 65536, 0.74551784973640045, 3.9450484847307731e-06, 27, 13, { { 16, 0, 0, 0, 2.1373907419246328e-09, 0 }, { 17, 0, 0, 0, 1.7241144253356586e-09, 0 }, { 18, 0, 0, 0, 2.0860212642654268e-09, 0 }, { 19, 0, 0, 0, 1.2719492393156103e-09, 0 }, { 20, 0, 0, 0, 1.2560949756955229e-09, 1 }, { 21, 0, 0, 0, 2.3797262040412004e-09, 1 }, { 22, 0, 0, 0, 2.2425465371204393e-09, 1 }, { 23, 0, 0, 0, 3.1216112346930384e-09, 1 }, { 24, 0, 0, 0, 5.5861902639288114e-09, 1 }, { 25, 0, 0, 0, 8.4538941672440813e-09, 1 }, { 26, 0, 0, 0, 2.147933201684689e-08, 1 }, { 27, 53, 0.2264957264957265, 5.4203583354365072, 3.6381863357367906e-06, 1 }, { 28, 75, 0.32051282051282054, 8.096642339018878, 9.5050569147851674e-06, 1 } } },
};
