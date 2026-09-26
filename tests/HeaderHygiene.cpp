// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

// HEADER HYGIENE — one TU that #includes every public header of this repository, compiled under core's
// strict, downstream-grade warning set (FELITRONICS_HYGIENE_WARNINGS, -Werror). The same TU is the
// -fno-exceptions / -fno-rtti probe. Nothing here is a class template with a default to instantiate: the
// chain, the solver and every analyzer are concrete types, so including them puts their member bodies
// through the compiler.

#include <felitronics/analysis/BandBursts.h>
#include <felitronics/analysis/BandCrest.h>
#include <felitronics/analysis/ClipDetector.h>
#include <felitronics/analysis/HumDetector.h>
#include <felitronics/analysis/LowEnd.h>
#include <felitronics/analysis/PeakExcursions.h>
#include <felitronics/analysis/ProgrammeReport.h>
#include <felitronics/analysis/SourceForensics.h>
#include <felitronics/analysis/SpectrumFrames.h>
#include <felitronics/analysis/StereoBandBursts.h>
#include <felitronics/analysis/StereoColumns.h>
#include <felitronics/analysis/WaveformPeaks.h>
#include <felitronics/mastering/DeliveredMastering.h>
#include <felitronics/mastering/DeliveryConverter.h>
#include <felitronics/mastering/LoudnessSolver.h>
#include <felitronics/mastering/MasteringChain.h>
#include <felitronics/mastering/OfflineRenderer.h>
#include <felitronics/mastering/Planes.h>
#include <felitronics/mastering/Progress.h>
#include <felitronics/tempo/JsNumerics.h>
#include <felitronics/tempo/TempoDetector.h>

int main() { return 0; }
