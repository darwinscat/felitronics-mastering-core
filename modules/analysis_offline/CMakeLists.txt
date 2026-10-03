# felitronics::analysis_offline — the OFFLINE programme analyzers (ProgrammeReport, SourceForensics,
# HumDetector, LowEnd, BandBursts, StereoBandBursts, BandCrest, PeakExcursions, ClipDetector, WaveformPeaks,
# StereoColumns, SpectrumFrames). They compose over core's eq/stereo primitives (eq::Crossover2 is the LR4,
# eq::Svf the biquad, stereo::MidSide the M/S pair) instead of hand-rolling a private copy of each, so the
# target links felitronics::eq and felitronics::stereo as well as felitronics::analysis. The headers keep
# their core spelling, <felitronics/analysis/...>, and the namespace stays felitronics::analysis; this
# directory is their include root.
add_library(felitronics_analysis_offline INTERFACE)
add_library(felitronics::analysis_offline ALIAS felitronics_analysis_offline)
target_include_directories(felitronics_analysis_offline INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(felitronics_analysis_offline INTERFACE felitronics_storage
    felitronics::analysis felitronics::eq felitronics::stereo)
target_compile_features(felitronics_analysis_offline INTERFACE cxx_std_20)

if(FELITRONICS_BUILD_TESTS)
    add_executable(felitronics_source_storage_parity_tests tests/SourceStorageParityTests.cpp)
    target_link_libraries(felitronics_source_storage_parity_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    if(NOT MSVC)
        target_compile_options(felitronics_source_storage_parity_tests PRIVATE -ffp-contract=off)
    endif()
    add_test(NAME felitronics_source_storage_parity_tests COMMAND felitronics_source_storage_parity_tests)

    add_executable(felitronics_spectrum_frames_tests tests/SpectrumFramesTests.cpp)   # the shared frame producer: DFT null + law 8a
    target_link_libraries(felitronics_spectrum_frames_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_spectrum_frames_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_spectrum_frames_tests COMMAND felitronics_spectrum_frames_tests)

    # The analyzers' half of core's math-policy routing gate (MathPolicyTests there): the public aliases each
    # analyzer declares its members with are the DETERMINISTIC spellings. Compile-time, by static_assert.
    add_executable(felitronics_analyzer_math_policy_tests tests/AnalyzerMathPolicyTests.cpp)
    target_link_libraries(felitronics_analyzer_math_policy_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_analyzer_math_policy_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_analyzer_math_policy_tests COMMAND felitronics_analyzer_math_policy_tests)

    add_executable(felitronics_clip_detector_tests tests/ClipDetectorTests.cpp)   # clipping by construction + a whole-file reference null
    target_link_libraries(felitronics_clip_detector_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_clip_detector_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_clip_detector_tests COMMAND felitronics_clip_detector_tests)

    # WaveformPeaks + StereoColumns — ports of the site's audio-peaks.js / stereo-meter.js, pinned to the spec's own
    # numbers. Built with the tree's own `on`, which is where the natural removal of the stereo products' volatile pin
    # (`mid += m*m`) fuses on arm64 and fails witnesses W5/W6f — see the test file's header for the measured matrix.
    add_executable(felitronics_waveform_index_tests tests/WaveformIndexTests.cpp)
    target_link_libraries(felitronics_waveform_index_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_waveform_index_tests PRIVATE cxx_std_20)
    if(NOT MSVC)
        target_compile_options(felitronics_waveform_index_tests PRIVATE -ffp-contract=off)
    endif()
    add_test(NAME felitronics_waveform_index_tests COMMAND felitronics_waveform_index_tests)

    add_executable(felitronics_waveform_shape_tests tests/WaveformShapeTests.cpp)
    target_link_libraries(felitronics_waveform_shape_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_waveform_shape_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_waveform_shape_tests COMMAND felitronics_waveform_shape_tests)

    # ProgrammeReport — the OFFLINE whole-programme report. It composes over eq::Crossover2 (the LR4) and the
    # stereo primitives; the suite's own whole-file reference drives eq::Crossover2 and analysis::KWeightingFilter
    # directly, so it needs the same surface.
    add_executable(felitronics_programme_report_tests tests/ProgrammeReportTests.cpp)
    target_link_libraries(felitronics_programme_report_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_programme_report_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_programme_report_tests COMMAND felitronics_programme_report_tests)
    # HumDetector — mains hum against the bass notes that look like it (49.0 / 58.3 / 61.7 Hz), plus law 8a
    # over the whole report.
    add_executable(felitronics_hum_detector_tests tests/HumDetectorTests.cpp)
    target_link_libraries(felitronics_hum_detector_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_hum_detector_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_hum_detector_tests COMMAND felitronics_hum_detector_tests)
    # LowEnd — wide bass (LR4 + M/S over the 10 ms grid) and the dominant low note (semitone fold of the
    # shared frame producer).
    add_executable(felitronics_low_end_tests tests/LowEndTests.cpp)
    target_link_libraries(felitronics_low_end_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_low_end_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_low_end_tests COMMAND felitronics_low_end_tests)
    # BandBursts — an offline burst detector: energy in a band against the MEDIAN of a long trailing ring of
    # hops, over eq::Crossover2 (the LR4 that makes the band); and its stereo sibling on Mid and Side.
    add_executable(felitronics_stereo_band_bursts_tests tests/StereoBandBurstsTests.cpp)
    target_link_libraries(felitronics_stereo_band_bursts_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_stereo_band_bursts_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_stereo_band_bursts_tests COMMAND felitronics_stereo_band_bursts_tests)

    add_executable(felitronics_band_bursts_tests tests/BandBurstsTests.cpp)
    target_link_libraries(felitronics_band_bursts_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_band_bursts_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_band_bursts_tests COMMAND felitronics_band_bursts_tests)
    # BandCrest — the peak-to-RMS of a programme per 400 ms block and per band, and the PAIRED loss between a
    # source and its master, over eq::Crossover2 and the oversampler.
    add_executable(felitronics_band_crest_tests tests/BandCrestTests.cpp)
    target_link_libraries(felitronics_band_crest_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_band_crest_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_band_crest_tests COMMAND felitronics_band_crest_tests)
    add_executable(felitronics_peak_excursions_tests tests/PeakExcursionsTests.cpp)   # runs over a ceiling, on the certificate's own reconstruction
    target_link_libraries(felitronics_peak_excursions_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_peak_excursions_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_peak_excursions_tests COMMAND felitronics_peak_excursions_tests)
    add_executable(felitronics_source_forensics_tests tests/SourceForensicsTests.cpp)   # the spectral wall + the sample grid: the traps, two oracles, law 8a
    target_link_libraries(felitronics_source_forensics_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_compile_features(felitronics_source_forensics_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_source_forensics_tests COMMAND felitronics_source_forensics_tests)
    # Peaq — PEAQ Basic (ITU-R BS.1387): the model's parts by hand, five synthetic pairs against GstPEAQ's numbers on
    # the same files (`felitronics_peaq_tests --write <dir>` writes them), pins, verdicts and the analyzer contract.
    # Contract-off like the tool and the wasm module whose bits it pins.
    add_executable(felitronics_peaq_tests tests/PeaqTests.cpp)
    target_link_libraries(felitronics_peaq_tests PRIVATE felitronics::analysis_offline felitronics::io felitronics::test_support)
    target_compile_features(felitronics_peaq_tests PRIVATE cxx_std_20)
    if(NOT MSVC)
        target_compile_options(felitronics_peaq_tests PRIVATE -ffp-contract=off)
    endif()
    add_test(NAME felitronics_peaq_tests COMMAND felitronics_peaq_tests)
    # Split invariance across ALL of the above through ONE harness (tests/split_invariance.h, shared with the
    # mastering module's suite): one call against fixed blocks and ragged cuts with zero-length calls, a clock-only
    # gap where the contract has one, a second maxBlock, and a finish on a prefix — every published output compared
    # as bits, each divergence named by output, size and cut, and a planted one-ulp control it must catch.
    add_executable(felitronics_analyzer_split_invariance_tests tests/SplitInvarianceTests.cpp)
    target_link_libraries(felitronics_analyzer_split_invariance_tests PRIVATE felitronics::analysis_offline felitronics::test_support)
    target_include_directories(felitronics_analyzer_split_invariance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../tests)   # split_invariance.h
    target_compile_features(felitronics_analyzer_split_invariance_tests PRIVATE cxx_std_20)
    add_test(NAME felitronics_analyzer_split_invariance_tests COMMAND felitronics_analyzer_split_invariance_tests)
endif()
