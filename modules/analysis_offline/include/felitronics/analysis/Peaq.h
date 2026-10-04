// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.

#pragma once

// THE BUILD THIS HEADER NEEDS. Its numbers are diffed bit for bit between rows, and a fused multiply-add or a
// reassociated sum is another number: every translation unit that includes it must be compiled with
// -ffp-contract=off -fno-fast-math (MSVC: /fp:precise, no /fp:contract). The target felitronics::peaq carries those
// flags as INTERFACE options, together with FELITRONICS_PEAQ_FLAGS; a unit that includes this header without linking
// it does not compile. (The preprocessor cannot see contraction on clang or gcc — hence the marker; fast-math it can
// see, and core/DetMath.h refuses it.)
#if ! defined (FELITRONICS_PEAQ_FLAGS)
    #error "felitronics/analysis/Peaq.h: link felitronics::peaq, which compiles this unit with -ffp-contract=off -fno-fast-math (tools/wasm/build.sh passes the same flags and -DFELITRONICS_PEAQ_FLAGS)"
#endif
#if defined (_MSC_VER) && ! defined (__clang__) && (defined (_M_FP_CONTRACT) || defined (_M_FP_FAST))
    #error "felitronics/analysis/Peaq.h must be compiled with /fp:precise and without /fp:contract"
#endif

#include <felitronics/core/DetMath.h>
#include <felitronics/core/OfflineFft.h>
#include <felitronics/storage/VectorBytes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace felitronics::analysis
{

//==============================================================================
// felitronics::analysis::Peaq — PEAQ, the BASIC version of ITU-R BS.1387 (Perceptual Evaluation of Audio Quality),
// written from the text of the Recommendation (BS.1387-2, 05/2023). Where the text is ambiguous the reading follows
// P. Kabal, "An Examination and Interpretation of ITU-R BS.1387: PEAQ" (McGill, 2003); each such place says so.
//
// WHAT IT ANSWERS. A reference and a test programme, time-aligned, 48 kHz, mono or stereo, go in; out come the
// eleven Model Output Variables of the Basic version, the network's Distortion Index and the Objective Difference
// Grade (0 = imperceptible ... -4 = very annoying). The listening level is the standard's default: a full-scale
// sine plays at 92 dB SPL.
//
// THE MODEL, section by section of the Recommendation (Annex 2):
//   2.1.2-2.1.3  frames of 2048 samples, hop 1024, Hann window, FFT; the scale is set so that the largest bin of a
//                0 dBFS sine at 1019.5 Hz reads 92 dB, measured over 10 frames exactly as the text defines Norm.
//   2.1.4        outer and middle ear weighting W[k] (eq. 7).
//   2.1.5-2.1.6  grouping into the 109 bands of Table 6 (its frequencies are used as printed; they agree with
//                7*asinh(f/650) at 0.25 Bark to 4e-6) and the internal noise PThres (eq. 13).
//   2.1.7        the level-dependent two-sided spreading (eqs. 15-20), normalised by the spread of a 0 dB pattern;
//                summed in Kabal's recursive order (lower slope from the top band down, upper slope band by band).
//   2.1.8-2.1.9  forward masking (eqs. 21-24) and the mask offset m[k] (eqs. 25-26).
//   3            level and pattern adaptation (eqs. 41-53, M = 8: M1 = 3, M2 = 4), modulation (54-57), loudness
//                (58-61), the error signal from the difference of the weighted MAGNITUDE spectra (62).
//   4            the eleven MOVs; 5 their spectral, temporal and channel averaging; 6.1-6.2 the network (Tables 13-16).
//
// FRAME SELECTION (5.2.4). Every MOV is averaged only over frames inside the DATA BOUNDARY of the reference: the
// first and last group of five consecutive samples whose absolute sum exceeds 200 on the 16-bit scale (200/32768
// here) in either channel; a frame fully outside is ignored (Kabal 5.1: start = first sample of the first such group,
// end = last sample of the last one). Inside it the modulation and noise-loudness MOVs skip the first 0.5 s of the
// programme (delayed averaging); the noise loudness also waits until 50 ms after both programmes first reach 0.1 sone
// in one channel; the bandwidths count frames whose reference bandwidth exceeds 346 lines; EHS skips frames whose
// newest half has an energy below 8000 (16-bit scale) in every channel of both programmes. Stereo MOVs are computed
// per channel and averaged, except MFPD and ADB, which take the binaural maximum per band (4.7).
//
// THE ORACLE. Where the text leaves a choice, the choice was checked against GstPEAQ (M. Holters, LGPL — run as a
// black box, its source neither copied nor read): the tail frame, the start of the delayed averaging, the loudness
// threshold, the EHS lines, lags and mean removal each say so where they are made. Where the text decides, the text
// wins, and where GstPEAQ then differs the place says by how much: the bandwidth of a frame in which both programmes
// are digitally silent, and the loudness threshold of a pair whose loud passages never overlap (GstPEAQ: nan). On the
// 158 pairs it grades (drum loops at 48 damage levels, 99 masters of this core at nine loudness targets, 11 synthetic
// pairs built for one reading each) the grade agrees with it to 0.0015 on average, 0.032 at worst (a snare loop with
// silent frames: the bandwidth above).
//
// DETERMINISM. Every elementary function is core::det's (or built here from IEEE-exact operations: atan, below), the
// FFT is core::offline::fftInplace, every sum runs in a fixed order, and the units that compile this header build
// with -ffp-contract=off -fno-fast-math. Square roots go through core::DetMath::sqrt, the exact IEEE operation (the
// det-math lint reads a bare std::sqrt in a file that spells std::complex as a possible complex root). Native and
// wasm give the same bits; the parity step diffs them.
//
// THE CONTRACT (law 8a, the analyzers' shape): prepare() allocates everything, sized by the channel count and the
// longest programme it will see; process() and finish() allocate nothing. Frame f covers samples [1024 f, 1024 f +
// 2048) and is analysed the instant its last sample arrives, so no call boundary moves anything: any slicing gives
// the same bits. THE TAIL: samples that no frame has covered when finish() runs go into one more frame completed
// with zeros (the text is silent; Kabal 6.8 lists this as one option, and it is GstPEAQ's), so a programme of
// 336000 samples has 328 frames, not 327. The per-frame values of the MOVs are kept (eleven doubles per frame and
// channel, three per frame), because the data boundary's end is known only at finish(); finish() selects and
// averages them.
//
// TRANSPARENT INPUT. On a test that is the reference itself, or differs from it far below the mask, the formula of
// EHS still normalises the error spectrum's autocorrelation and reads a structure in numerical noise (GstPEAQ and this
// model both score a real master that left its programme alone at ODG -2.1..-2.2). PEAQ was never asked that
// question. So when EVERY channel's noise-to-mask ratio is below kTransparentNmrDb AND every channel's waveform error
// is below kTransparentResidualDb of its energy, the verdict is Transparent and the reported grade is 0; the
// network's own answer stays readable as modelOdg. Per channel, because the MOV is the mean of the channels' dB and an
// untouched channel would average a damaged one away; the waveform term, because the model compares magnitude
// spectra and a channel with its polarity flipped reads no noise at all. An Undefined MOV wins over this rule.
//
// THE INPUT RANGE. Samples beyond kMaxAbsSample (+18 dBFS) make the verdict OutOfRange: the model is not defined there
// (see the constant).
//
// CONFORMANCE (7.4) IS NOT PROVEN. The Recommendation asks |delta DI| < 0.02 on its 16 test items (Table 22: acodsna
// 1.304 ... scodclv 1.689) and says they are available for download; no open copy was found (the Recommendation's
// page and the ITU-R software pages, 03.10.2026), so this implementation has not been run on them. What stands
// instead is the oracle agreement below and the hand checks of the suite.
//==============================================================================

inline constexpr int kPeaqBands = 109;
inline constexpr int kPeaqMovCount = 11;

// The eleven MOVs in the order of Table 13 — the network's input order.
enum class PeaqMov : int
{
    BandwidthRef = 0, BandwidthTest, TotalNmr, WinModDiff1, Adb, Ehs, AvgModDiff1, AvgModDiff2, RmsNoiseLoud, Mfpd,
    RelDistFrames
};

inline constexpr const char* kPeaqMovNames[kPeaqMovCount] {
    "BandwidthRefB", "BandwidthTestB", "Total NMRB", "WinModDiff1B", "ADBB", "EHSB", "AvgModDiff1B", "AvgModDiff2B",
    "RmsNoiseLoudB", "MFPDB", "RelDistFramesB"
};

enum class PeaqVerdict : int
{
    NotRun = 0,        // finish() has not run since prepare()/reset()
    Graded = 1,        // odg is the network's grade
    Transparent = 2,   // EVERY channel's NMR under kTransparentNmrDb and its waveform error under
                       // kTransparentResidualDb: odg = 0, modelOdg keeps the network's
    NoSignal = 3,      // no frame inside the data boundary (a silent reference, or a programme shorter than a frame)
    NonFinite = 4,     // a non-finite sample arrived (it was analysed as zero): nothing is graded
    Undefined = 5,     // a MOV averaged over no frame at all, so it is NaN and so is the grade: a reference that never
                       // reaches 346 lines (8.1 kHz), a programme shorter than 0.5 s, nothing above the EHS energy floor
    OutOfRange = 6     // a sample beyond Peaq::kMaxAbsSample, where the model is no longer defined: nothing is graded
};

struct PeaqResult
{
    PeaqVerdict verdict = PeaqVerdict::NotRun;
    std::array<double, kPeaqMovCount> mov {};
    double distortionIndex = 0.0;
    double modelOdg = 0.0;          // the network's grade whenever there were frames to grade (NaN when Undefined)
    double odg = 0.0;               // the reported grade: modelOdg, or 0 for Transparent
    long long frames = 0;           // frames analysed
    long long firstFrame = -1;      // the data boundary, in frames (inclusive); -1 when there is none
    long long lastFrame = -1;
    long long nonFiniteSamples = 0;
    long long outOfRangeSamples = 0;  // |sample| > Peaq::kMaxAbsSample
    std::array<double, 2> channelNmrDb { 0.0, 0.0 };        // Total NMR of each channel (the MOV is their mean)
    std::array<double, 2> channelResidualDb { 0.0, 0.0 };   // 10 log10 of sum (test - ref)^2 / sum ref^2, per channel
    bool graded() const noexcept { return verdict == PeaqVerdict::Graded || verdict == PeaqVerdict::Transparent; }
};

namespace peaq_detail
{
    inline constexpr double kBandLowHz[kPeaqBands] {
        80, 103.445, 127.023, 150.762, 174.694, 198.849, 223.257, 247.95, 272.959, 298.317, 324.055, 350.207, 376.805,
        403.884, 431.478, 459.622, 488.353, 517.707, 547.721, 578.434, 609.885, 642.114, 675.161, 709.071, 743.884,
        779.647, 816.404, 854.203, 893.091, 933.119, 974.336, 1016.797, 1060.555, 1105.666, 1152.187, 1200.178, 1249.7,
        1300.816, 1353.592, 1408.094, 1464.392, 1522.559, 1582.668, 1644.795, 1709.021, 1775.427, 1844.098, 1915.121,
        1988.587, 2064.59, 2143.227, 2224.597, 2308.806, 2395.959, 2486.169, 2579.551, 2676.223, 2776.309, 2879.937,
        2987.238, 3098.35, 3213.415, 3332.579, 3455.993, 3583.817, 3716.212, 3853.348, 3995.399, 4142.547, 4294.979,
        4452.89, 4616.482, 4785.962, 4961.548, 5143.463, 5331.939, 5527.217, 5729.545, 5939.183, 6156.396, 6381.463,
        6614.671, 6856.316, 7106.708, 7366.166, 7635.02, 7913.614, 8202.302, 8501.454, 8811.45, 9132.688, 9465.574,
        9810.536, 10168.013, 10538.46, 10922.351, 11320.175, 11732.438, 12159.67, 12602.412, 13061.229, 13536.71,
        14029.458, 14540.103, 15069.295, 15617.71, 16186.049, 16775.035, 17385.42
    };
    inline constexpr double kBandCentreHz[kPeaqBands] {
        91.708, 115.216, 138.87, 162.702, 186.742, 211.019, 235.566, 260.413, 285.593, 311.136, 337.077, 363.448,
        390.282, 417.614, 445.479, 473.912, 502.95, 532.629, 562.988, 594.065, 625.899, 658.533, 692.006, 726.362,
        761.644, 797.898, 835.17, 873.508, 912.959, 953.576, 995.408, 1038.511, 1082.938, 1128.746, 1175.995, 1224.744,
        1275.055, 1326.992, 1380.623, 1436.014, 1493.237, 1552.366, 1613.474, 1676.641, 1741.946, 1809.474, 1879.31,
        1951.543, 2026.266, 2103.573, 2183.564, 2266.34, 2352.008, 2440.675, 2532.456, 2627.468, 2725.832, 2827.672,
        2933.12, 3042.309, 3155.379, 3272.475, 3393.745, 3519.344, 3649.432, 3784.176, 3923.748, 4068.324, 4218.09,
        4373.237, 4533.963, 4700.473, 4872.978, 5051.7, 5236.866, 5428.712, 5627.484, 5833.434, 6046.825, 6267.931,
        6497.031, 6734.42, 6980.399, 7235.284, 7499.397, 7773.077, 8056.673, 8350.547, 8655.072, 8970.639, 9297.648,
        9636.52, 9987.683, 10351.586, 10728.695, 11119.49, 11524.47, 11944.149, 12379.066, 12829.775, 13296.85,
        13780.887, 14282.503, 14802.338, 15341.057, 15899.345, 16477.914, 17077.504, 17690.045
    };
    inline constexpr double kBandHighHz[kPeaqBands] {
        103.445, 127.023, 150.762, 174.694, 198.849, 223.257, 247.95, 272.959, 298.317, 324.055, 350.207, 376.805,
        403.884, 431.478, 459.622, 488.353, 517.707, 547.721, 578.434, 609.885, 642.114, 675.161, 709.071, 743.884,
        779.647, 816.404, 854.203, 893.091, 933.119, 974.336, 1016.797, 1060.555, 1105.666, 1152.187, 1200.178, 1249.7,
        1300.816, 1353.592, 1408.094, 1464.392, 1522.559, 1582.668, 1644.795, 1709.021, 1775.427, 1844.098, 1915.121,
        1988.587, 2064.59, 2143.227, 2224.597, 2308.806, 2395.959, 2486.169, 2579.551, 2676.223, 2776.309, 2879.937,
        2987.238, 3098.35, 3213.415, 3332.579, 3455.993, 3583.817, 3716.212, 3853.348, 3995.399, 4142.547, 4294.979,
        4452.89, 4616.482, 4785.962, 4961.548, 5143.463, 5331.939, 5527.217, 5729.545, 5939.183, 6156.396, 6381.463,
        6614.671, 6856.316, 7106.708, 7366.166, 7635.02, 7913.614, 8202.302, 8501.454, 8811.45, 9132.688, 9465.574,
        9810.536, 10168.013, 10538.46, 10922.351, 11320.175, 11732.438, 12159.67, 12602.412, 13061.229, 13536.71,
        14029.458, 14540.103, 15069.295, 15617.71, 16186.049, 16775.035, 17385.42, 18000
    };

    inline constexpr double kLog2E = 1.44269504088896340736;
    inline constexpr double kTwoPi = 6.28318530717958647692;
    inline constexpr double kHalfPi = 1.57079632679489661923;

    // e^x through det::exp2. The product x*log2(e) rounds, which costs accuracy at large |x|, never determinism.
    inline double detExp (double x) noexcept { return core::det::exp2 (x * kLog2E); }

    // atan from IEEE-exact operations only (+ - * / sqrt): two half-angle reductions take |x| <= 1 under 0.2,
    // where the Taylor series has converged to the last bit by its 22nd term. Used for constants only (eq. 61).
    inline double atan (double x) noexcept
    {
        if (std::isnan (x)) return x;
        const bool neg = x < 0.0;
        double t = neg ? -x : x;
        const bool inv = t > 1.0;
        if (inv) t = 1.0 / t;
        t = t / (1.0 + core::DetMath::sqrt (1.0 + t * t));
        t = t / (1.0 + core::DetMath::sqrt (1.0 + t * t));
        const double t2 = t * t;
        double term = t, sum = 0.0;
        double pw = t;
        for (int n = 0; n < 22; ++n)
        {
            term = pw / (double) (2 * n + 1);
            sum = (n & 1) ? sum - term : sum + term;
            pw = pw * t2;
        }
        double r = 4.0 * sum;
        if (inv) r = kHalfPi - r;
        return neg ? -r : r;
    }

    // The network of the Basic version (Tables 13-16).
    inline constexpr double kAmin[kPeaqMovCount] { 393.916656, 361.965332, -24.045116, 1.110661, -0.206623, 0.074318,
                                                   1.113683, 0.950345, 0.029985, 0.000101, 0.0 };
    inline constexpr double kAmax[kPeaqMovCount] { 921.0, 881.131226, 16.212030, 107.137772, 2.886017, 13.933351,
                                                   63.257874, 1145.018555, 14.819740, 1.0, 1.0 };
    inline constexpr double kWx[kPeaqMovCount + 1][3] {
        { -0.502657,  0.436333,   1.219602 },
        {  4.307481,  3.246017,   1.123743 },
        {  4.984241, -2.211189,  -0.192096 },
        {  0.051056, -1.762424,   4.331315 },
        {  2.321580,  1.789971,  -0.754560 },
        { -5.303901, -3.452257, -10.814982 },
        {  2.730991, -6.111805,   1.519223 },
        {  0.624950, -1.331523,  -5.955151 },
        {  3.102889,  0.871260,  -5.922878 },
        { -1.051468, -0.939882,  -0.142913 },
        { -1.804679, -0.503610,  -0.620456 },
        { -2.518254,  0.654841,  -2.207228 }   // bias
    };
    inline constexpr double kWy[4] { -3.817048, 4.107138, 4.629582, -0.307594 };   // nodes 1-3, bias
    inline constexpr double kBmin = -3.98, kBmax = 0.22;

    inline double sigmoid (double x) noexcept { return 1.0 / (1.0 + detExp (-x)); }
} // namespace peaq_detail

class Peaq
{
public:
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kFrame = 2048;
    static constexpr int kHop = 1024;
    static constexpr int kBins = kFrame / 2 + 1;
    static constexpr int kBands = kPeaqBands;
    static constexpr int kMaxChannels = 2;
    static constexpr double kListeningLevelDb = 92.0;    // 2.1.3: SPL of a full-scale sine when the level is unknown
    static constexpr double kTransparentNmrDb = -90.0;   // see TRANSPARENT INPUT above: every channel under it ...
    static constexpr double kTransparentResidualDb = -40.0;  // ... and every channel's waveform error this far down
    // THE RANGE THE MODEL IS DEFINED ON. Full scale plays at 92 dB SPL; past about +28 dB the upper spreading slope
    // (eq. 15: -24 - 230/fc + 0.2 L) turns positive, the spreading sums grow without bound, and at 1e19 the noise-to-
    // mask ratio reads +531 dB while at 1e37 it is NaN. A sample past +18 dBFS makes the verdict OutOfRange.
    static constexpr double kMaxAbsSample = 8.0;
    // 4.6: a frame is disturbed when some band's noise-to-mask ratio is >= 1.5 dB.
    static bool disturbedFrame (double nmrMaxLinear) noexcept { return nmrMaxLinear >= core::det::pow10 (0.15); }
    static constexpr int kDelayFrames = 24;              // 5.2.4.1: ceil(0.5 s * 48000 / 1024)
    static constexpr int kLoudnessDelayFrames = 3;       // 5.2.4.2: ceil(0.05 s * 48000 / 1024)
    static constexpr int kEhsLags = 256;                 // 4.8.1: largest power of two below 768 / 2
    static constexpr int kPerFrameChannel = 11;          // stored per frame and channel (see Slot)
    static constexpr int kPerFrame = 3;                  // stored per frame: Pbin, Qbin, energetic

    // What prepare() asks the heap for (law 11d), from the layout prepare() builds the object with.
    struct Storage
    {
        bool ok = false;
        std::uint64_t arenaDoubles = 0;
        std::uint64_t fftComplex = 0;     // 2048 + 512 + 256
        std::uint64_t bytes() const noexcept { return 8u * arenaDoubles + 16u * fftComplex; }
        std::uint64_t firstBytes() const noexcept { return Peaq::constructBytes() + bytes(); }
    };
    static constexpr std::uint64_t constructBytes() noexcept { return 4u * storage::kVectorProxyBytes; }

    static long long framesFor (long long samples) noexcept
    {
        if (samples <= 0) return 0;
        return samples <= kFrame ? 1 : (samples - kFrame + kHop - 1) / kHop + 1;
    }

    static Storage storageFor (int channels, long long maxSamples) noexcept
    {
        Storage s;
        if (channels < 1 || channels > kMaxChannels || maxSamples < 0 || maxSamples > (1LL << 40)) return s;
        // The per-frame history grows with the programme; sized in uint64 first, so a wasm32 size_t never wraps.
        const std::uint64_t frames = (std::uint64_t) framesFor (maxSamples);
        const std::uint64_t perFrame = (std::uint64_t) channels * (std::uint64_t) kPerFrameChannel + (std::uint64_t) kPerFrame;
        if (frames > (std::uint64_t) std::numeric_limits<std::size_t>::max() / 16u / perFrame) return s;
        const Layout l = layoutFor (channels, framesFor (maxSamples));
        s.ok = true;
        s.arenaDoubles = l.total;
        s.fftComplex = (std::uint64_t) kFrame + 512u + 256u;
        return s;
    }

    [[nodiscard]] bool prepare (int channels, long long maxSamples) noexcept
    {
        prepared_ = false;                                   // disarm, validate, write
        const Storage st = storageFor (channels, maxSamples);
        if (! st.ok) return false;
        channels_ = channels;
        maxSamples_ = maxSamples;
        capacityFrames_ = framesFor (maxSamples);
        l_ = layoutFor (channels, capacityFrames_);
        arena_.assign ((std::size_t) st.arenaDoubles, 0.0);
        fft_.assign ((std::size_t) kFrame, std::complex<double> {});
        fftCorr_.assign (512u, std::complex<double> {});
        fftEhs_.assign ((std::size_t) kEhsLags, std::complex<double> {});
        buildTables();
        prepared_ = true;
        reset();
        return true;
    }

    bool isPrepared() const noexcept { return prepared_; }
    int channels() const noexcept { return channels_; }

    void reset() noexcept
    {
        total_ = 0;
        frames_ = 0;
        finished_ = false;
        nonFinite_ = 0;
        outOfRange_ = 0;
        boundaryFirst_ = -1;
        boundaryLast_ = -1;
        result_ = PeaqResult {};
        if (! prepared_) return;
        std::fill (arena_.begin() + (std::ptrdiff_t) l_.state, arena_.end(), 0.0);
    }

    // Reference and test, `numChannels` planes each, n samples. Refused whole (nothing moves): n < 0, unprepared,
    // after finish(), a channel count other than the prepared one, null planes with n > 0, or more samples in total
    // than prepare() was sized for.
    [[nodiscard]] bool process (const float* const* ref, const float* const* test, int numChannels, int n) noexcept
    {
        if (n < 0) return false;
        if (! prepared_ || finished_) return false;
        if (numChannels != channels_) return false;
        if (n == 0) return true;
        if (ref == nullptr || test == nullptr) return false;
        for (int c = 0; c < numChannels; ++c)
            if (ref[c] == nullptr || test[c] == nullptr) return false;
        if (total_ + (long long) n > maxSamples_) return false;
        for (int i = 0; i < n; ++i)
        {
            const std::size_t slot = (std::size_t) (total_ & (kFrame - 1));
            for (int c = 0; c < channels_; ++c)
            {
                double xr = (double) ref[c][i], xt = (double) test[c][i];
                if (! std::isfinite (xr)) { xr = 0.0; ++nonFinite_; }
                if (! std::isfinite (xt)) { xt = 0.0; ++nonFinite_; }
                if (std::fabs (xr) > kMaxAbsSample || std::fabs (xt) > kMaxAbsSample) ++outOfRange_;
                const double err = xt - xr;
                at (chanBase (c) + kEnergy)[0] += xr * xr;
                at (chanBase (c) + kEnergy)[1] += err * err;
                arena_[ringRef (c) + slot] = xr;
                arena_[ringTest (c) + slot] = xt;
                watchBoundary (c, xr);
            }
            ++total_;
            if (total_ >= kFrame && ((total_ - kFrame) % kHop) == 0) analyseFrame (total_);
        }
        return true;
    }

    // Ends the programme: selects the frames, averages the MOVs, runs the network. Idempotent; process() is refused
    // afterwards until reset().
    void finish() noexcept
    {
        if (! prepared_ || finished_) return;
        finished_ = true;
        const long long covered = frames_ == 0 ? 0 : (frames_ - 1) * kHop + kFrame;
        if (total_ > covered)
        {
            // THE TAIL: samples no frame has covered yet go into one more frame, completed with zeros.
            long long clock = total_;
            while (clock < kFrame || ((clock - kFrame) % kHop) != 0)
            {
                const std::size_t slot = (std::size_t) (clock & (kFrame - 1));
                for (int c = 0; c < channels_; ++c) { arena_[ringRef (c) + slot] = 0.0; arena_[ringTest (c) + slot] = 0.0; }
                ++clock;
            }
            analyseFrame (clock);
        }
        computeResult();
    }

    const PeaqResult& result() const noexcept { return result_; }
    long long samplesProcessed() const noexcept { return total_; }

    // The network alone (6.1, eqs. 94-96): MOVs in Table 13 order -> DI. Public so a test can pin it to hand values.
    static double distortionIndex (const std::array<double, kPeaqMovCount>& mov) noexcept
    {
        using namespace peaq_detail;
        double di = kWy[3];
        for (int j = 0; j < 3; ++j)
        {
            double arg = kWx[kPeaqMovCount][j];
            for (int i = 0; i < kPeaqMovCount; ++i)
                arg += kWx[i][j] * (mov[(std::size_t) i] - kAmin[i]) / (kAmax[i] - kAmin[i]);
            di += kWy[j] * sigmoid (arg);
        }
        return di;
    }
    static double odgFromDi (double di) noexcept
    {
        return peaq_detail::kBmin + (peaq_detail::kBmax - peaq_detail::kBmin) * peaq_detail::sigmoid (di);
    }

    // Read-only views of the model's constant tables, for the suite's hand checks.
    double outerEarWeight (int bin) const noexcept { return arena_[l_.w2 + (std::size_t) bin]; }
    double internalNoise (int band) const noexcept { return arena_[l_.pthres + (std::size_t) band]; }
    double spreadNorm (int band) const noexcept { return arena_[l_.normSp + (std::size_t) band]; }
    double windowSample (int n) const noexcept { return arena_[l_.window + (std::size_t) n]; }
    double loudnessIndex (int band) const noexcept { return arena_[l_.sLoud + (std::size_t) band]; }

private:
    // One frame and channel's stored values.
    enum Slot : int { kMd1 = 0, kMd2, kTempWt, kNl, kBwRef, kBwTest, kNmrAvg, kNmrMax, kEhs, kNtotRef, kNtotTest };

    struct Layout
    {
        // constant tables
        std::size_t window = 0, w2 = 0, bandUl = 0, bandUu = 0, pthres = 0, pthres03 = 0, ethres = 0, sLoud = 0,
                    etsLoud = 0, aUC = 0, gIL = 0, normSp = 0, gm = 0, aTime = 0, bTime = 0, aAdapt = 0, bAdapt = 0,
                    ehsWindow = 0;
        // everything from `state` on is zeroed by reset()
        std::size_t state = 0;
        std::size_t chan = 0, chanStride = 0;
        // scratch shared by the channels of a frame
        std::size_t x2R = 0, x2T = 0, feR = 0, feT = 0, nz = 0;
        std::size_t ppR = 0, ppT = 0, e2R = 0, e2T = 0, eR = 0, eT = 0, epR = 0, epT = 0, modR = 0, modT = 0, pn = 0,
                    rR = 0, rT = 0, ene = 0, aucee = 0, es = 0, ehsD = 0, ehsC = 0;
        std::size_t history = 0, frameHistory = 0;
        std::uint64_t total = 0;
    };

    // Per-channel state, as offsets inside one channel's stride.
    enum ChanOffset : std::size_t
    {
        kRingRef = 0,
        kRingTest = kRingRef + (std::size_t) kFrame,
        kEfR = kRingTest + (std::size_t) kFrame,          // forward masking filters (2.1.8)
        kEfT = kEfR + (std::size_t) kBands,
        kPR = kEfT + (std::size_t) kBands,                // level adaptation low-passes (eqs. 42-43)
        kPT = kPR + (std::size_t) kBands,
        kRn = kPT + (std::size_t) kBands,                 // pattern adaptation sums (eq. 48)
        kRd = kRn + (std::size_t) kBands,
        kPcR = kRd + (std::size_t) kBands,                // PattCorr (eq. 50)
        kPcT = kPcR + (std::size_t) kBands,
        kDeR = kPcT + (std::size_t) kBands,               // modulation (eqs. 54-55)
        kDeT = kDeR + (std::size_t) kBands,
        kEaR = kDeT + (std::size_t) kBands,
        kEaT = kEaR + (std::size_t) kBands,
        kEeR = kEaT + (std::size_t) kBands,
        kEeT = kEeR + (std::size_t) kBands,
        kPd = kEeT + (std::size_t) kBands,                // this frame's detection probabilities per band
        kQd = kPd + (std::size_t) kBands,
        kLast5 = kQd + (std::size_t) kBands,              // the data boundary's five-sample window (+1: its cursor)
        kEnergy = kLast5 + 6u,                            // sum of ref^2 and of (test - ref)^2 over the programme
        kChanSize = kEnergy + 2u
    };

    static Layout layoutFor (int channels, long long frames) noexcept
    {
        Layout l;
        std::size_t at = 0;
        auto take = [&at] (std::size_t n) { const std::size_t o = at; at += n; return o; };
        const std::size_t B = (std::size_t) kBands;
        l.window = take ((std::size_t) kFrame);
        l.w2 = take ((std::size_t) kBins);
        l.bandUl = take (B); l.bandUu = take (B); l.pthres = take (B); l.pthres03 = take (B); l.ethres = take (B);
        l.sLoud = take (B); l.etsLoud = take (B); l.aUC = take (B); l.gIL = take (B); l.normSp = take (B);
        l.gm = take (B); l.aTime = take (B); l.bTime = take (B); l.aAdapt = take (B); l.bAdapt = take (B);
        l.ehsWindow = take ((std::size_t) kEhsLags);
        l.state = at;
        l.chanStride = (std::size_t) kChanSize;
        l.chan = take (l.chanStride * (std::size_t) channels);
        l.x2R = take ((std::size_t) kBins); l.x2T = take ((std::size_t) kBins);
        l.feR = take ((std::size_t) kBins); l.feT = take ((std::size_t) kBins); l.nz = take ((std::size_t) kBins);
        l.ppR = take (B); l.ppT = take (B); l.e2R = take (B); l.e2T = take (B); l.eR = take (B); l.eT = take (B);
        l.epR = take (B); l.epT = take (B); l.modR = take (B); l.modT = take (B); l.pn = take (B); l.rR = take (B);
        l.rT = take (B); l.ene = take (B); l.aucee = take (B); l.es = take (B);
        l.ehsD = take (2u * (std::size_t) kEhsLags);
        l.ehsC = take ((std::size_t) kEhsLags);
        l.history = take ((std::size_t) frames * (std::size_t) channels * (std::size_t) kPerFrameChannel);
        l.frameHistory = take ((std::size_t) frames * (std::size_t) kPerFrame);
        l.total = (std::uint64_t) at;
        return l;
    }

    std::size_t chanBase (int c) const noexcept { return l_.chan + (std::size_t) c * l_.chanStride; }
    std::size_t ringRef (int c) const noexcept { return chanBase (c) + kRingRef; }
    std::size_t ringTest (int c) const noexcept { return chanBase (c) + kRingTest; }
    double* at (std::size_t o) noexcept { return arena_.data() + o; }
    const double* at (std::size_t o) const noexcept { return arena_.data() + o; }
    double& hist (long long f, int c, int slot) noexcept
    {
        return arena_[l_.history + ((std::size_t) f * (std::size_t) channels_ + (std::size_t) c) * kPerFrameChannel
                      + (std::size_t) slot];
    }
    double hist (long long f, int c, int slot) const noexcept
    {
        return arena_[l_.history + ((std::size_t) f * (std::size_t) channels_ + (std::size_t) c) * kPerFrameChannel
                      + (std::size_t) slot];
    }
    double& fhist (long long f, int slot) noexcept { return arena_[l_.frameHistory + (std::size_t) f * kPerFrame + (std::size_t) slot]; }
    double fhist (long long f, int slot) const noexcept { return arena_[l_.frameHistory + (std::size_t) f * kPerFrame + (std::size_t) slot]; }

    void buildTables() noexcept
    {
        using namespace peaq_detail;
        namespace det = core::det;
        const double df = kSampleRate / (double) kFrame;          // 23.4375 Hz per line (eq. 8)
        const std::size_t B = (std::size_t) kBands;

        // 2.1.3: the Hann window (eq. 2) with the playback-level scale folded in. The scale is the text's own
        // definition: Norm is the largest spectral magnitude of a 0 dBFS sine at 1019.5 Hz over 10 frames, and
        // fac = 10^(Lp/20) / Norm. The window's constant sqrt(8/3) and the FFT's 1/2048 cancel in that ratio.
        double* win = at (l_.window);
        for (int n = 0; n < kFrame; ++n)
            win[n] = 0.5 * (1.0 - det::cos (kTwoPi * (double) n / (double) (kFrame - 1)));
        double norm = 0.0;
        for (int fr = 0; fr < 10; ++fr)
        {
            for (int n = 0; n < kFrame; ++n)
            {
                const double t = (double) (fr * kHop + n);
                fft_[(std::size_t) n] = { win[n] * det::sin (kTwoPi * (1019.5 * t / kSampleRate)), 0.0 };
            }
            core::offline::fftInplace (fft_, -1);
            for (int k = 0; k < kBins; ++k)
            {
                const double re = fft_[(std::size_t) k].real(), im = fft_[(std::size_t) k].imag();
                norm = std::max (norm, core::DetMath::sqrt (re * re + im * im));
            }
        }
        const double gain = det::pow10 (kListeningLevelDb / 20.0) / norm;
        for (int n = 0; n < kFrame; ++n) win[n] = win[n] * gain;

        // 2.1.4, eq. 7: the outer and middle ear, as a POWER weight 10^(W/10). Line 0 (0 Hz) has W = -inf.
        double* w2 = at (l_.w2);
        w2[0] = 0.0;
        for (int k = 1; k < kBins; ++k)
        {
            const double f = (double) k * df / 1000.0;
            const double d = f - 3.3;
            const double w = -0.6 * 3.64 * det::pow (f, -0.8) + 6.5 * peaq_detail::detExp (-0.6 * d * d)
                           - 1e-3 * det::pow (f, 3.6);
            w2[k] = det::pow10 (w / 10.0);
        }

        // 2.1.5: which lines feed which band, and the share of the two border lines (the pseudocode of 2.1.5.1,
        // in Kabal's first/last-line form).
        for (int i = 0; i < kBands; ++i)
        {
            const double fl = kBandLowHz[i], fu = kBandHighHz[i];
            int kl = 0, ku = 0;
            for (int k = 0; k < kBins; ++k)
                if (((double) k + 0.5) * df > fl) { kl = k; break; }
            for (int k = kBins - 1; k >= 0; --k)
                if (((double) k - 0.5) * df < fu) { ku = k; break; }
            bandKl_[(std::size_t) i] = kl;
            bandKu_[(std::size_t) i] = ku;
            at (l_.bandUl)[i] = (std::min (fu, ((double) kl + 0.5) * df) - std::max (fl, ((double) kl - 0.5) * df)) / df;
            at (l_.bandUu)[i] = kl == ku ? 0.0
                              : (std::min (fu, ((double) ku + 0.5) * df) - std::max (fl, ((double) ku - 0.5) * df)) / df;
        }

        aL_ = det::pow10 (-2.7 * 0.25);                 // the lower slope, 27 dB/Bark, per 0.25 Bark (eq. 16)
        aLe_ = det::pow (aL_, 0.4);
        fivedB_ = det::pow10 (0.5);
        for (std::size_t i = 0; i < B; ++i)
        {
            const double fc = kBandCentreHz[i];
            const double fk = fc / 1000.0;
            const double ex = det::pow (fk, -0.8);
            at (l_.pthres)[i] = det::pow10 (0.4 * 0.364 * ex);                         // eq. 13
            at (l_.pthres03)[i] = det::pow (at (l_.pthres)[i], 0.3);                   // eq. 65
            at (l_.ethres)[i] = det::pow10 (0.364 * ex);                               // eq. 60
            const double sdB = -2.0 - 2.05 * peaq_detail::atan (fc / 4000.0)
                             - 0.75 * peaq_detail::atan ((fc / 1600.0) * (fc / 1600.0)); // eq. 61
            at (l_.sLoud)[i] = det::pow10 (sdB / 10.0);
            at (l_.etsLoud)[i] = 1.07664 * det::pow (at (l_.ethres)[i] / (at (l_.sLoud)[i] * 1e4), 0.23);   // eq. 58
            at (l_.aUC)[i] = det::pow10 ((-2.4 - 23.0 / fc) * 0.25);                   // eq. 15 at L = 0 dB
            double g = 0.0, r = 1.0;                                                   // sum_{v<=i} aL^(i-v)
            for (std::size_t v = 0; v <= i; ++v) { g = g + r; r = r * aL_; }
            at (l_.gIL)[i] = g;
            const double mdB = (double) i * 0.25 <= 12.0 ? 3.0 : 0.25 * (double) i * 0.25;   // eq. 25
            at (l_.gm)[i] = det::pow10 (-mdB / 10.0);
            const double tauT = 0.008 + (100.0 / fc) * (0.030 - 0.008);                 // eq. 21
            at (l_.aTime)[i] = peaq_detail::detExp (-(double) kHop / (kSampleRate * tauT));  // eq. 24
            at (l_.bTime)[i] = 1.0 - at (l_.aTime)[i];
            const double tauA = 0.008 + (100.0 / fc) * (0.050 - 0.008);                 // eqs. 41 and 56
            at (l_.aAdapt)[i] = peaq_detail::detExp (-(double) kHop / (kSampleRate * tauA));  // eq. 44
            at (l_.bAdapt)[i] = 1.0 - at (l_.aAdapt)[i];
        }
        // NormSP (eqs. 19-20): the spread of a pattern of 0 dB in every band.
        double* ns = at (l_.normSp);
        for (std::size_t i = 0; i < B; ++i) { ns[i] = 1.0; at (l_.ppR)[i] = 1.0; }
        spread (at (l_.ppR), at (l_.e2R));
        for (std::size_t i = 0; i < B; ++i) ns[i] = at (l_.e2R)[i];

        // 4.8.1: the normalised Hann window of the correlation spectrum (Kabal H.6: sqrt(8/3) / 256).
        double* hw = at (l_.ehsWindow);
        for (int n = 0; n < kEhsLags; ++n)
            hw[n] = (core::DetMath::sqrt (8.0 / 3.0) / (double) kEhsLags)
                  * 0.5 * (1.0 - det::cos (kTwoPi * (double) n / (double) (kEhsLags - 1)));
    }

    // 5.2.4.4: the data boundary of the reference, on the 16-bit scale.
    void watchBoundary (int c, double x) noexcept
    {
        double* w = at (chanBase (c) + kLast5);
        const int cursor = (int) w[5];
        w[cursor] = std::fabs (x) * 32768.0;
        w[5] = (double) ((cursor + 1) % 5);
        if (total_ < 4) return;
        const double sum = w[0] + w[1] + w[2] + w[3] + w[4];
        if (! (sum > 200.0)) return;
        if (boundaryFirst_ < 0) boundaryFirst_ = total_ - 4;
        boundaryLast_ = total_;
    }

    // 2.1.5.1 (+ eq. 14 when `noise` is non-null): lines -> bands, floored at 1e-12.
    void group (const double* fsp, double* out, const double* noise) const noexcept
    {
        const double* ul = at (l_.bandUl);
        const double* uu = at (l_.bandUu);
        for (int i = 0; i < kBands; ++i)
        {
            const int kl = bandKl_[(std::size_t) i], ku = bandKu_[(std::size_t) i];
            double s = ul[i] * fsp[kl];
            for (int k = kl + 1; k < ku; ++k) s = s + fsp[k];
            s = s + uu[i] * fsp[ku];
            s = std::max (s, 1e-12);
            out[i] = noise != nullptr ? s + noise[i] : s;
        }
    }

    // 2.1.7, eqs. 15-20: the level-dependent spreading, summed with exponent 0.4, in Kabal's order (F.4).
    void spread (const double* pp, double* out) noexcept
    {
        namespace det = core::det;
        const int B = kBands;
        const double* aUC = at (l_.aUC);
        const double* gIL = at (l_.gIL);
        const double* ns = at (l_.normSp);
        double* ene = at (l_.ene);
        double* aucee = at (l_.aucee);
        double* es = at (l_.es);
        for (int m = 0; m < B; ++m)
        {
            const double e = pp[m];
            const double aUCE = aUC[m] * det::pow (e, 0.2 * 0.25);   // the upper slope's level term, 0.2 dB/Bark/dB
            double gIU = 0.0, r = 1.0;
            for (int v = m; v < B; ++v) { gIU = gIU + r; r = r * aUCE; }
            const double en = e / (gIL[m] + gIU - 1.0);
            aucee[m] = det::pow (aUCE, 0.4);
            ene[m] = det::pow (en, 0.4);
        }
        es[B - 1] = ene[B - 1];
        for (int m = B - 2; m >= 0; --m) es[m] = aLe_ * es[m + 1] + ene[m];
        for (int m = 0; m < B - 1; ++m)
        {
            double r = ene[m];
            const double a = aucee[m];
            for (int i = m + 1; i < B; ++i) { r = r * a; es[i] = es[i] + r; }
        }
        for (int i = 0; i < B; ++i) out[i] = det::pow (es[i], 2.5) / ns[i];
    }

    // The windowed, scaled power spectrum of one programme's frame (eqs. 2-6), lines 0..1024.
    void transform (const double* ring, std::size_t start, double* x2) noexcept
    {
        const double* win = at (l_.window);
        const std::size_t mask = (std::size_t) (kFrame - 1);
        for (int j = 0; j < kFrame; ++j)
            fft_[(std::size_t) j] = { win[j] * ring[(start + (std::size_t) j) & mask], 0.0 };
        core::offline::fftInplace (fft_, -1);
        for (int k = 0; k < kBins; ++k)
        {
            const double re = fft_[(std::size_t) k].real(), im = fft_[(std::size_t) k].imag();
            x2[k] = re * re + im * im;
        }
    }

    void analyseFrame (long long clock) noexcept
    {
        namespace det = core::det;
        const long long f = frames_;
        if (f >= capacityFrames_) return;
        const std::size_t start = (std::size_t) (clock & (kFrame - 1));   // the slot of the frame's first sample
        const std::size_t mask = (std::size_t) (kFrame - 1);
        const int B = kBands;

        // 5.2.4.3: the energy of the newest half, every channel of both programmes.
        bool energetic = false;
        for (int c = 0; c < channels_; ++c)
        {
            const double* rr = at (ringRef (c));
            const double* rt = at (ringTest (c));
            double enR = 0.0, enT = 0.0;
            for (int j = kHop; j < kFrame; ++j)
            {
                const std::size_t idx = (start + (std::size_t) j) & mask;
                const double a = rr[idx] * 32768.0, b = rt[idx] * 32768.0;
                enR = enR + a * a;
                enT = enT + b * b;
            }
            if (enR >= 8000.0 || enT >= 8000.0) energetic = true;
        }

        const double* w2 = at (l_.w2);
        const double* pthres = at (l_.pthres);
        double* x2R = at (l_.x2R); double* x2T = at (l_.x2T);
        double* feR = at (l_.feR); double* feT = at (l_.feT); double* nz = at (l_.nz);
        double* ppR = at (l_.ppR); double* ppT = at (l_.ppT);
        double* e2R = at (l_.e2R); double* e2T = at (l_.e2T);
        double* eR = at (l_.eR); double* eT = at (l_.eT);
        double* epR = at (l_.epR); double* epT = at (l_.epT);
        double* modR = at (l_.modR); double* modT = at (l_.modT);
        double* pn = at (l_.pn);

        for (int c = 0; c < channels_; ++c)
        {
            const std::size_t base = chanBase (c);
            const double* rr = at (ringRef (c));
            const double* rt = at (ringTest (c));

            // 2.1.3: one transform per programme. Not both through one complex transform (reference real, test
            // imaginary): unpacking that leaves rounding of one programme in the other's bins, so a test identical to
            // its reference read an error spectrum 150 dB down instead of none at all.
            transform (rr, start, x2R);
            transform (rt, start, x2T);
            for (int k = 0; k < kBins; ++k)
            {
                feR[k] = x2R[k] * w2[k];
                feT[k] = x2T[k] * w2[k];
                const double d = core::DetMath::sqrt (feR[k]) - core::DetMath::sqrt (feT[k]);     // eq. 62: magnitudes
                nz[k] = d * d;
            }

            // 2.1.5-2.1.8: pitch patterns, spreading, forward masking.
            group (feR, ppR, pthres);
            group (feT, ppT, pthres);
            group (nz, pn, nullptr);
            spread (ppR, e2R);
            spread (ppT, e2T);
            {
                double* efR = at (base + kEfR); double* efT = at (base + kEfT);
                const double* a = at (l_.aTime); const double* b = at (l_.bTime);
                for (int k = 0; k < B; ++k)
                {
                    efR[k] = a[k] * efR[k] + b[k] * e2R[k];
                    efT[k] = a[k] * efT[k] + b[k] * e2T[k];
                    eR[k] = std::max (efR[k], e2R[k]);
                    eT[k] = std::max (efT[k], e2T[k]);
                }
            }

            // 3.3: overall loudness of both, for the loudness threshold of 5.2.4.2.
            double ntotR = 0.0, ntotT = 0.0;
            {
                const double* s = at (l_.sLoud); const double* et = at (l_.ethres); const double* ets = at (l_.etsLoud);
                for (int k = 0; k < B; ++k)
                {
                    const double nr = ets[k] * (det::pow (1.0 - s[k] + s[k] * eR[k] / et[k], 0.23) - 1.0);
                    const double nt = ets[k] * (det::pow (1.0 - s[k] + s[k] * eT[k] / et[k], 0.23) - 1.0);
                    ntotR = ntotR + std::max (nr, 0.0);
                    ntotT = ntotT + std::max (nt, 0.0);
                }
                ntotR = (24.0 / (double) B) * ntotR;
                ntotT = (24.0 / (double) B) * ntotT;
            }

            // 3.2: modulation, from the unsmeared patterns.
            {
                const double* a = at (l_.aAdapt); const double* b = at (l_.bAdapt);
                double* deR = at (base + kDeR); double* deT = at (base + kDeT);
                double* eaR = at (base + kEaR); double* eaT = at (base + kEaT);
                double* eeR = at (base + kEeR); double* eeT = at (base + kEeT);
                const double fss = kSampleRate / (double) kHop;
                for (int k = 0; k < B; ++k)
                {
                    const double r = det::pow (e2R[k], 0.3), t = det::pow (e2T[k], 0.3);
                    deR[k] = a[k] * deR[k] + b[k] * fss * std::fabs (r - eeR[k]);
                    deT[k] = a[k] * deT[k] + b[k] * fss * std::fabs (t - eeT[k]);
                    eaR[k] = a[k] * eaR[k] + b[k] * r;
                    eaT[k] = a[k] * eaT[k] + b[k] * t;
                    eeR[k] = r;
                    eeT[k] = t;
                    modR[k] = deR[k] / (1.0 + eaR[k] / 0.3);
                    modT[k] = deT[k] / (1.0 + eaT[k] / 0.3);
                }
            }

            // 4.2: modulation differences and the temporal weight (eqs. 63-65, Table 10).
            double md1 = 0.0, md2 = 0.0, tw = 0.0;
            {
                const double* eaR = at (base + kEaR);
                const double* p03 = at (l_.pthres03);
                for (int k = 0; k < B; ++k)
                {
                    double num1, num2;
                    if (modR[k] > modT[k]) { num1 = modR[k] - modT[k]; num2 = 0.1 * num1; }
                    else                   { num1 = modT[k] - modR[k]; num2 = num1; }
                    md1 = md1 + num1 / (1.0 + modR[k]);
                    md2 = md2 + num2 / (0.01 + modR[k]);
                    tw = tw + eaR[k] / (eaR[k] + 100.0 * p03[k]);
                }
                md1 = (100.0 / (double) B) * md1;
                md2 = (100.0 / (double) B) * md2;
            }

            adapt (base, eR, eT, epR, epT);

            // 4.3: noise loudness (eqs. 66-68, Table 11 row NoiseLoudB: alpha 1.5, ThresFac0 0.15, S0 0.5, NLmin 0).
            double nl = 0.0;
            for (int k = 0; k < B; ++k)
            {
                const double sR = 0.15 * modR[k] + 0.5, sT = 0.15 * modT[k] + 0.5;
                const double num = std::max (sT * epT[k] - sR * epR[k], 0.0);
                if (! (num > 0.0)) continue;                                  // the term is exactly zero
                const double beta = peaq_detail::detExp (-1.5 * (epT[k] - epR[k]) / epR[k]);
                const double den = pthres[k] + sR * epR[k] * beta;
                nl = nl + det::pow (pthres[k] / sT, 0.23) * (det::pow (1.0 + num / den, 0.23) - 1.0);
            }
            nl = (24.0 / (double) B) * nl;
            if (nl < 0.0) nl = 0.0;

            // 4.5: noise to mask.
            double nmrSum = 0.0, nmrMax = 0.0;
            {
                const double* gm = at (l_.gm);
                for (int k = 0; k < B; ++k)
                {
                    const double r = pn[k] / (gm[k] * eR[k]);
                    nmrSum = nmrSum + r;
                    nmrMax = std::max (nmrMax, r);
                }
            }

            // 4.4.1: bandwidths, on the unweighted spectra, levels compared as powers (+10 dB = x10, +5 dB = x3.16).
            // A line of zero power has the level -inf, and the comparisons keep IEEE's meaning of it: when the test's
            // top lines are digitally silent, ZeroThreshold is -inf and "-inf >= 10 + -inf" holds — a frame of
            // silence, or one where the test drops out under a playing reference, reads 921 and 921 lines. The text
            // gives no other value to a zero level. GstPEAQ, the oracle, floors the levels instead and does not count
            // such frames: on two drum loops with silent frames, 0.15 and 1.0 lines of mean bandwidth apart.
            double zt = x2T[921];
            for (int k = 921; k < 1024; ++k) zt = std::max (zt, x2T[k]);
            int bwr = 0, bwt = 0;
            for (int k = 920; k >= 0; --k)
                if (x2R[k] >= 10.0 * zt) { bwr = k + 1; break; }
            for (int k = bwr - 1; k >= 0; --k)
                if (x2T[k] >= fivedB_ * zt) { bwt = k + 1; break; }

            // 4.7: detection probability per band (eqs. 72-78).
            {
                double* pd = at (base + kPd); double* qd = at (base + kQd);
                for (int k = 0; k < B; ++k)
                {
                    const double dR = 10.0 * det::log10 (eR[k]), dT = 10.0 * det::log10 (eT[k]);
                    const double e = dR - dT;
                    double L; bool four;
                    if (e > 0.0) { L = 0.3 * dR + 0.7 * dT; four = true; }
                    else         { L = dT; four = false; }
                    const double s = L > 0.0
                        ? 5.95072 * det::pow (6.39468 / L, 1.71332)
                              + (-0.198719 + L * (0.0550197 + L * (-0.00102438 + L * (5.05622e-6 + L * 9.01033e-11))))
                        : 1e30;
                    const double x = e / s;
                    const double x2 = x * x;
                    const double xb = four ? x2 * x2 : x2 * x2 * x2;
                    pd[k] = 1.0 - det::exp2 (-xb);                                  // 1 - 0.5^((e/s)^b)
                    qd[k] = std::fabs (std::trunc (e)) / s;
                }
            }

            const double ehs = energetic ? errorHarmonicStructure (x2R, x2T) : -1.0;

            hist (f, c, kMd1) = md1;      hist (f, c, kMd2) = md2;        hist (f, c, kTempWt) = tw;
            hist (f, c, kNl) = nl;        hist (f, c, kBwRef) = bwr;      hist (f, c, kBwTest) = bwt;
            hist (f, c, kNmrAvg) = nmrSum / (double) B;                    hist (f, c, kNmrMax) = nmrMax;
            hist (f, c, kEhs) = ehs;      hist (f, c, kNtotRef) = ntotR;  hist (f, c, kNtotTest) = ntotT;
        }

        // 4.7: the binaural channel takes the larger probability and step count per band (eqs. 79-82).
        double prodP = 1.0, sumQ = 0.0;
        for (int k = 0; k < B; ++k)
        {
            double pb = 0.0, qb = 0.0;
            for (int c = 0; c < channels_; ++c)
            {
                pb = std::max (pb, at (chanBase (c) + kPd)[k]);
                qb = std::max (qb, at (chanBase (c) + kQd)[k]);
            }
            prodP = prodP * (1.0 - pb);
            sumQ = sumQ + qb;
        }
        fhist (f, 0) = 1.0 - prodP;
        fhist (f, 1) = sumQ;
        fhist (f, 2) = energetic ? 1.0 : 0.0;
        ++frames_;
    }

    // 3.1: level adaptation (eqs. 42-47), then pattern adaptation (eqs. 48-53) with M = 8 (M1 = 3, M2 = 4).
    void adapt (std::size_t base, const double* eR, const double* eT, double* epR, double* epT) noexcept
    {
        const int B = kBands;
        const double* a = at (l_.aAdapt); const double* b = at (l_.bAdapt);
        double* pR = at (base + kPR); double* pT = at (base + kPT);
        double* rn = at (base + kRn); double* rd = at (base + kRd);
        double* pcR = at (base + kPcR); double* pcT = at (base + kPcT);
        double* rR = at (l_.rR); double* rT = at (l_.rT);
        double sn = 0.0, sd = 0.0;
        for (int k = 0; k < B; ++k)
        {
            pR[k] = a[k] * pR[k] + b[k] * eR[k];
            pT[k] = a[k] * pT[k] + b[k] * eT[k];
            sn = sn + core::DetMath::sqrt (pT[k] * pR[k]);
            sd = sd + pT[k];
        }
        const double q = sd > 0.0 ? sn / sd : 1.0;
        const double cl = q * q;
        for (int k = 0; k < B; ++k)
        {
            if (cl > 1.0) { epR[k] = eR[k] / cl; epT[k] = eT[k]; }
            else          { epR[k] = eR[k];      epT[k] = eT[k] * cl; }
            rn[k] = a[k] * rn[k] + epT[k] * epR[k];
            rd[k] = a[k] * rd[k] + epR[k] * epR[k];
            if (rd[k] > 0.0)
            {
                if (rn[k] >= rd[k]) { rR[k] = 1.0; rT[k] = rd[k] / rn[k]; }
                else                { rR[k] = rn[k] / rd[k]; rT[k] = 1.0; }
            }
            else if (rn[k] > 0.0) { rT[k] = 0.0; rR[k] = 1.0; }
            else if (k > 0)       { rT[k] = rT[k - 1]; rR[k] = rR[k - 1]; }
            else                  { rT[k] = 1.0; rR[k] = 1.0; }
        }
        for (int k = 0; k < B; ++k)
        {
            const int iL = std::max (k - 3, 0), iU = std::min (k + 4, B - 1);
            double s1 = 0.0, s2 = 0.0;
            for (int i = iL; i <= iU; ++i) { s1 = s1 + rR[i]; s2 = s2 + rT[i]; }
            const double m = (double) (iU - iL + 1);
            pcR[k] = a[k] * pcR[k] + b[k] * (s1 / m);
            pcT[k] = a[k] * pcT[k] + b[k] * (s2 / m);
            epR[k] = epR[k] * pcR[k];
            epT[k] = epT[k] * pcT[k];
        }
    }

    // 4.8.1, read with Kabal 5.7 and H.6. The error vector is D[k] = log(|X_T|^2 / |X_R|^2) of the weighted spectra
    // from line 1 (line 0's weight is -inf dB, its log ratio undefined; elsewhere the weight cancels in the ratio, so
    // the unweighted powers are used). Its normalised autocorrelation runs over vectors of 256 at lags 0..255 —
    // the text's own example: "the first value ... aligning Ft[0] with F0[0] and the last by aligning Ft[0] with
    // F0[255]" (Kabal took 1..256). Then the mean is removed, a normalised Hann window applied, a 256-point power
    // spectrum taken, and the largest value after the first valley kept. Checked against GstPEAQ, mean |delta EHS|:
    // these readings 0.0008 on 48 drum pairs, 0.0003 on 96 masters, 0.034 on three tonal pairs (EHS 1.8-5.6); lines
    // from 0 or lags 1..256: 0.002 on the drums, 0.13 on the tonal pairs.
    // THE ORDER OF MEAN AND WINDOW. The text: the correlations are "windowed with a normalized Hann window and, after
    // removing the DC component by subtracting the average value, a power spectrum is computed". Windowing first and
    // then removing the mean of the windowed vector leaves the window's own main lobe at lines 1-2, right after the
    // "first valley" at line 0, and the peak search returns it: EHS of 125 on ordinary drum pairs where GstPEAQ reads
    // 0.1, against Table 13's scaling range of 0.074..13.9 — the network was trained on a mean removed before the
    // window (Kabal E.2 reads it so too). The mean is removed before.
    double errorHarmonicStructure (const double* x2R, const double* x2T) noexcept
    {
        namespace det = core::det;
        constexpr int M = kEhsLags;          // vector length = number of lags
        constexpr double kFloor = 1e-30;
        double* d = at (l_.ehsD);
        double* cn = at (l_.ehsC);
        for (int k = 0; k < 2 * M; ++k)
            d[k] = det::log2 (std::max (x2T[k + 1], kFloor) / std::max (x2R[k + 1], kFloor));   // the base cancels too
        // C[l] = sum_{j<M} d[j] d[j+l], l = 0..M, as one packed 512-point transform: a = d[0..M) zero-padded (real),
        // b = d[0..2M) (imaginary); correlation = IFFT(conj(A) B) / 512.
        constexpr int N = 2 * M;
        for (int j = 0; j < N; ++j) fftCorr_[(std::size_t) j] = { j < M ? d[j] : 0.0, d[j] };
        core::offline::fftInplace (fftCorr_, -1);
        // unpack A and B, form conj(A) * B in place (bins k and N-k are read before either is written)
        for (int k = 0; k <= N / 2; ++k)
        {
            const int kn = (N - k) & (N - 1);
            const std::complex<double> z = fftCorr_[(std::size_t) k], w = fftCorr_[(std::size_t) kn];
            auto cross = [] (std::complex<double> zz, std::complex<double> ww)
            {
                const double aRe = 0.5 * (zz.real() + ww.real()), aIm = 0.5 * (zz.imag() - ww.imag());
                const double bRe = 0.5 * (zz.imag() + ww.imag()), bIm = 0.5 * (ww.real() - zz.real());
                return std::complex<double> (aRe * bRe + aIm * bIm, aRe * bIm - aIm * bRe);   // conj(A) * B
            };
            const std::complex<double> ck = cross (z, w);
            const std::complex<double> cnk = cross (w, z);
            fftCorr_[(std::size_t) k] = ck;
            fftCorr_[(std::size_t) kn] = cnk;
        }
        core::offline::fftInplace (fftCorr_, +1);
        double s0 = 0.0;
        for (int j = 0; j < M; ++j) s0 = s0 + d[j] * d[j];
        double sl = s0;
        cn[0] = 1.0;
        for (int l = 1; l < M; ++l)
        {
            sl = sl + d[l + M - 1] * d[l + M - 1] - d[l - 1] * d[l - 1];
            const double c = fftCorr_[(std::size_t) l].real() / (double) N;
            const double den = s0 * sl;
            cn[l] = den > 0.0 ? c / core::DetMath::sqrt (den) : 1.0;
        }
        double mean = 0.0;
        for (int l = 0; l < M; ++l) mean = mean + cn[l];
        mean = mean / (double) M;
        const double* hw = at (l_.ehsWindow);
        for (int l = 0; l < M; ++l) fftEhs_[(std::size_t) l] = { hw[l] * (cn[l] - mean), 0.0 };
        core::offline::fftInplace (fftEhs_, -1);
        auto power = [this] (int k)
        {
            const double re = fftEhs_[(std::size_t) k].real(), im = fftEhs_[(std::size_t) k].imag();
            return re * re + im * im;
        };
        int k = 1;
        while (k <= M / 2 && power (k) <= power (k - 1)) ++k;          // down to the first valley
        double peak = 0.0;
        for (; k <= M / 2; ++k) peak = std::max (peak, power (k));
        return peak;
    }

    void computeResult() noexcept
    {
        namespace det = core::det;
        PeaqResult r;
        r.frames = frames_;
        r.nonFiniteSamples = nonFinite_;
        r.outOfRangeSamples = outOfRange_;
        if (nonFinite_ > 0) { r.verdict = PeaqVerdict::NonFinite; result_ = r; return; }
        if (outOfRange_ > 0) { r.verdict = PeaqVerdict::OutOfRange; result_ = r; return; }
        if (boundaryFirst_ < 0 || frames_ == 0) { r.verdict = PeaqVerdict::NoSignal; result_ = r; return; }
        const long long f0 = boundaryFirst_ <= kFrame - 1 ? 0 : (boundaryFirst_ - (kFrame - 1) + kHop - 1) / kHop;
        const long long f1 = std::min (frames_ - 1, boundaryLast_ / kHop);
        if (f1 < f0) { r.verdict = PeaqVerdict::NoSignal; result_ = r; return; }
        r.firstFrame = f0;
        r.lastFrame = f1;
        // 5.2.4.1: "the first 0.5 s of the measurement" — counted from the start of the programme, not from the data
        // boundary (Kabal counts from the boundary; GstPEAQ, the oracle, from the start, and so does the text).
        const long long fDelay = std::max (f0, (long long) kDelayFrames);
        // 4.3 and 5.2.4.2: the noise loudness counts from 50 ms after "the overall loudness of one of the audio channels
        // has once reached NThres = 0.1 sone for both test and Reference Signal". Read as two marks per channel —
        // the frame where the reference first reaches it and the frame where the test does, whenever each happens —
        // and the later of the two; the earliest channel wins. (Requiring both in the same frame would leave a pair
        // whose loud passages do not overlap with no noise loudness at all.)
        long long fLoud = f1 + 1;
        for (int c = 0; c < channels_; ++c)
        {
            long long firstR = -1, firstT = -1;
            for (long long f = f0; f <= f1 && (firstR < 0 || firstT < 0); ++f)
            {
                if (firstR < 0 && hist (f, c, kNtotRef) >= 0.1) firstR = f;
                if (firstT < 0 && hist (f, c, kNtotTest) >= 0.1) firstT = f;
            }
            if (firstR >= 0 && firstT >= 0) fLoud = std::min (fLoud, std::max (firstR, firstT) + kLoudnessDelayFrames);
        }
        const long long fNl = std::max (fDelay, fLoud);

        std::array<double, kPeaqMovCount> mov {};
        for (int c = 0; c < channels_; ++c)
        {
            double bwR = 0.0, bwT = 0.0, nmr = 0.0, rel = 0.0, ehs = 0.0;
            long long nBw = 0, nEhs = 0;
            for (long long f = f0; f <= f1; ++f)
            {
                if (hist (f, c, kBwRef) > 346.0) { bwR = bwR + hist (f, c, kBwRef); bwT = bwT + hist (f, c, kBwTest); ++nBw; }
                nmr = nmr + hist (f, c, kNmrAvg);
                if (disturbedFrame (hist (f, c, kNmrMax))) rel = rel + 1.0;
                if (fhist (f, 2) > 0.0) { ehs = ehs + hist (f, c, kEhs); ++nEhs; }
            }
            const double nAll = (double) (f1 - f0 + 1);
            double win = 0.0, sw = 0.0, sw1 = 0.0, sw2 = 0.0;
            long long winFrames = 0;
            if (fDelay <= f1)
            {
                const long long n = f1 - fDelay + 1;
                if (n >= 4)
                {
                    double acc = 0.0;
                    for (long long f = fDelay + 3; f <= f1; ++f)
                    {
                        const double m = (core::DetMath::sqrt (hist (f, c, kMd1)) + core::DetMath::sqrt (hist (f - 1, c, kMd1))
                                        + core::DetMath::sqrt (hist (f - 2, c, kMd1)) + core::DetMath::sqrt (hist (f - 3, c, kMd1))) / 4.0;
                        acc = acc + (m * m) * (m * m);
                    }
                    win = core::DetMath::sqrt (acc / (double) (n - 3));
                    winFrames = n - 3;
                }
                for (long long f = fDelay; f <= f1; ++f)
                {
                    const double w = hist (f, c, kTempWt);
                    sw = sw + w;
                    sw1 = sw1 + w * hist (f, c, kMd1);
                    sw2 = sw2 + w * hist (f, c, kMd2);
                }
            }
            double nl2 = 0.0; long long nNl = 0;
            for (long long f = fNl; f <= f1; ++f) { const double v = hist (f, c, kNl); nl2 = nl2 + v * v; ++nNl; }

            // An average over no frame is NaN, never a made-up 0: the grade then becomes Undefined.
            constexpr double kNone = std::numeric_limits<double>::quiet_NaN();
            const double per[kPeaqMovCount] {
                nBw > 0 ? bwR / (double) nBw : kNone,
                nBw > 0 ? bwT / (double) nBw : kNone,
                10.0 * det::log10 (nmr / nAll),
                winFrames > 0 ? win : kNone,
                0.0,                                                       // ADB: binaural, below
                nEhs > 0 ? 1000.0 * ehs / (double) nEhs : kNone,
                sw > 0.0 ? sw1 / sw : kNone,
                sw > 0.0 ? sw2 / sw : kNone,
                nNl > 0 ? core::DetMath::sqrt (nl2 / (double) nNl) : kNone,
                0.0,                                                       // MFPD: binaural, below
                rel / nAll
            };
            for (int i = 0; i < kPeaqMovCount; ++i) mov[(std::size_t) i] = mov[(std::size_t) i] + per[i];
            r.channelNmrDb[(std::size_t) c] = per[(int) PeaqMov::TotalNmr];
            const double eR = at (chanBase (c) + kEnergy)[0], eE = at (chanBase (c) + kEnergy)[1];
            r.channelResidualDb[(std::size_t) c] = eE == 0.0 ? -std::numeric_limits<double>::infinity()
                                                 : (eR > 0.0 ? 10.0 * det::log10 (eE / eR) : std::numeric_limits<double>::infinity());
        }
        for (double& v : mov) v = v / (double) channels_;

        // 4.7.1-4.7.2 on the binaural channel: c0 = 0.9, c1 = 1 (the note to eq. 86).
        double pt = 0.0, pm = 0.0, qs = 0.0;
        long long nd = 0;
        for (long long f = f0; f <= f1; ++f)
        {
            pt = 0.1 * fhist (f, 0) + 0.9 * pt;
            pm = std::max (pm, pt);
            if (fhist (f, 0) > 0.5) ++nd;
            qs = qs + fhist (f, 1);
        }
        mov[(std::size_t) PeaqMov::Mfpd] = pm;
        mov[(std::size_t) PeaqMov::Adb] = nd == 0 ? 0.0 : (qs > 0.0 ? det::log10 (qs / (double) nd) : -0.5);

        r.mov = mov;
        r.distortionIndex = distortionIndex (mov);
        r.modelOdg = odgFromDi (r.distortionIndex);
        bool defined = std::isfinite (r.distortionIndex);
        for (double v : mov) defined = defined && std::isfinite (v);
        // TRANSPARENT holds per channel: one untouched channel must not average away a damaged one (a right channel
        // at 0.7x beside an untouched left reads -90.9 dB as a mean). And the waveform error must be small too: the
        // model compares magnitude spectra, so a channel with its polarity flipped reads no noise at all.
        bool transparent = true;
        for (int c = 0; c < channels_; ++c)
            transparent = transparent && r.channelNmrDb[(std::size_t) c] < kTransparentNmrDb
                                      && r.channelResidualDb[(std::size_t) c] < kTransparentResidualDb;
        if (! defined)        { r.verdict = PeaqVerdict::Undefined; r.odg = r.modelOdg; }
        else if (transparent) { r.verdict = PeaqVerdict::Transparent; r.odg = 0.0; }
        else                  { r.verdict = PeaqVerdict::Graded; r.odg = r.modelOdg; }
        result_ = r;
    }

    Layout l_ {};
    std::vector<double> arena_;
    std::vector<std::complex<double>> fft_, fftCorr_, fftEhs_;
    std::array<int, kPeaqBands> bandKl_ {}, bandKu_ {};
    double aL_ = 0.0, aLe_ = 0.0, fivedB_ = 0.0;
    int channels_ = 0;
    long long maxSamples_ = 0, capacityFrames_ = 0;
    long long total_ = 0, frames_ = 0, nonFinite_ = 0, outOfRange_ = 0;
    long long boundaryFirst_ = -1, boundaryLast_ = -1;
    bool prepared_ = false, finished_ = false;
    PeaqResult result_ {};
};

} // namespace felitronics::analysis
