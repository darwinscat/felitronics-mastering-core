# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
# The paired core change until a release containing the tap supersedes v0.55.0.
set(header "${CORE_SOURCE}/modules/limiter/include/felitronics/limiter/TruePeakLimiter.h")
file(READ "${header}" source)
string(FIND "${source}" "float* peakClipReductionDb" present)
if(NOT present EQUAL -1)
    return()
endif()
file(SHA256 "${header}" base)
if(NOT base STREQUAL "4aecef82d3e9879f1dacab2618059e4c4df09181a0aa901ee86a30e97d8d6a19")
    message(FATAL_ERROR "K13 tap patch requires the pinned felitronics-core v0.55.0 header")
endif()
function(replace_once before after)
    string(FIND "${source}" "${before}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "K13 tap patch hunk did not match")
    endif()
    string(REPLACE "${before}" "${after}" revised "${source}")
    set(source "${revised}" PARENT_SCOPE)
endfunction()
replace_once([=[    int    capacity        = 0;         // in OVERSAMPLED samples; < numSamples * F refuses the call
]=] [=[    int    capacity        = 0;         // in OVERSAMPLED samples; < numSamples * F refuses the call
    float* peakClipReductionDb = nullptr; // positive K13 reduction, on the same oversampled grid
]=])
replace_once([=[    // The full form: the same call, with the oversampled gain-reduction and reconstructed-peak traces
    // written out. `tap.gainReductionDb == nullptr && tap.linkedPeakLin == nullptr` is off and costs
]=] [=[    // The full form: the same call, with the oversampled limiter, reconstructed-peak and K13 reduction traces
    // written out. All three tap pointers null is off and costs
]=])
replace_once([=[        if ((tap.gainReductionDb != nullptr || tap.linkedPeakLin != nullptr)
]=] [=[        if ((tap.gainReductionDb != nullptr || tap.linkedPeakLin != nullptr || tap.peakClipReductionDb != nullptr)
]=])
replace_once([=[            if (tap.linkedPeakLin   != nullptr) sTap.linkedPeakLin   = tap.linkedPeakLin   + osOff;
]=] [=[            if (tap.linkedPeakLin   != nullptr) sTap.linkedPeakLin   = tap.linkedPeakLin   + osOff;
            if (tap.peakClipReductionDb != nullptr) sTap.peakClipReductionDb = tap.peakClipReductionDb + osOff;
]=])
replace_once([=[            if (tap.linkedPeakLin != nullptr) tap.linkedPeakLin[(std::size_t) i] = linkedPeak;
]=] [=[            if (tap.linkedPeakLin != nullptr) tap.linkedPeakLin[(std::size_t) i] = linkedPeak;
            if (tap.peakClipReductionDb != nullptr) tap.peakClipReductionDb[(std::size_t) i] = 0.0f;
]=])
replace_once([=[                    if (red > clipMaxRedDb_) clipMaxRedDb_ = red;
]=] [=[                    if (tap.peakClipReductionDb != nullptr) tap.peakClipReductionDb[(std::size_t) i] = red;
                    if (red > clipMaxRedDb_) clipMaxRedDb_ = red;
]=])
file(WRITE "${header}" "${source}")
